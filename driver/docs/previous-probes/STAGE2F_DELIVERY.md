# RealtekVTDNext 0.8.0 STAGE2F DUPLEX

Baseline 0.7.2 este păstrat. Captura `stage2e-samerx-20261004-005907.tgz` a validat 8 recepții, 4 rearmări și două runde RX cu AppleVTD. Repornirea separată D→E rămâne nevalidată.

## Secvența nouă

STAGE1 → STAGE2A → așteptare link (maximum 60 verificări la 100 ms) → o singură sesiune TX+RX (`ChipCommand=12`). B/C/D/E standalone sunt omise intenționat și rămân Complete=false. Rezultatul curent este STAGE2F, cu RunMode=direct-from-a-single-duplex-session.

TX: opt cadre Ethernet de 60 octeți către broadcast, EtherType 0x88b5, marker RealtekVTDNext-STAGE2F și secvențe 0..7 la offsetul 40, big endian. Un buffer de 480 octeți ToDevice este mapat prin mapper-ul dispozitivului și împărțit în opt cadre; nu este un fallback/bounce. Opt descriptori TX sunt publicați treptat, la circa 100 ms; RingEnd este numai pe ultimul. Completarea este verificată prin OWN, iar IntrStatus TxErr este urmărit după ștergerea stării vechi înainte de pornire.

RX: patru buffer-e de 2048 octeți, fiecare completat de patru ori și rearmat de trei ori: 16 recepții și 12 rearmări. Validarea RX verifică lungimea, First/Last și erorile descriptorului. Nu verifică încă integritatea fiecărui payload RX. TX este verificabil suplimentar pe emitător prin captura pcap.

Pollingul este limitat la 30 secunde. TX și RX sunt oprite împreună; dacă device-ul poate deține memorie, bus mastering este dezactivat înainte de unmap. Dacă oprirea sigură eșuează, sunt reținute atât buffer-ele RX, cât și TX. Nu există interfață Ethernet macOS, interrupturi sau trafic permanent.

## Test exact

1. Pe ALT calculator din aceeași rețea Ethernet, într-un terminal, pornește captura înainte de reboot:

```bash
sudo tcpdump -i INTERFATA_ETHERNET -s 0 -w stage2f-tx.pcap 'ether proto 0x88b5'
```

Înlocuiește INTERFATA_ETHERNET cu numele real al interfeței acelui calculator. Captura confirmă cadrele TX pe rețea; completarea OWN în Acer singură nu confirmă livrarea.

2. În al doilea terminal al aceluiași calculator, din directorul pachetului, pornește emițătorul cu adresele reale:

```bash
python3 tools/send_rx_test.py --source IP_EMITATOR --broadcast BROADCAST_RETEA --seconds 600
```

3. Repornește imediat Acerul cu kext 0.8.0 și AppleVTD activ. Proba rulează înainte de login. Alex gestionează EFI și încărcarea.

4. După login, pe Acer:

```bash
bash COLLECT_LOCAL.command
```

5. Oprește captura și emițătorul cu Ctrl+C. Pe celălalt calculator:

```bash
python3 tools/verify_tx_capture.py stage2f-tx.pcap
```

Trimite captura .tgz de pe Acer, rezultatul emitătorului și pcap-ul sau rezultatul verificării TX.

## Criteriu de trecere

Stage2FStatus=0, Stage2FComplete=true, TxSubmitted=TxCompleted=8, TxCompletionMask=255, RxCompleted=16, RxRearmed=12, RxFirstCycleMask=RxFinalCycleMask=15, CombinedEngineObserved=true, fără erori/timeout și ChipCommandAfter=0. Verificarea externă trebuie să găsească toate cele opt cadre cu conținut exact; altfel integritatea TX pe cablu rămâne nevalidată.

## Compilare locală

```bash
bash TEST_LOCAL.command
FIRMWARE_DIR="$PWD/firmware" bash BUILD_PROBE.command --fetch-sdk
```

Rezultat: build/Release/RealtekVTDNextProbe.kext. Firmware inclus. Hardware 0.8.0: încă netestat.

## Verificare locală

Toate cele nouă suite și verificările politicilor au trecut. Buildul x86_64 cu firmware a trecut. Verificatorul pcap a acceptat o captură sintetică completă și a respins un payload corupt. Nu s-a transmis trafic în rețea din această sesiune; proba pe Acer și confirmarea TX pe cablu rămân de făcut.
