#!/bin/bash
# Launch Hans in an emulator via Retro68's LaunchAPPL.
#
# LaunchAPPL needs to know how to reach an emulator. The usual setup is a
# ~/.LaunchAPPL.cfg pointing at a Mini vMac / Basilisk II / SheepShaver
# install with a bootable Mac OS 9 image. See the README for details — you
# must supply the OS 9 image and (for real PPC emulation) a Mac ROM; those
# can't be bundled here.
set -euo pipefail
cd "$(dirname "$0")/.."

LAUNCHAPPL="$HOME/Retro68-build/toolchain/bin/LaunchAPPL"

if [ ! -f build/Hans.bin ]; then
    echo "Building first..."
    bash tools/build.sh
fi

if [ ! -x "$LAUNCHAPPL" ]; then
    echo "LaunchAPPL not found at $LAUNCHAPPL" >&2
    exit 1
fi

echo "Launching Hans (Ctrl-C to stop)..."
exec "$LAUNCHAPPL" build/Hans.bin
