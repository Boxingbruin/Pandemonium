#!/usr/bin/env bash
# Deploy pandemonium.z64 to a SummerCart64 over USB using sc64deployer.
#
# Prerequisites: install sc64deployer from
# https://github.com/Polprzewodnikowy/SummerCart64/releases
# and connect the cart in developer / USB upload mode.
#
# Environment:
#   SC64_DEPLOYER  Path to the sc64deployer binary, or the extracted release
#                   folder containing it (default: sc64deployer on PATH)
#   SC64_SAVE      If set, passed as --save (preserve EEPROM across uploads)
#   SC64_EXTRA_ARGS  Extra arguments for upload (space-separated, e.g. "--direct")

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROM="${ROOT}/pandemonium.z64"

usage() {
	cat <<'EOF'
Usage: deploy.sh [options]

Options:
  --no-build    Skip make; upload existing pandemonium.z64 in repo root
  -v, --verbose Print commands before running them
  -h, --help    Show this help

Examples:
  ./scripts/deploy.sh
  ./scripts/deploy.sh --no-build
  SC64_DEPLOYER=~/n64-dev/sc64deployer SC64_SAVE=./pandemonium.eep ./scripts/deploy.sh
EOF
}

die() {
	echo "deploy.sh: $*" >&2
	exit 1
}

do_build=1
verbose=0
while [[ $# -gt 0 ]]; do
	case "$1" in
	--no-build)
		do_build=0
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

# Resolve SC64_DEPLOYER: may be the binary, or a directory from the release archive.
resolve_sc64deployer_path() {
	local p="$1"
	if [[ ! -e "$p" ]]; then
		local hint=""
		# Common mistake: release archives are named sc64-deployer-* but the binary/dir is often sc64deployer (no hyphen).
		local alt="${p//sc64-deployer/sc64deployer}"
		if [[ "$alt" != "$p" && -e "$alt" ]]; then
			hint=" — try: $alt"
		fi
		die "SC64_DEPLOYER path does not exist: $p${hint}"
	fi
	if [[ -d "$p" ]]; then
		if [[ -f "$p/sc64deployer" ]]; then
			p="$p/sc64deployer"
		else
			local found
			found="$(find "$p" -maxdepth 4 -type f -name sc64deployer 2>/dev/null | head -n 1)"
			if [[ -n "$found" ]]; then
				p="$found"
			else
				die "SC64_DEPLOYER directory contains no sc64deployer binary (searched up to depth 4): $1"
			fi
		fi
	fi
	if [[ ! -f "$p" ]]; then
		die "SC64_DEPLOYER must be a file or directory: $1"
	fi
	if [[ ! -x "$p" ]]; then
		die "sc64deployer is not executable: $p (try: chmod +x \"$p\")"
	fi
	SC64_RESOLVED="$p"
}

if [[ -n "${SC64_DEPLOYER:-}" ]]; then
	resolve_sc64deployer_path "${SC64_DEPLOYER}"
	SC64="$SC64_RESOLVED"
else
	SC64="$(command -v sc64deployer 2>/dev/null || true)"
	[[ -n "$SC64" ]] || die "sc64deployer not found. Set SC64_DEPLOYER (binary or extracted folder) or add it to PATH."
	[[ -x "$SC64" ]] || die "sc64deployer on PATH is not executable: $SC64"
fi

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

extra_args=()
if [[ -n "${SC64_EXTRA_ARGS:-}" ]]; then
	read -r -a extra_args <<< "$SC64_EXTRA_ARGS"
fi

upload_cmd=("$SC64" upload "$ROM" --save-type eeprom4k)
if [[ -n "${SC64_SAVE:-}" ]]; then
	upload_cmd+=(--save "$SC64_SAVE")
fi
if ((${#extra_args[@]} > 0)); then
	upload_cmd+=("${extra_args[@]}")
fi

if [[ "$verbose" -eq 1 ]]; then
	printf 'Running:'
	printf ' %q' "${upload_cmd[@]}"
	printf '\n'
fi

if ! "${upload_cmd[@]}"; then
	printf 'deploy.sh: upload failed. Command:' >&2
	printf ' %q' "${upload_cmd[@]}" >&2
	printf '\n' >&2
	exit 1
fi
