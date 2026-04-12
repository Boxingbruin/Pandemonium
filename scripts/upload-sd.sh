#!/usr/bin/env bash
# Copy pandemonium.z64 to the SummerCart64 microSD over USB (sc64deployer sd upload).
# Overwrites the file at the destination path on the card if it already exists.
#
# SD lock: only one of PC (USB) or N64 may use the SD at a time. If you see
# "SD card is locked by the N64 side", turn the N64 off (see SummerCart64 USB docs)
# or run with --reset-first so sc64deployer reset runs first (releases lock; resets cart state).
#
# Environment:
#   SC64_DEPLOYER Same as scripts/deploy.sh
#   SC64_SD_DEST    Path on SD (default: Games/Homebrew/Pandemonium.z64)

set -euo pipefail

_SCRIPTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$_SCRIPTS_DIR/.." && pwd)"
ROM="${ROOT}/pandemonium.z64"
DESC="${ROOT}/pandemonium.description"
SD_DEST="${SC64_SD_DEST:-Games/Homebrew/Pandemonium.z64}"
# Same folder as ROM, same basename, extension .description (patched N64FlashcartMenu)
SD_DESC_DEST="${SD_DEST%.*}.description"
# shellcheck source=sc64-common.sh
source "$_SCRIPTS_DIR/sc64-common.sh"

usage() {
	cat <<'EOF'
Usage: upload-sd.sh [options]

Copies repo pandemonium.z64 to the SC64 SD card via USB (sd upload).

Options:
  --no-build      Skip make; use existing pandemonium.z64 in repo root
  --reset-first   Run sc64deployer reset before sd upload (clears N64 SD lock; resets cart config)
  -v, --verbose   Print commands before running them
  -h, --help      Show this help

Environment:
  SC64_SD_DEST  Destination path on SD (default: Games/Homebrew/Pandemonium.z64)
EOF
}

die() {
	echo "upload-sd.sh: $*" >&2
	exit 1
}

do_build=1
verbose=0
reset_first=0
while [[ $# -gt 0 ]]; do
	case "$1" in
	--no-build)
		do_build=0
		shift
		;;
	--reset-first)
		reset_first=1
		shift
		;;
	-v | --verbose)
		verbose=1
		shift
		;;
	-h | --help)
		usage
		exit 0
		;;
	*)
		die "unknown option: $1 (try --help)"
		;;
	esac
done

sc64_set_deployer_binary die

if [[ "$do_build" -eq 1 ]]; then
	if [[ "$verbose" -eq 1 ]]; then
		echo "Running: make -C $(printf '%q' "$ROOT") pandemonium.z64"
	fi
	make -C "$ROOT" pandemonium.z64
fi

[[ -f "$ROM" ]] || die "ROM not found: $ROM"

if [[ "$verbose" -eq 1 ]]; then
	echo "Running: $(printf '%q' "$SC64") list"
fi
if ! "$SC64" list; then
	die "sc64deployer list failed. Is the SummerCart64 connected and in developer mode?"
fi

if [[ "$reset_first" -eq 1 ]]; then
	if [[ "$verbose" -eq 1 ]]; then
		echo "Running: $(printf '%q' "$SC64") reset"
	fi
	if ! "$SC64" reset; then
		die "sc64deployer reset failed"
	fi
fi

cmd=("$SC64" sd upload "$ROM" "$SD_DEST")
if [[ "$verbose" -eq 1 ]]; then
	printf 'Running:'
	printf ' %q' "${cmd[@]}"
	printf '\n'
fi

log="$(mktemp)"
trap 'rm -f "$log"' EXIT
set -o pipefail
if ! "${cmd[@]}" 2>&1 | tee "$log"; then
	printf 'upload-sd.sh: sd upload failed. Command:' >&2
	printf ' %q' "${cmd[@]}" >&2
	printf '\n' >&2
	if grep -q 'locked by the N64' "$log" 2>/dev/null; then
		printf '\n%s\n' "The N64 still holds the SD lock (menu/game using the card). Options:" >&2
		printf '%s\n' "  • Power off the console, then run upload-sd again (USB can stay connected if your setup allows)." >&2
		printf '%s\n' "  • Or retry with: ./scripts/upload-sd.sh --no-build --reset-first" >&2
	fi
	exit 1
fi

if [[ -f "$DESC" ]]; then
	desc_cmd=("$SC64" sd upload "$DESC" "$SD_DESC_DEST")
	if [[ "$verbose" -eq 1 ]]; then
		printf 'Running:'
		printf ' %q' "${desc_cmd[@]}"
		printf '\n'
	fi
	if ! "${desc_cmd[@]}"; then
		die "sd upload failed for ROM description sidecar (try --reset-first if SD is locked)"
	fi
fi
