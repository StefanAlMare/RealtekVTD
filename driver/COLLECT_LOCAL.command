#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
stamp="$(date +%Y%m%d-%H%M%S)"
out="build/local-evidence/media1-$stamp"
mkdir -p "$out"
{ date; sw_vers; uname -a; /usr/sbin/sysctl kern.bootargs 2>/dev/null || true; } > "$out/00_SYSTEM.txt"
/usr/bin/kmutil showloaded > "$out/01_LOADED.txt" 2>&1 || true
/usr/sbin/ioreg -lw0 -r -c AppleVTD > "$out/02_APPLEVTD.txt" 2>&1 || true
/usr/sbin/ioreg -lw0 -r -c AppleVTDDeviceMapper > "$out/03_MAPPERS.txt" 2>&1 || true
/usr/sbin/ioreg -lw0 -r -c IOPCIDevice > "$out/04_PCI.txt" 2>&1 || true
/usr/sbin/ioreg -lw0 -r -c RealtekVTDNextEthernet > "$out/05_ETHERNET.txt" 2>&1 || true
/usr/sbin/ioreg -lw0 -r -c RealtekVTDNextEthernet 2>/dev/null |
 /usr/bin/grep -E '"(Pause[^" ]*|Media1[^" ]*|PHYDiag[^" ]*|PHYAutoneg[^" ]*|PHYCapabilities[^" ]*|ChipProfile|PCIRevision|PCISubsystemVendor|PCISubsystemDevice|XID|MACVersion|IOLinkSpeed|IOLinkStatus|DriverVersion|DMAMode|Stage3[BCDEFG][^" ]*|Stage4[ABD][^" ]*|DeviceMapperPresent|Provider[^" ]*|Stage1Complete|FirmwareApplied|Probe[^" ]*|TXRingIOVA64|RXRingIOVA64|BusMasterEnabled|ChipCommand|PHYLink|EEEAdvertisement|IOBSDName)"' > "$out/06_STAGE3C_PROPERTIES.txt" || true
{ /sbin/ifconfig -a; /usr/sbin/networksetup -listallhardwareports; /usr/sbin/netstat -rn -f inet; /usr/sbin/scutil --nwi; } > "$out/07_NETWORK.txt" 2>&1 || true
{ /sbin/ifconfig en0; /usr/sbin/networksetup -getMedia en0; /sbin/route -n get default; } > "$out/07_EN0_MEDIA_ROUTE.txt" 2>&1 || true
/usr/bin/log show --last boot --style compact --predicate 'process == "kernel"' 2>/dev/null |
 /usr/bin/grep -Ei 'RealtekVTDNext|RealtekRTL8111|AppleVTD|IOMMU|DMAR|DMA fault|translation fault' > "$out/08_KERNEL_LOG.txt" || true
# Preserve the latest explicit test result, if the user ran TEST_NETWORK.command.
if [ -f build/local-evidence/network-test-latest.txt ]; then cp build/local-evidence/network-test-latest.txt "$out/09_NETWORK_TEST.txt"; fi
for result in media1-reporting-latest.json autoneg-acceptance-latest.json power-before.json power-after.json before-sleep-network.txt after-wake-network.txt idle-test-latest.txt lan-test-latest.txt lan-server-latest.txt tcp-before.txt tcp-after.txt; do
 if [ -f "build/local-evidence/$result" ]; then cp "build/local-evidence/$result" "$out/$result"; fi
done
/usr/sbin/netstat -s -p tcp > "$out/10_TCP_STATS.txt" 2>&1 || true
echo "RealtekVTDNext 0.20.3-MEDIA1" > "$out/11_TOOLS_VERSION.txt"
/usr/bin/shasum -a 256 tools/registry.py tools/network_test.py tools/lan_throughput.py >> "$out/11_TOOLS_VERSION.txt"
archive="${out}.tgz"
/usr/bin/tar -czf "$archive" -C "$(dirname "$out")" "$(basename "$out")"
echo "Evidence archive: $archive"
/usr/bin/shasum -a 256 "$archive"
echo 'Read-only collection; no kext, EFI, NVRAM or network configuration changes.'
