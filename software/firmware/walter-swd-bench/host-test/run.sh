#!/usr/bin/env bash
# Host test: the bench tool's SWD engine and provisioning sequence (src/swd.cpp, src/nrf52.cpp, unchanged)
# against a bit-level simulated nRF52840, with an image made by tools/hex2image.py from a synthetic hex.
# Needs g++ and python3. Run from anywhere: host-test/run.sh
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
b="$here/build"
mkdir -p "$b"
python3 "$here/make_test_hex.py" "$b/test.hex"
python3 "$here/../tools/hex2image.py" "$b/image_data.gen.cpp" "$b/test.hex"
g++ -std=c++17 -O2 -Wall -Wextra -Wshadow -I"$here/../src" -I"$here" \
  "$here/../src/swd.cpp" "$here/../src/nrf52.cpp" "$here/sim_target.cpp" "$here/test_main.cpp" \
  "$b/image_data.gen.cpp" -o "$b/swdtest"
"$b/swdtest" "$@"
