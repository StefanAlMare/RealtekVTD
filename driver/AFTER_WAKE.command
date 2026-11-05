#!/bin/bash
set -uo pipefail
cd "$(dirname "$0")"
mkdir -p build/local-evidence
network_status=0
bash TEST_NETWORK.command > build/local-evidence/after-wake-network.txt 2>&1 || network_status=$?
cat build/local-evidence/after-wake-network.txt
power_status=0
python3 tools/power_check.py after || power_status=$?
bash COLLECT_LOCAL.command || exit 1
if [ "$network_status" -ne 0 ] || [ "$power_status" -ne 0 ]; then exit 1; fi
