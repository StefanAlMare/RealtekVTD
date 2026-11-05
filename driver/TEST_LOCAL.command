#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build/tests
flags=(-std=c++14 -Wall -Wextra -Werror -g)
if [ "${SANITIZE:-0}" = 1 ]; then flags+=(-fsanitize=address,undefined); fi
core=(src/RegisterAccess.cpp src/FirmwareProgram.cpp src/PhyInit.cpp src/CopperAutoneg.cpp src/HardwareInit.cpp src/ProbeProgram.cpp src/DMAProgram.cpp src/Stage2BProgram.cpp src/Stage2CProgram.cpp src/Stage2DProgram.cpp src/Stage2EProgram.cpp src/Stage2FProgram.cpp src/Stage2GProgram.cpp src/Stage3AProgram.cpp src/NetworkProgram.cpp src/TXSnapshot.cpp src/TXPool.cpp)
for test in autoneg phydiag chip_identity register_access stage1 dma_stage2a stage2b stage2c stage2d stage2e stage2f stage2g stage3a network txaudit txsnapshot txpool irqaudit interrupt_selection power_lifecycle dma_mode jumbo mtu_transaction jtx_trace; do
 clang++ "${flags[@]}" "tests/$test.cpp" "${core[@]}" -o "build/tests/$test"
 "build/tests/$test"
done
python3 tools/verify_network_policy.py
python3 tests/network_test_tool.py
python3 tests/lan_protocol.py
python3 tests/nettrace_analysis.py
python3 tests/autoneg_acceptance_tool.py

xcrun clang++ -std=c++14 -Wall -Wextra -Werror -g tests/pause_observation.cpp -o build/tests/pause_observation
build/tests/pause_observation
python3 tests/media1_audit_tool.py

xcrun clang++ -std=c++14 -Wall -Wextra -Werror -g -fsanitize=address,undefined tests/multicast_filter.cpp -o build/tests/multicast_filter
build/tests/multicast_filter

xcrun clang++ -std=c++14 -Wall -Wextra -Werror -g -fsanitize=address,undefined tests/rx_handoff_diagnostics.cpp -o build/tests/rx_handoff_diagnostics
build/tests/rx_handoff_diagnostics

build/tests/jtx_trace build/tests/jtx1-fixture.bin
python3 -B tests/jtx_decode.py build/tests/jtx1-fixture.bin
