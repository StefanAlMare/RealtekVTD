#!/bin/bash
set -euo pipefail
# Cumulative MAC46 N64 TXPRECHECK-B experimental candidate. No Git, network or installation.
task_root="$(cd -- "$(dirname -- "$0")/.." && pwd)"
exec python3 -B "$task_root/build_gu24.py" "$@"
