#!/usr/bin/env bash
# Default workload. Supply your own script to profile a sequence of applications.
set -euo pipefail
exec weston-simple-dmabuf-egl -s 256 -e 0
