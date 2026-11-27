#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p dist
cp web/index.html web/app.js dist/
em++ converter.cpp -std=c++20 -O2 -fwasm-exceptions --no-entry \
  -sUSE_ZLIB=1 -sMODULARIZE=1 -sEXPORT_ES6=1 -sALLOW_MEMORY_GROWTH=1 \
  -sENVIRONMENT=web,node \
  -sEXPORTED_FUNCTIONS=_malloc,_free,_convert_state,_result_data,_result_size,_result_error,_clear_result \
  -sEXPORTED_RUNTIME_METHODS=HEAPU8,UTF8ToString -o dist/converter.mjs
