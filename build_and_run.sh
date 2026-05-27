#!/usr/bin/env bash
set -euo pipefail

make clean
make

ROM="./pandemonium.z64"
ARES="$HOME/n64/ares/ares-147/build/desktop-ui/ares"

"$ARES" "$ROM"
