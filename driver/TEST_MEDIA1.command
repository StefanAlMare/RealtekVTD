#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build/local-evidence
status=0
python3 tools/media1_audit.py "$@" --output build/local-evidence/media1-reporting-latest.json || status=$?
bash COLLECT_LOCAL.command
exit "$status"
