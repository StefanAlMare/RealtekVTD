# STAGE3G MSI 0.16.0 — test exact

Extrage în /Volumes/USB/A pe Acer. Kext compilat cu firmware: build/Release/RealtekVTDNextEthernet.kext. Alex gestionează încărcarea/EFI și boot-ul cu AppleVTD activ. Scripturile nu instalează sau încarcă kext-ul.

Recompilare opțională pe Acer:

```bash
cd /Volumes/USB/A/RealtekVTDNext-0.16.0-STAGE3G-MSI
bash TEST_LOCAL.command
bash BUILD_DRIVER.command --fetch-sdk
```

După boot, Ethernet activ, fără transfer pornit în cele 10 secunde:

```bash
cd /Volumes/USB/A/RealtekVTDNext-0.16.0-STAGE3G-MSI
bash TEST_IDLE.command
```

Pe iMac, oprește receptorul vechi dacă încă rulează, apoi:

```bash
cd /Volumes/USB/A/RealtekVTDNext-STAGE3C-NETTRACE-0.1.0
sudo bash CAPTURE_IMAC.command
```

După CAPTURE READY, pe Acer:

```bash
cd /Volumes/USB/A/RealtekVTDNext-0.16.0-STAGE3G-MSI
bash TEST_NETWORK.command
bash TEST_LAN.command --peer 192.168.10.10 --mib 32
bash COLLECT_LOCAL.command
```

După transfer apasă Ctrl+C pe iMac și trimite arhivele noi stage3g-msi-*.tgz și stage3c-nettrace-*.tgz. Wi-Fi poate rămâne activ. Nu schimba MTU și nu intra în sleep înaintea testului.

Dacă Ethernet nu apare, rulează COLLECT_LOCAL.command oricum și trimite arhiva; nu există fallback la index0 în acest experiment. Vom verifica Stage3GMessageInterruptSelected, indexul, Stage3BInterruptType cu flag messaged, filtre unqualified în observație și integritatea transferului. Comparăm cu 64.143 filtre unqualified în 10 secunde. Reducerea lor trebuie observată, nu presupusă. Sleep/wake rămâne separat.
