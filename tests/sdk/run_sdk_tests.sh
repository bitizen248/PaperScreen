#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/../.."

CXX_BIN="${CXX:-c++}"
OUT_DIR="${TMPDIR:-/tmp}/paperscreen-sdk-tests"
OUT_BIN="$OUT_DIR/sdk_tests"

mkdir -p "$OUT_DIR"

"$CXX_BIN" \
  -std=c++20 \
  -Wall \
  -Wextra \
  -Werror \
  -Isrc \
  tests/sdk/sdk_tests.cpp \
  src/apps/trmnl/trmnl_app.cpp \
  src/apps/trmnl/trmnl_cache.cpp \
  src/apps/trmnl/trmnl_settings_adapter.cpp \
  src/apps/trmnl/trmnl_view.cpp \
  src/sdk/app_registry.cpp \
  -o "$OUT_BIN"

"$OUT_BIN"
