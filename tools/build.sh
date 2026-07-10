#!/bin/bash
# Configure (once) and build Hans for PowerPC classic Mac OS.
set -euo pipefail
cd "$(dirname "$0")/.."

TOOLCHAIN="$HOME/Retro68-build/toolchain/powerpc-apple-macos/cmake/retroppc.toolchain.cmake"

if [ ! -f build/CMakeCache.txt ]; then
    cmake -B build -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DCMAKE_BUILD_TYPE=Release
fi
cmake --build build -- "$@"
echo
echo "Artifacts:"
ls -la build/Hans.bin build/Hans.dsk build/Hans.APPL 2>/dev/null || true
