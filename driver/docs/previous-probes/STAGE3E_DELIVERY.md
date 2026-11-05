# STAGE3E TXPOOL 0.14.0 — pasul următor

Extrage ZIP-ul în /Volumes/USB/A pe Acer. Include firmware și build/Release/RealtekVTDNextEthernet.kext compilat. Alex gestionează încărcarea/EFI și boot-ul cu AppleVTD activ. Scripturile nu instalează și nu încarcă kext-ul.

## Dacă dorești recompilare pe Acer

```bash
cd /Volumes/USB/A/RealtekVTDNext-0.14.0-STAGE3E-TXPOOL
bash TEST_LOCAL.command
bash BUILD_DRIVER.command --fetch-sdk
```

## După boot cu 0.14.0 — pe iMac

Oprește receptorul vechi cu Ctrl+C, dacă încă rulează. Folosește pachetul NETTRACE existent, compatibil cu protocolul acestei versiuni:

```bash
cd /Volumes/USB/A/RealtekVTDNext-STAGE3C-NETTRACE-0.1.0
sudo bash CAPTURE_IMAC.command
```

Așteaptă CAPTURE READY. Receptorul este 192.168.10.10; captura durează 90 secunde.

## După CAPTURE READY — pe Acer

```bash
cd /Volumes/USB/A/RealtekVTDNext-0.14.0-STAGE3E-TXPOOL
bash TEST_NETWORK.command
bash TEST_LAN.command --peer 192.168.10.10 --mib 32
bash COLLECT_LOCAL.command
```

După transfer apasă Ctrl+C pe iMac și trimite arhiva stage3c-nettrace-*.tgz nouă și stage3e-txpool-*.tgz de pe Acer. Wi-Fi poate rămâne activ. Nu schimba MTU/MSS și nu intra în sleep înaintea testului.

## Criterii

Confirmăm DriverVersion 0.14.0, DeviceMapperPresent și pool-ul de 65 de sloturi. Verificăm SHA256 și modelul complet al upload-ului pe iMac, checksum-urile pachetelor de intrare, suprapunerile TCP și contoarele TX. Stage3CTXMapCalls/MapAverageNS măsoară acum rezervarea și copia, nu alocarea/maparea efectivă IOMMU; Stage3ETXPoolIOVA64 arată maparea persistentă. Comparăm rata cu 306,32/917,20 Mbit/s din 0.13.0. În caz de corupere sau regresie, păstrăm 0.13.0 ca versiune validată pentru aceste teste. Sleep/wake rămâne un pas separat.
