#!/bin/sh
# Run on Linux with a C++20 compiler, CMake, Ninja, DXC with SPIR-V support,
# Python 3, SDL's X11/audio development dependencies and FFmpeg 5+ development
# headers/libraries. Pass an already installed game data directory as argument 1.
set -eu
LIFELINE_SOURCE=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
LIFELINE_WORKSPACE=$(CDPATH= cd -- "$LIFELINE_SOURCE/../.." && pwd)
LIFELINE_ASSETS=${1:-$LIFELINE_WORKSPACE/build/Install/data}
LIFELINE_BUILD_DIR=${LIFELINE_BUILD_DIR:-$LIFELINE_WORKSPACE/dev/build/linux}
LIFELINE_PACKAGE_DIR=${LIFELINE_PACKAGE_DIR:-$LIFELINE_WORKSPACE/build/Steamdeck}
cmake -S "$LIFELINE_SOURCE" -B "$LIFELINE_BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DLIFELINE_BUILD_TOOLS=OFF \
    -DLIFELINE_OPENGL=ON -DLIFELINE_STEAMDECK=ON \
    -DSDL_X11=ON -DSDL_WAYLAND=OFF -DSDL_PIPEWIRE=OFF \
    -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON '-DCMAKE_INSTALL_RPATH=$ORIGIN/lib'
cmake --build "$LIFELINE_BUILD_DIR" --target lifeline --parallel
python3 "$LIFELINE_SOURCE/packaging/steamdeck/package.py" \
    --binary "$LIFELINE_BUILD_DIR/bin/lifeline" --assets "$LIFELINE_ASSETS" \
    --output "$LIFELINE_PACKAGE_DIR"
