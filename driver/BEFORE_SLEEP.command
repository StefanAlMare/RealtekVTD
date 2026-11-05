#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build/local-evidence
if ! bash TEST_NETWORK.command > build/local-evidence/before-sleep-network.txt 2>&1; then
 cat build/local-evidence/before-sleep-network.txt
 bash COLLECT_LOCAL.command
 exit 1
fi
cat build/local-evidence/before-sleep-network.txt
python3 tools/power_check.py before
echo 'Baseline recorded. Now sleep manually for at least 60 seconds, wake, wait 30 seconds, then run AFTER_WAKE.command.'
