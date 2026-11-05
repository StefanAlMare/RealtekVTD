# STAGE3F IRQAUDIT 0.15.0 — test exact

Extrage ZIP-ul în /Volumes/USB/A pe Acer. Include firmware și build/Release/RealtekVTDNextEthernet.kext compilat. Alex gestionează încărcarea/EFI și reboot-ul cu AppleVTD activ. Instrumentele nu instalează sau încarcă kext-ul.

## Recompilare opțională pe Acer

```bash
cd /Volumes/USB/A/RealtekVTDNext-0.15.0-STAGE3F-IRQAUDIT
bash TEST_LOCAL.command
bash BUILD_DRIVER.command --fetch-sdk
```

## 1. După boot — observație pe Acer

Ethernet activ, fără speedtest/YouTube/transfer pornit în aceste 10 secunde; traficul de fundal va fi contabilizat.

```bash
cd /Volumes/USB/A/RealtekVTDNext-0.15.0-STAGE3F-IRQAUDIT
bash TEST_IDLE.command
```

Nu intra în sleep înainte de test. TEST_IDLE nu trimite pachete și nu schimbă configurația rețelei.

## 2. Pe iMac — receptor și captură

```bash
cd /Volumes/USB/A/RealtekVTDNext-STAGE3C-NETTRACE-0.1.0
sudo bash CAPTURE_IMAC.command
```

Oprește receptorul vechi cu Ctrl+C dacă este încă deschis. Așteaptă CAPTURE READY. iMac are 192.168.10.10, Acer 192.168.10.105.

## 3. După CAPTURE READY — pe Acer

```bash
cd /Volumes/USB/A/RealtekVTDNext-0.15.0-STAGE3F-IRQAUDIT
bash TEST_NETWORK.command
bash TEST_LAN.command --peer 192.168.10.10 --mib 32
bash COLLECT_LOCAL.command
```

După test apasă Ctrl+C pe iMac; trimite arhivele noi stage3f-irqaudit-*.tgz de pe Acer și stage3c-nettrace-*.tgz de pe iMac. Colectarea include idle-test-latest.txt. Wi-Fi poate rămâne activ; nu modifica MTU/MSS.

Vom identifica RxOK bit0, RxErr bit1, TxOK bit2, TxErr bit3, callbacks fără cauze și multiplicarea prin bucla de opt treceri. Comparăm faza liniștită cu progresul TX/RX și transferul. Numărătoarea diagnostică poate adăuga un cost mic; nu pretindem că 0.15.0 crește viteza.
