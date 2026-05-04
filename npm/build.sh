#!/usr/bin/env bash
# Build Magic WASM and copy artifacts into this npm/ directory.
#
# Usage:
#   npm/build.sh [--release] [--test] [--pack]
#
#   --release   Omit debug symbols (-g).
#   --test      Run `npm run test` after copying artifacts.
#   --pack      Run `npm pack` after copying artifacts (and tests, if given).
#
# Requirements (must be on PATH or set via env vars before running):
#   emcc / emmake / emconfigure  — Emscripten compiler tools
#   make, gcc                    — standard build tools
#   node, npm                    — only required for --test / --pack
#
# Environment:
#   EMSDK_DIR   Path to an activated emsdk checkout.
#               If set, emsdk_env.sh is sourced from there.
#               If unset, emcc must already be on PATH (e.g. sourced externally).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

OPT_RELEASE=0
OPT_TEST=0
OPT_PACK=0
for arg in "$@"; do
  case "$arg" in
    --release) OPT_RELEASE=1 ;;
    --test)    OPT_TEST=1    ;;
    --pack)    OPT_PACK=1    ;;
    *) echo "Unknown option: $arg" >&2; exit 1 ;;
  esac
done

# --- locate emscripten -------------------------------------------------------
if [ -n "${EMSDK_DIR:-}" ]; then
  if [ ! -f "$EMSDK_DIR/emsdk_env.sh" ]; then
    echo "Error: EMSDK_DIR is set to '$EMSDK_DIR' but emsdk_env.sh was not found there." >&2
    exit 1
  fi
  # shellcheck source=/dev/null
  source "$EMSDK_DIR/emsdk_env.sh"
else
  if ! command -v emcc &>/dev/null; then
    echo "Error: emcc not found on PATH and EMSDK_DIR is not set." >&2
    echo "  Either source emsdk_env.sh before running this script," >&2
    echo "  or set EMSDK_DIR to your emsdk checkout directory." >&2
    exit 1
  fi
fi

echo "Using emcc: $(command -v emcc)"
emcc --version | head -1

# --- portability helpers -----------------------------------------------------
# CPU count: Linux has nproc, macOS has sysctl, fall back to getconf.
ncpu() {
  if command -v nproc &>/dev/null; then
    nproc
  elif [ "$(uname)" = "Darwin" ]; then
    sysctl -n hw.ncpu
  else
    getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1
  fi
}

# Portable in-place sed (BSD sed on macOS disagrees with GNU on -i).
# Uses redirect-back instead of mv so the file's mode bits are preserved
# (configure must stay executable across build.sh invocations).
sed_strip_cr() {
  local file=$1 tmp
  tmp=$(mktemp)
  sed 's/\r//' "$file" > "$tmp" && cat "$tmp" > "$file" && rm "$tmp"
}

# --- clean -------------------------------------------------------------------
cd "$REPO_ROOT"

# Only distclean if there's something to clean. A stale `|| true` here would
# hide real failures (e.g. broken toolchain) on a fresh checkout.
if [ -f defs.mak ]; then
  emmake make distclean || true
fi
rm -f defs.mak database/database.h

# --- configure ---------------------------------------------------------------

# Strip Windows CRLF line endings (no-op on Linux-native files).
sed_strip_cr configure
find scripts/ -type f -print0 | while IFS= read -r -d '' f; do sed_strip_cr "$f"; done

if [ $OPT_RELEASE -eq 1 ]; then
  EXTRA_CFLAGS=" -O2"
else
  EXTRA_CFLAGS=" -g"
fi

CFLAGS="--std=c17 -D_DEFAULT_SOURCE=1 -DEMSCRIPTEN=1${EXTRA_CFLAGS}" \
  emconfigure ./configure \
    --without-cairo --without-opengl --without-x --without-tk --without-tcl \
    --disable-readline --disable-compression \
    --host=asmjs-unknown-emscripten \
    --target=asmjs-unknown-emscripten

cat toolchains/emscripten/defs.mak >> defs.mak

# --- build -------------------------------------------------------------------
emmake make depend
emmake make -j"$(ncpu)" modules libs
emmake make techs
emmake make mains

# --- copy artifacts ----------------------------------------------------------
cp magic/magic.js   "$SCRIPT_DIR/"
cp magic/magic.wasm "$SCRIPT_DIR/"
echo "Copied magic.js and magic.wasm to npm/"

# --- optional test -----------------------------------------------------------
# Runs the same smoke test that CI runs (see .github/workflows/main.yml).
if [ $OPT_TEST -eq 1 ]; then
  cd "$SCRIPT_DIR"
  npm run test
fi

# --- optional pack -----------------------------------------------------------
if [ $OPT_PACK -eq 1 ]; then
  "$SCRIPT_DIR/pack.sh"
  echo "npm package tarball created in npm/"
fi
