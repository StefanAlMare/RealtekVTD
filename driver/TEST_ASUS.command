#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
echo 'Read-only Asus acceptance for an already booted 0.20.4 driver. No installation or network configuration changes.'
status=0
bash TEST_NETWORK.command "$@" || status=$?
python3 tools/autoneg_acceptance.py --machine asus || status=1
bash COLLECT_LOCAL.command || status=1
exit "$status"
