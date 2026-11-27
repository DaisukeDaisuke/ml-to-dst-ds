#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
root=$PWD
src="$root/upstream/desmume/desmume/src"
platform="$root/upstream/desmume-webassembly/webassembly"
out="$root/build/desmume-web"
mkdir -p "$out" dist

# Use the pinned upstream's portable core sources without the POSIX UI or JIT.
sources=$(python3 - "$src/frontend/posix/meson.build" <<'PY'
import pathlib
import re
import sys

meson = pathlib.Path(sys.argv[1])
text = meson.read_text(encoding="utf-8")
core = text.split("libdesmume_src = [", 1)[1].split("\n]", 1)[0]
for path in re.findall(r"'(\.\./\.\./[^']+)'", core):
    print((meson.parent / path).resolve())
PY
)
flags=(-O2 -sUSE_ZLIB=1 -DHAVE_LIBZ -DHOST_32
       -I"'${src}'" -I"'${src}/libretro-common/include'" -I"'${platform}/include'")
ninja_file="$out/build.ninja"
objects=()
{
  printf 'rule cpp\n  command = em++ %s -std=c++17 -fwasm-exceptions -MMD -MF $out.d -c $in -o $out\n  depfile = $out.d\n  deps = gcc\n' "${flags[*]}"
  printf 'rule c\n  command = emcc %s -MMD -MF $out.d -c $in -o $out\n  depfile = $out.d\n  deps = gcc\n' "${flags[*]}"
  i=0
  while IFS= read -r source; do
    compiler=cpp
    [[ "$source" != *.c ]] || compiler=c
    escaped=${source//\$/\$\$}
    escaped=${escaped//:/\$:}
    escaped=${escaped// /\$ }
    printf 'build %s.o: %s %s\n' "$i" "$compiler" "$escaped"
    objects+=("$out/$i.o")
    i=$((i + 1))
  done <<< "$root/desmume-template.cpp"$'\n'"$platform/support.c"$'\n'"$sources"$'\n'"$src/mic.cpp"
} > "$ninja_file"
ninja -C "$out" -j "${BUILD_JOBS:-4}"

em++ "${objects[@]}" -O2 -fwasm-exceptions --no-entry \
  -sUSE_ZLIB=1 -sFORCE_FILESYSTEM=1 -sALLOW_MEMORY_GROWTH=1 \
  -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web,node \
  -sEXPORTED_FUNCTIONS=_make_template -sEXPORTED_RUNTIME_METHODS=FS \
  -o dist/desmume-template.mjs
