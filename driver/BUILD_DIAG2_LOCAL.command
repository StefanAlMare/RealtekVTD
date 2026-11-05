#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
[ "$#" -eq 1 ] || { echo 'Usage: MAC_KERNEL_SDK=/verified/local/SDK bash BUILD_DIAG2_LOCAL.command /canonical/DIAG1-BAZA.zip'; exit 2; }
[ -n "${MAC_KERNEL_SDK:-}" ] || { echo 'Verified local SDK required; network downloads are disabled.'; exit 2; }
mkdir -p build
python3 -B tools/verify_media1_sdk.py --sdk "$MAC_KERNEL_SDK"
python3 -B tools/verify_diag2_scope.py --baseline-zip "$1" --report build/diag2-scope.json
# Inherited host tests invoke clang++ directly; make the Xcode host SDK explicit.
# Kernel compilation still uses the unchanged -nostdinc / MacKernelSDK flags.
export SDKROOT="$(xcrun --sdk macosx --show-sdk-path)"
SANITIZE=1 bash TEST_LOCAL.command > build/diag2-tests.log 2>&1
bash BUILD_DRIVER.command
echo 'DIAG2 only: RX handoff observations. RX fix, VLAN and Jumbo are not implemented. No installation.'
