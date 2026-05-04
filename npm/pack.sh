#!/usr/bin/env bash
# Pack the magic-vlsi-wasm npm tarball with reproducible-but-current timestamps.
#
# npm/pacote normalizes file mtimes for reproducibility and, when no
# SOURCE_DATE_EPOCH is set, falls back to 1985-10-26. That makes published
# tarballs carry 1985 dates, which confuses users and tooling.
#
# This script:
#   1. touches every file in npm/ so mtimes reflect the build time
#   2. sets SOURCE_DATE_EPOCH to `now` so pacote's normalization uses it
#   3. runs `npm pack`, producing magic-vlsi-wasm-<version>.tgz in npm/
#
# Used by:
#   - npm/build.sh --pack  (local build)
#   - .github/workflows/main.yml        (CI artifact upload)
#   - .github/workflows/npm-publish.yml (tag-triggered publish)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

find . -exec touch {} +
SOURCE_DATE_EPOCH=$(date +%s) npm pack
