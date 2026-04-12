# Shared helpers for scripts that invoke sc64deployer (source this file).
# Usage: source .../sc64-common.sh; sc64_set_deployer_binary your_die_fn
# Sets global: SC64

sc64_set_deployer_binary() {
	local die_fn="$1"
	local p

	if [[ -n "${SC64_DEPLOYER:-}" ]]; then
		p="${SC64_DEPLOYER}"
		if [[ ! -e "$p" ]]; then
			local hint=""
			local alt="${p//sc64-deployer/sc64deployer}"
			if [[ "$alt" != "$p" && -e "$alt" ]]; then
				hint=" — try: $alt"
			fi
			"$die_fn" "SC64_DEPLOYER path does not exist: $p${hint}"
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
					"$die_fn" "SC64_DEPLOYER directory contains no sc64deployer binary (searched up to depth 4): ${SC64_DEPLOYER}"
				fi
			fi
		fi
		if [[ ! -f "$p" ]]; then
			"$die_fn" "SC64_DEPLOYER must be a file or directory: ${SC64_DEPLOYER}"
		fi
		if [[ ! -x "$p" ]]; then
			"$die_fn" "sc64deployer is not executable: $p (try: chmod +x \"$p\")"
		fi
		SC64="$p"
	else
		SC64="$(command -v sc64deployer 2>/dev/null || true)"
		[[ -n "$SC64" ]] || "$die_fn" "sc64deployer not found. Set SC64_DEPLOYER (binary or extracted folder) or add it to PATH."
		[[ -x "$SC64" ]] || "$die_fn" "sc64deployer on PATH is not executable: $SC64"
	fi
}
