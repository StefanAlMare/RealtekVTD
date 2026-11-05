# RealtekVTDNext 0.10.0 STAGE3A IRQ

Baseline 0.9.0 păstrat. STAGE2G a validat 32 TX, 24 rearmări TX, 64 RX și 60 rearmări RX cu AppleVTD. STAGE3A păstrează aceste ținte, dar colectează completările numai în acțiunea de întrerupere pe IOWorkLoop.

## Arhitectură

IOFilterInterruptEventSource pe sursa PCI index 0: filtrul citește IntrStatus și maschează IntrMask în context primar. Nu alocă, nu loghează și nu procesează descriptorii în filtru. Acțiunea pe IOWorkLoop citește/confirmă cauzele W1C, procesează completările și rearmează IRQ. Drain-ul este limitat la opt treceri pe acțiune.

Masca este 0x000f (RxOK/RxErr/TxOK/TxErr). Nu se schimbă manual configurația MSI/MSI-X; tipul sursei furnizate de IOPCIFamily este expus în Stage3AInterruptType. Readback-ul inițial al măștii poate fi 0 dacă filtrul a mascat deja o întrerupere pending; dovada necesară rămâne numărul de acțiuni IRQ plus completările.

Un IOTimerEventSource publică următorul TX la 100 ms, fără syncForCPU sau colectarea completărilor. Un timer separat de 30 secunde oprește proba dacă nu termină. Fără întreruperi nu există fallback de polling. Ringurile și buffer-ele rămân cele din STAGE2G, cu IOVA 64-bit și refolosire numai după întoarcerea OWN.

Întreruperile sunt armate după publicarea diagnosticului inițial, prin command gate. Sursele sunt dezactivate și timer-ele anulate la terminare. La stop/free sunt eliminate înainte de detașarea backend-ului și eliberarea DMA. Dacă oprirea DMA nu poate fi confirmată, mapările sunt reținute. Nu există încă interfață Ethernet macOS.

## Test exact

Pe ALT calculator din aceeași rețea Ethernet, înainte de reboot:

```bash
sudo tcpdump -i INTERFATA_ETHERNET -s 0 -w stage3a-tx.pcap 'ether proto 0x88b5'
```

În alt terminal, cu adresele reale:

```bash
python3 tools/send_rx_test.py --source IP_EMITATOR --broadcast BROADCAST_RETEA --seconds 600
```

Repornește imediat Acerul cu 0.10.0 și AppleVTD activ. Alex gestionează EFI și încărcarea. Proba începe la boot, dar este asincronă și nu blochează login-ul până la terminarea întregului test.

AȘTEAPTĂ 35 SECUNDE DUPĂ LOGIN, apoi din directorul noului pachet pe Acer:

```bash
bash COLLECT_LOCAL.command
```

Oprește captura/emițătorul cu Ctrl+C și verifică pcap-ul cu scriptul versiunii 0.10.0:

```bash
python3 tools/verify_tx_capture.py stage3a-tx.pcap
```

Trimite .tgz, pcap-ul și rezultatul emitătorului.

## Criterii

Stage3AStatus=0, Stage3AComplete=true, Stage3ARunning=false, Stage3AInterruptActions>0, TxCompleted=32, TxRearmed=24, RxCompleted=64, RxRearmed=60, fără timeout, ChipCommandAfter=0. TimerCompletionPolling=false.

IRQ=0 și timeout înseamnă că trebuie investigată livrarea/armarea întreruperii; rezultatul nu poate fi marcat PASS doar pentru că ringurile sunt mapate. B/C/D/E/F/G standalone nu rulează în acest build. Integritatea TX pe cablu este confirmată separat de pcap. Validarea RX privește descriptorii/lungimile, nu încă întregul payload.

## Compilare

```bash
bash TEST_LOCAL.command
FIRMWARE_DIR="$PWD/firmware" bash BUILD_PROBE.command --fetch-sdk
```

Kext: build/Release/RealtekVTDNextProbe.kext. Firmware inclus. Hardware STAGE3A: încă netestat.

## Validare locală

Toate cele 11 suite și verificările politicilor au trecut. Testele noi acoperă colectarea exclusiv prin evenimente IRQ, absența pollingului în timer, lipsa completărilor când nu există IRQ, termenul-limită și reținerea sigură a mapărilor. Buildul x86_64 cu firmware a trecut; verificatorul pcap a acceptat 32 cadre sintetice STAGE3A. Numai proba hardware poate valida livrarea reală a întreruperilor PCI. Nu s-a încărcat kext-ul și nu s-a modificat EFI în această sesiune.
