#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/../.."

CXX_BIN="${CXX:-c++}"
OUT_DIR="${TMPDIR:-/tmp}/paperscreen-reader-tests"
OUT_BIN="$OUT_DIR/reader_tests"

mkdir -p "$OUT_DIR"

"$CXX_BIN" \
  -std=c++20 \
  -Wall \
  -Wextra \
  -Werror \
  -I. \
  -Isrc \
  tests/reader/inflate_tests.cpp \
  src/apps/reader/format/inflate.cpp \
  -o "$OUT_BIN"

"$OUT_BIN"
