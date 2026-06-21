#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/../.."

CXX_BIN="${CXX:-c++}"
OUT_DIR="${TMPDIR:-/tmp}/paperscreen-reader-tests"

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
  -o "$OUT_DIR/inflate_tests"

"$CXX_BIN" \
  -std=c++20 \
  -Wall \
  -Wextra \
  -Werror \
  -I. \
  -Isrc \
  tests/reader/zip_reader_tests.cpp \
  src/apps/reader/format/zip_reader.cpp \
  src/apps/reader/format/inflate.cpp \
  -o "$OUT_DIR/zip_reader_tests"

"$CXX_BIN" \
  -std=c++20 \
  -Wall \
  -Wextra \
  -Werror \
  -I. \
  -Isrc \
  tests/reader/xml_document_tests.cpp \
  src/apps/reader/format/xml_document.cpp \
  src/apps/reader/format/zip_reader.cpp \
  src/apps/reader/format/inflate.cpp \
  -o "$OUT_DIR/xml_document_tests"

"$OUT_DIR/inflate_tests"
"$OUT_DIR/zip_reader_tests"
"$OUT_DIR/xml_document_tests"
