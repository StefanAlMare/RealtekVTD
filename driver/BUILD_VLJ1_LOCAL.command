#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
[ "$#" -eq 1 ] || { echo 'Usage: MAC_KERNEL_SDK=/verified/local/SDK bash BUILD_VLJ1_LOCAL.command /exact/RealtekVTDNext-0.20.5-PREP-DIAG2-BAZA.zip'; exit 2; }
[ -n "${MAC_KERNEL_SDK:-}" ] || { echo 'Verified local SDK required; no downloads.'; exit 2; }
python3 -B tools/verify_media1_sdk.py --sdk "$MAC_KERNEL_SDK"
python3 -B tools/verify_vlj1_scope.py --baseline-zip "$1" --report build/vlj1-scope.json
export SDKROOT="$(xcrun --sdk macosx --show-sdk-path)"
SANITIZE=1 bash TEST_LOCAL.command > build/vlj1-tests.log 2>&1
bash BUILD_DRIVER.command
echo 'Experimental local build only. RXSTAT1 deferred. No installation.'
