#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
echo 'Run only after Asus acceptance passes. Driver must already be booted.'
status=0
bash TEST_NETWORK.command "$@" || status=$?
python3 tools/autoneg_acceptance.py --machine acer || status=1
bash COLLECT_LOCAL.command || status=1
exit "$status"
