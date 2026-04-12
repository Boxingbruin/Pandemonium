#!/usr/bin/env bash
# Reset SummerCart64 to default boot state (bootloader + menu path), same as power-up.
# Runs: sc64deployer reset — see SummerCart64 deployer and USB docs.
#
# Environment:
#   SC64_DEPLOYER  Same as scripts/deploy.sh (binary or extracted folder, or PATH)

set -euo pipefail

_SCRIPTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=sc64-common.sh
source "$_SCRIPTS_DIR/sc64-common.sh"

usage() {
	cat <<'EOF'
Usage: sc64-bootloader.sh [options]

Runs sc64deployer reset so the cart returns to normal bootloader behavior
(undo direct-boot / dev upload state; equivalent to power-up defaults).

Options:
  -v, --verbose  Print commands before running them
  -h, --help     Show this help
EOF
}

die() {
	echo "sc64-bootloader.sh: $*" >&2
	exit 1
}

verbose=0
while [[ $# -gt 0 ]]; do
	case "$1" in
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

if [[ "$verbose" -eq 1 ]]; then
	echo "Running: $(printf '%q' "$SC64") list"
fi
if ! "$SC64" list; then
	die "sc64deployer list failed. Is the SummerCart64 connected and in developer mode?"
fi

if [[ "$verbose" -eq 1 ]]; then
	echo "Running: $(printf '%q' "$SC64") reset"
fi
exec "$SC64" reset
