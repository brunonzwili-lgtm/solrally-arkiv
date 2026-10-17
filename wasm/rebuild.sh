#!/bin/bash
set -e
source "$HOME/emsdk/emsdk_env.sh" >/dev/null 2>&1
P=/Users/h/Documents/stk-code-wasm/wasm/prefix
cd /Users/h/Documents/stk-code-wasm/cmake_build/Release
emcmake cmake /Users/h/Documents/stk-code-wasm \
  -DNO_SHADERC=on -DCHECK_ASSETS=off -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DCMAKE_PREFIX_PATH=$P -DCMAKE_INCLUDE_PATH=$P/include -DCMAKE_LIBRARY_PATH=$P/lib \
  -DZLIB_ROOT=$P -DZLIB_INCLUDE_DIR=$P/include -DZLIB_LIBRARY=$P/lib/libz.a \
  -DPNG_ROOT=$P -DPNG_INCLUDE_DIRS=$P/include -DPNG_LIBRARY=$P/lib/libpng16.a \
  -DJPEG_ROOT=$P -DJPEG_INCLUDE_DIR=$P/include -DJPEG_LIBRARY=$P/lib/libjpeg.a \
  -DHarfBuzz_INCLUDE_DIR=$P/include/harfbuzz -DHarfBuzz_LIBRARY=$P/lib/libharfbuzz.a \
  -DEMSDK_DIR=$HOME/emsdk -DCMAKE_BUILD_TYPE=Release
make -j4
mkdir -p /Users/h/Documents/stk-code-wasm/wasm/web/game
cp ./bin/* /Users/h/Documents/stk-code-wasm/wasm/web/game/
python3 /Users/h/Documents/stk-code-wasm/wasm/patch_js.py \
  /Users/h/Documents/stk-code-wasm/wasm/fragments \
  /Users/h/Documents/stk-code-wasm/wasm/web/game/supertuxkart.js
echo "REBUILD_COMPLETE"
