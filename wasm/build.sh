#!/bin/bash

set -e
set -x

BUILD_TYPE="${1:-Release}"

if command -v nproc &> /dev/null; then
    CORE_COUNT="$(nproc --all)"
else
    CORE_COUNT="$(sysctl -n hw.ncpu)"
fi
BASE_DIR="$(realpath "$(dirname "$0")")"
SRC_DIR="$(dirname "$BASE_DIR")"
WEB_DIR="$BASE_DIR/web"
BUILD_DIR="$SRC_DIR/cmake_build/$BUILD_TYPE"
EMSDK_DIR="$HOME/emsdk"

mkdir -p $BUILD_DIR
cd $BUILD_DIR

source "$EMSDK_DIR/emsdk_env.sh"
embuilder build sdl2 sdl2_ttf sdl2_image sdl2_image_jpg sdl2_image_png
emcmake cmake "$SRC_DIR" -DNO_SHADERC=on -DCHECK_ASSETS=off -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_PREFIX_PATH="$BASE_DIR/prefix" \
    -DCMAKE_INCLUDE_PATH="$BASE_DIR/prefix/include" \
    -DCMAKE_LIBRARY_PATH="$BASE_DIR/prefix/lib" \
    -DZLIB_ROOT="$BASE_DIR/prefix" \
    -DZLIB_INCLUDE_DIR="$BASE_DIR/prefix/include" \
    -DZLIB_LIBRARY="$BASE_DIR/prefix/lib/libz.a" \
    -DPNG_ROOT="$BASE_DIR/prefix" \
    -DPNG_INCLUDE_DIRS="$BASE_DIR/prefix/include" \
    -DPNG_LIBRARY="$BASE_DIR/prefix/lib/libpng16.a" \
    -DJPEG_ROOT="$BASE_DIR/prefix" \
    -DJPEG_INCLUDE_DIR="$BASE_DIR/prefix/include" \
    -DJPEG_LIBRARY="$BASE_DIR/prefix/lib/libjpeg.a" \
    -DHarfBuzz_INCLUDE_DIR="$BASE_DIR/prefix/include/harfbuzz" \
    -DHarfBuzz_LIBRARY="$BASE_DIR/prefix/lib/libharfbuzz.a" \
    -DEMSDK_DIR="$EMSDK_DIR" \
    -DCMAKE_BUILD_TYPE=$BUILD_TYPE
make -j$CORE_COUNT

echo "copying wasm files"
mkdir -p "$WEB_DIR/game"
cp ./bin/* "$WEB_DIR/game/"

echo "applying patches"
python3 "$BASE_DIR/patch_js.py" "$BASE_DIR/fragments" "$WEB_DIR/game/supertuxkart.js"
