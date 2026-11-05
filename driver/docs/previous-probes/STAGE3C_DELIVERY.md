# STAGE3C TXAUDIT 0.12.0 — diagnosticarea upload-ului

Captura din 4 octombrie, 03:56 confirmă DHCP/ping pe Ethernet: 10/10 răspunsuri, medie 0,484 ms. Testul între Acer 192.168.10.105 și iMac 192.168.10.10 a măsurat 2,71 Mbit/s upload și 913,58 Mbit/s download. Cele 8 MiB din fiecare sens au trecut verificarea SHA256. Integritatea streamului TCP nu exclude pierderi de pachete reparate prin retransmisii.

Cererea TCP_MAXSEG pentru MSS 536 a fost respinsă de macOS cu EINVAL. Testul acestei versiuni rulează numai transferul normal, fără schimbarea MTU sau configurării TCP. Statisticile TCP din captura anterioară sunt integral zero; nu le interpretăm ca dovadă că nu există retransmisii.

## Ce adaugă driverul

Calea de transmisie rămâne aceeași. În 0.11.0, pachetele refuzate înainte de publicarea descriptorului nu erau contabilizate în Stage3BTXErrors. Prin urmare TXErrors=0 nu exclude asemenea drop-uri.

Noua versiune publică Stage3C*: attempts, accepted, bytes, drop-uri separate (inactive/size/padding/mapping/submit), ring-full și gated retries. Mapările sunt temporizate: medie/maxim în nanosecunde și număr de apeluri peste 1 ms. Sunt expuse codurile DMAStatus 0..15, numărul de segmente, maximul de mbuf-uri în lanț, lungimea maximă și cererile de checksum încă prezente în mbuf. Cererile sunt doar observate; nu se activează offload și nu se schimbă conținutul pachetelor.

Nu declarăm o cauză precisă sau o remediere de performanță înainte de această captură. Baseline-ul 0.11.0 și arhivele istorice rămân intacte. Noile contoare sunt protejate de același lock; maparea rămâne în afara gate-ului, iar timerul nu colectează completări.

## Kext / compilare

Kext inclus: `build/Release/RealtekVTDNextEthernet.kext`, versiune 0.12.0. Firmware inclus. Dacă compilezi pe Acer:

```bash
cd /Volumes/USB/A/RealtekVTDNext-0.12.0-STAGE3C-TXAUDIT
bash TEST_LOCAL.command
bash BUILD_DRIVER.command --fetch-sdk
```

Alex gestionează încărcarea/EFI. Bootează cu noul kext și AppleVTD activ, un singur driver pentru dispozitiv. Nu trebuie schimbate setările MTU/DHCP. Menține Ethernet-ul activ pe durata testului, apoi îl poți dezactiva pentru conversația prin Wi-Fi.

## Test exact

Pe iMac, folosește receptorul deja extras în NETDIAG 0.1.1; protocolul este compatibil:

```bash
cd /Volumes/USB/A/RealtekVTDNext-STAGE3B-NETDIAG-0.1.1
python3 tools/lan_throughput.py server --listen 192.168.10.10
```

Lasă terminalul deschis. După „Receiver ready”, pe Acer:

```bash
cd /Volumes/USB/A/RealtekVTDNext-0.12.0-STAGE3C-TXAUDIT
bash TEST_NETWORK.command
bash TEST_LAN.command --peer 192.168.10.10
bash COLLECT_LOCAL.command
```

Trimite arhiva `stage3c-txaudit-*.tgz` indicată de colectare, chiar dacă testul eșuează. Colectarea include noile contoare și rezultatul transferului. Scriptul refuză versiunea veche a driverului pentru a evita o măsurătoare fără audit.

## Cum alegem pasul următor

- DropMapping și codurile DMAStatus indică unde trebuie corectată maparea/lungimea/segmentarea.
- DropSize și LengthMax arată dacă sistemul oferă cadre pe care calea curentă le refuză.
- MapAverageNS/MaxNS/Over1MS arată dacă pregătirea mapărilor poate limita rata de transmisie.
- PendingChecksum/ChecksumFlags arată dacă ajung cereri nerezolvate de checksum la calea fără offload.
- RingFull/GatedRetry și completed versus accepted separă presiunea cozii de refuzurile mapării.

Dacă acestea nu explică limitarea, următorul test va fi captura de cadre/ACK pe iMac pentru retransmisii și ritmul descriptorilor. Nu aplicăm optimizări speculative în același build cu măsurătoarea de bază.
