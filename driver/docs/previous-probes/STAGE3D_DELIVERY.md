# STAGE3D TXSNAPSHOT 0.13.0 — test exact

Dezarhivează pachetul nou în /Volumes/USB/A pe Acer. Kext-ul compilat și firmware-ul sunt deja incluse. Alex gestionează încărcarea/EFI și reboot-ul cu AppleVTD activ; scripturile de aici nu instalează și nu încarcă driverul.

## 1. Acer — verificare și recompilare, dacă dorești

```bash
cd /Volumes/USB/A/RealtekVTDNext-0.13.0-STAGE3D-TXSNAPSHOT
bash TEST_LOCAL.command
bash BUILD_DRIVER.command --fetch-sdk
```

Kext: build/Release/RealtekVTDNextEthernet.kext. După boot cu 0.13.0, verifică Ethernet activ, IPv4 192.168.10.105. Fă acest test imediat după boot, înainte de sleep.

## 2. iMac — receptor și captură

Folosește pachetul NETTRACE 0.1.0 existent pe iMac, același care a produs captura primită. Protocolul este compatibil. Oprește un receptor vechi cu Ctrl+C, dacă este încă deschis.

```bash
cd /Volumes/USB/A/RealtekVTDNext-STAGE3C-NETTRACE-0.1.0
sudo bash CAPTURE_IMAC.command
```

Așteaptă CAPTURE READY. Captura durează 90 de secunde. iMac trebuie să aibă adresa 192.168.10.10. Terminalul rămâne deschis.

## 3. Acer — test în cele 90 de secunde

```bash
cd /Volumes/USB/A/RealtekVTDNext-0.13.0-STAGE3D-TXSNAPSHOT
bash TEST_NETWORK.command
bash TEST_LAN.command --peer 192.168.10.10
bash COLLECT_LOCAL.command
```

Testul refuză altă versiune de driver și leagă traficul de interfața acestuia. Wi-Fi poate rămâne activ pentru conversație. Nu modifica MTU/MSS. După transfer, pe iMac poți apăsa Ctrl+C pentru a închide și arhiva captura mai devreme.

Trimite ambele arhive: stage3d-txsnapshot-*.tgz de pe Acer și stage3c-nettrace-*.tgz nouă de pe iMac. Dacă un test eșuează, rulează totuși colectarea pe Acer.

## Criteriul pasului următor

Comparăm checksum-ul și modelul datelor primite de iMac, retransmisiile, SHA256 și rata de upload. Dispariția coruperii cu revenirea upload-ului susține problema din calea TX directă eliminată, fără să dovedească singură cauza exactă. Dacă rămâne coruperea, continuăm cu maparea/ringul și placa, folosind aceeași probă. După stabilizarea TX tratăm separat sleep/wake. Testele locale nu pot confirma comportamentul pe hardware cu AppleVTD.
