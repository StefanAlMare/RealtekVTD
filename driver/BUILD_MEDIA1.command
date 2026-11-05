#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
[ "$#" = 0 ] || { echo 'No arguments accepted; network fetch is not available through this entry point.'; exit 1; }
: "${MEDIA1_BASELINE_ZIP:?Set MEDIA1_BASELINE_ZIP to the original verified 0.20.2-AUTONEG.zip}"
: "${MAC_KERNEL_SDK:?Set MAC_KERNEL_SDK to the exact existing local pinned SDK}"
mkdir -p build
python3 tools/verify_media1_sdk.py --sdk "$MAC_KERNEL_SDK"
python3 tools/verify_media1_scope.py --baseline-zip "$MEDIA1_BASELINE_ZIP" --report build/media1-scope.json
bash TEST_LOCAL.command > build/media1-test.log 2>&1
bash BUILD_DRIVER.command > build/media1-build.log 2>&1
python3 - <<'PY'
from pathlib import Path
import hashlib
p=Path('build/Release/RealtekVTDNextEthernet.kext/Contents/MacOS/RealtekVTDNextEthernet')
Path('EXECUTABLE_SHA256.txt').write_text(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.as_posix()+'\n')
PY
echo 'PASS: local gated build. Hardware acceptance remains pending; no deployment performed.'
