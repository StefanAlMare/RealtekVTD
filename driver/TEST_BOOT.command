#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
echo 'Boot check 0.20.4: wait 60 seconds; do not sleep or disable/enable Ethernet.'
sleep 60
status=0
bash TEST_NETWORK.command || status=$?
bash COLLECT_LOCAL.command
exit "$status"
