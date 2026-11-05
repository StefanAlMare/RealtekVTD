#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
[ "$#" -eq 1 ] || { echo 'Usage: MAC_KERNEL_SDK=/verified/local/SDK bash BUILD_JTX1_LOCAL.command /exact/EXP1-BAZA_COMPLETA.zip'; exit 2; }
[ -n "${MAC_KERNEL_SDK:-}" ] || { echo 'Verified local SDK required; no downloads.'; exit 2; }
python3 -B tools/verify_media1_sdk.py --sdk "$MAC_KERNEL_SDK"
python3 -B tools/verify_jtx1_scope.py --baseline-zip "$1" --report build/jtx1-scope.json
export SDKROOT="$(xcrun --sdk macosx --show-sdk-path)"
SANITIZE=1 bash TEST_LOCAL.command > build/jtx1-tests.log 2>&1
bash BUILD_DRIVER.command
echo 'JTX1 diagnostic build only. Jumbo fix NOT implemented; hardware NOT TESTED; no installation.'
