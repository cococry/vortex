#!/usr/bin/env bash
# Compare the same Wayland workload in dwl and Vortex; see --help for examples.
set -euo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
export PYTHONDONTWRITEBYTECODE=1
exec python3 "$SCRIPT_DIR/profile_compare.py" "$@"
