#!/usr/bin/env bash
# Build Magic WASM and copy artifacts into this npm/ directory.
#
# Usage:
#   npm/build.sh [--variant=<tcl|notcl|both>] [--release] [--test] [--pack]
#
#   --variant=tcl    Build only the TCL-embedded variant       → npm/tcl/
#   --variant=notcl  Build only the plain (no Tcl/Tk) variant  → npm/notcl/
#   --variant=both   Build both (default)
#
#   --release   Omit debug symbols (-g) and build with -O2.
#   --test      Run `npm run test` after copying artifacts.
#   --pack      Run `npm pack` after copying artifacts (and tests, if given).
#
# Requirements (must be on PATH or set via env vars before running):
#   emcc / emmake / emconfigure  — Emscripten compiler tools
#   make, gcc                    — standard build tools
#   node, npm                    — only required for --test / --pack
#
# Environment:
#   EMSDK_DIR     Path to an activated emsdk checkout.
#                 If set, emsdk_env.sh is sourced from there.
#                 If unset, emcc must already be on PATH (e.g. sourced externally).
#   TCL_REF       git ref (tag/branch/SHA) of tcltk/tcl to build for the TCL
#                 variant. Default: main. (CI pins the latest stable tag.)
#   TCL_REPO_URL  git URL to clone tcltk/tcl from. Default: the upstream repo.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
# The TCL variant builds a static WASM Tcl from a pristine clone of tcltk/tcl.
# Both the clone (tcl/) and the out-of-source build artifacts live entirely
# under build-tcl-wasm/ (gitignored), so nothing outside it is ever touched.
TCL_BUILD_DIR="${TCL_BUILD_DIR:-$REPO_ROOT/build-tcl-wasm}"
TCL_SRC_DIR="$TCL_BUILD_DIR/tcl"
TCL_WASM_PREFIX="$TCL_BUILD_DIR/install"

OPT_RELEASE=0
OPT_TEST=0
OPT_PACK=0
OPT_VARIANT=both
for arg in "$@"; do
  case "$arg" in
    --release)        OPT_RELEASE=1 ;;
    --test)           OPT_TEST=1    ;;
    --pack)           OPT_PACK=1    ;;
    --variant=tcl)    OPT_VARIANT=tcl   ;;
    --variant=notcl)  OPT_VARIANT=notcl ;;
    --variant=both)   OPT_VARIANT=both  ;;
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

if [ $OPT_RELEASE -eq 1 ]; then
  EXTRA_CFLAGS="-O2"
else
  EXTRA_CFLAGS="-g"
fi

# --- TCL fork: clone and prebuild (TCL variant only) ------------------------
# Uses TCL_REPO_URL and TCL_REF from the environment (both have defaults).
# The source is cloned into build-tcl-wasm/tcl on the first run and checked
# out at the requested ref. Because this clone is private to the build dir,
# we manage its HEAD freely — no user-supplied tree is ever mutated.
#
# The actual WASM build runs out-of-source in $TCL_BUILD_DIR, driven by
# toolchains/emscripten/build-tcl-wasm.sh.
ensure_tcl_built() {
  : "${TCL_REPO_URL:=https://github.com/tcltk/tcl.git}"
  : "${TCL_REF:=main}"

  if [ ! -d "$TCL_SRC_DIR/.git" ]; then
    echo "=== cloning $TCL_REPO_URL into $TCL_SRC_DIR ==="
    mkdir -p "$TCL_BUILD_DIR"
    git -c core.autocrlf=false clone "$TCL_REPO_URL" "$TCL_SRC_DIR"
  fi

  ( cd "$TCL_SRC_DIR"
    current_sha=$(git rev-parse HEAD 2>/dev/null || echo "")
    if [ "$current_sha" != "$TCL_REF" ]; then
      git fetch --quiet origin
      git checkout --quiet --detach "$TCL_REF"
    fi
    echo "Using TCL at $(git rev-parse HEAD) ($TCL_REPO_URL)"
  )

  # Build TCL for WASM if it hasn't been built yet. The presence of
  # tclConfig.sh in the install prefix is the canonical "TCL is built" marker.
  if [ ! -f "$TCL_WASM_PREFIX/lib/tclConfig.sh" ]; then
    echo "=== building TCL for WASM into $TCL_BUILD_DIR (one-time) ==="
    bash "$REPO_ROOT/toolchains/emscripten/build-tcl-wasm.sh" \
      --src="$TCL_SRC_DIR" --out="$TCL_BUILD_DIR"
  fi
}

# --- build a single variant --------------------------------------------------
# Each variant gets a fresh configure run because the two configurations
# select different code paths (MAGIC_WRAPPER on/off, MAGIC_NO_TK, link flags),
# so the object cache from one variant is not compatible with the other.
build_variant() {
  local variant=$1
  local out_dir="$SCRIPT_DIR/$variant"

  echo
  echo "==============================================================="
  echo "=== building variant: $variant"
  echo "==============================================================="

  cd "$REPO_ROOT"

  # Full clean — distclean removes the generated defs.mak and module objects.
  if [ -f defs.mak ]; then
    emmake make distclean || true
  fi
  rm -f defs.mak database/database.h

  # Strip Windows CRLF line endings (no-op on Linux-native files).
  sed_strip_cr configure
  find scripts/ -type f -print0 | while IFS= read -r -d '' f; do sed_strip_cr "$f"; done

  if [ "$variant" = "tcl" ]; then
    ensure_tcl_built
    CFLAGS="--std=c17 -D_DEFAULT_SOURCE=1 -DEMSCRIPTEN=1 ${EXTRA_CFLAGS}" \
      emconfigure ./configure \
        --without-cairo --without-opengl --without-x --without-tk \
        --with-tcl="$TCL_WASM_PREFIX/lib" \
        --with-tclincls="$TCL_WASM_PREFIX/include" \
        --with-tcllibs="$TCL_WASM_PREFIX/lib" \
        --disable-readline --disable-compression \
        --host=asmjs-unknown-emscripten \
        --target=asmjs-unknown-emscripten
  else
    CFLAGS="--std=c17 -D_DEFAULT_SOURCE=1 -DEMSCRIPTEN=1 ${EXTRA_CFLAGS}" \
      emconfigure ./configure \
        --without-cairo --without-opengl --without-x \
        --without-tk --without-tcl \
        --disable-readline --disable-compression \
        --host=asmjs-unknown-emscripten \
        --target=asmjs-unknown-emscripten
  fi

  cat toolchains/emscripten/defs.mak >> defs.mak

  emmake make depend
  emmake make -j"$(ncpu)" modules libs
  emmake make techs
  emmake make mains

  mkdir -p "$out_dir"
  cp magic/magic.js   "$out_dir/"
  cp magic/magic.wasm "$out_dir/"
  echo "Copied magic.js + magic.wasm into npm/$variant/"
}

case "$OPT_VARIANT" in
  tcl|notcl) build_variant "$OPT_VARIANT" ;;
  both)      build_variant notcl
             build_variant tcl ;;
esac

# --- optional test -----------------------------------------------------------
# Runs the same smoke test that CI runs (see .github/workflows/main-wasm.yml).
if [ $OPT_TEST -eq 1 ]; then
  cd "$SCRIPT_DIR"
  npm run test
  npm run test:tcl
fi

# --- optional pack -----------------------------------------------------------
if [ $OPT_PACK -eq 1 ]; then
  "$SCRIPT_DIR/pack.sh"
  echo "npm package tarball created in npm/"
fi
