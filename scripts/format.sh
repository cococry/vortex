#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/src"

find "$SRC" \
  \( -name '*.c' -o -name '*.h' \) \
  -print0 |
  xargs -0 clang-format -i

echo "Formatted C files successfully."
