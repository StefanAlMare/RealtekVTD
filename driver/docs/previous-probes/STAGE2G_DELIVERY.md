# RealtekVTDNext 0.9.0 STAGE2G TXRECYCLE

Baseline 0.8.0 păstrat. Captura `stage2f-duplex-20261004-013513.tgz` a validat 8 TX, 16 RX și 12 rearmări RX cu AppleVTD; fără pcap, integritatea TX pe cablu rămâne nevalidată.

## Schimbarea

Opt descriptori TX și opt sloturi de câte 60 octeți din același buffer ToDevice de 480 octeți sunt refolosiți pentru 32 transmisii. Fiecare descriptor este completat de patru ori și rearmat de trei ori: 24 rearmări TX. Adresa IOVA a fiecărui slot rămâne aceeași. RingEnd rămâne numai pe descriptorul 7.

După sincronizarea ringului pentru CPU, codul verifică întoarcerea OWN înainte de a modifica payload-ul acelui slot și înainte de a publica din nou descriptorul. Dacă slotul următor este încă deținut de device, emiterea așteaptă; nu suprascrie descriptorul. Cadrele au marker RealtekVTDNext-STAGE2G și secvențe 0..31 (big endian la offset 40).

RX rulează simultan: patru descriptori, 16 completări fiecare, 64 recepții și 60 rearmări. Validarea RX este cea a descriptorilor/lungimilor, fără verificarea completă a payload-urilor. Fereastra maximă rămâne 30 secunde, precedată de așteptare limitată a linkului. Sesiunea pornește direct după STAGE1/2A; B/C/D/E/F standalone nu rulează.

Calea de oprire și reținere a mapărilor pe erori rămâne cea din STAGE2F. Nu există încă interfață Ethernet macOS, interrupturi sau trafic permanent.

## Test exact

Pe ALT calculator din aceeași rețea Ethernet pornește captura, folosind interfața reală:

```bash
sudo tcpdump -i INTERFATA_ETHERNET -s 0 -w stage2g-tx.pcap 'ether proto 0x88b5'
```

În alt terminal al aceluiași calculator pornește emitătorul cu adresele reale:

```bash
python3 tools/send_rx_test.py --source IP_EMITATOR --broadcast BROADCAST_RETEA --seconds 600
```

Repornește imediat Acerul cu 0.9.0 și AppleVTD activ. După login, din directorul pachetului de pe Acer:

```bash
bash COLLECT_LOCAL.command
```

Oprește captura și emițătorul cu Ctrl+C. Pe celălalt calculator, din directorul pachetului 0.9.0:

```bash
python3 tools/verify_tx_capture.py stage2g-tx.pcap
```

Trimite .tgz, pcap-ul și rezultatul emitătorului. Captura este importantă pentru a confirma cele 32 de secvențe și conținutul TX pe cablu. Alex gestionează EFI și încărcarea.

## Criterii

Stage2GStatus=0, Complete=true, TxSubmitted=TxCompleted=32, TxRearmed=24, TxDesc0..7Completions=4 fiecare; RxCompleted=64, RxRearmed=60; fără timeout, TxErrorBits=0 și ChipCommandAfter=0. TxCompletionMask conține toate cele 32 biți (0xffffffff; afișarea IORegistry poate avea extensie de semn).

Verificatorul pcap trebuie să confirme toate cele 32 cadre cu conținut exact. Completările DMA în Acer nu demonstrează singure livrarea pe cablu.

## Build

```bash
bash TEST_LOCAL.command
FIRMWARE_DIR="$PWD/firmware" bash BUILD_PROBE.command --fetch-sdk
```

Kext: build/Release/RealtekVTDNextProbe.kext. Firmware inclus. Hardware 0.9.0 încă netestat.

## Validare locală

Toate cele zece suite au trecut. Modelul TX a verificat 32 de secvențe unice, aceleași IOVA pe slot, RingEnd doar pe descriptorul 7 și patru completări per descriptor, inclusiv completări întârziate care umplu ringul și forțează așteptarea. Timeout-urile și reținerea sigură a mapărilor sunt testate. Verificatorul pcap acceptă 32 cadre corecte și respinge un payload corupt. Buildul cu firmware a trecut. Niciun kext nu a fost încărcat în această sesiune.
