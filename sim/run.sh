#!/bin/bash
# PYXIS Big Six — Mac simulator.
#   ./run.sh          connect to the camera over Bluetooth (reads AND writes settings)
#   ./run.sh --demo   no camera: fake values, for working on the layout
set -e
cd "$(dirname "$0")"

if ! command -v sdl2-config >/dev/null; then
  echo "Installing SDL2 with Homebrew (one time)..."
  brew install sdl2
fi
if [ ! -d lvgl ] && [ ! -d ../firmware/deps/lvgl/src ]; then
  echo "Downloading LVGL 9.3 (one time)..."
  git clone --quiet --depth 1 -b v9.3.0 https://github.com/lvgl/lvgl.git lvgl
fi
if [ "$1" != "--demo" ] && ! python3 -c "import bleak" 2>/dev/null; then
  python3 -m pip install bleak
fi

echo "Building (first build takes a minute)..."
make -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" bigsix_sim
./bigsix_sim "$@"
