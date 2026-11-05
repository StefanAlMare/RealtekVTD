# 0.7.2 STAGE2E SAMERX

Baseline 0.7.1 păstrat. La 0.7.1 STAGE2D primea 4 cadre, apoi STAGE2E expira cu 0/8, deși RX/BME/link erau active.

Acest build înlocuiește execuția separată D apoi E cu o singură sesiune de reciclare, pornită după STAGE2C. Cele patru buffer-e sunt mapate o singură dată, RX este activat o singură dată, iar prima completare a fiecărui descriptor este urmată de rearmare fără oprirea RX. A doua completare validează reciclarea. Oprirea are loc doar la sfârșit sau eroare. Configurația, mapper-ul și diagnosticul rămân cele din 0.7.1; pollingul rămâne finit, 30 secunde.

## Interpretare

`Stage2ERunMode=direct-from-c-single-rx-session`, `Stage2DStandaloneRun=false`. `Stage2DComplete=false` este intenționat: proba D separată nu a rulat. Nu indică regresie; prima rundă de patru cadre este validată în sesiunea E prin `Stage2EFirstCycleComplete=true`, `Stage2EFirstCycleMask=15` și `Stage2EFirstCycleCompletePoll`.

Succes: `Stage2EComplete=true`, `Stage2ECompletedCount=8`, `Stage2ERearmedCount=4`, FirstCycleMask=SecondCycleMask=15, fără erori. 4/8 indică prima rundă reușită fără validarea rundei a doua. 0/8 indică o problemă anterioară reciclării. Succesul acestui test nu validează repornirea după D, trafic continuu sau integrarea Ethernet în macOS.

## Următorul boot

Pe ALT calculator din aceeași rețea Ethernet pornește emitătorul ÎNAINTE de reboot, pentru 600 secunde:

```bash
python3 tools/send_rx_test.py --source IP_EMITATOR --broadcast BROADCAST_RETEA --seconds 600
```

Bootează imediat Acerul cu 0.7.2 și AppleVTD activ. Proba rulează înainte de login. După login:

```bash
bash COLLECT_LOCAL.command
```

Trimite .tgz și rezultatul emitătorului. Adresele sunt cele reale ale emitătorului și broadcastului subnetului; nu se folosesc literal placeholder-ele. Alex gestionează EFI/încărcarea.

## Compilare

```bash
bash TEST_LOCAL.command
FIRMWARE_DIR="$PWD/firmware" bash BUILD_PROBE.command --fetch-sdk
```

Rezultat: build/Release/RealtekVTDNextProbe.kext. Firmware inclus. Hardware 0.7.2: încă netestat.

## Validare locală

Toate suitele și verificările de politici au trecut. Testele noi acoperă 0/8, 4/8 și 8/8, o singură activare RX cu oprire doar la sfârșit, precum și refuzul execuției fără STAGE2C complet. Buildul cu firmware și verificarea kext au trecut. Codex nu a încărcat kext-ul și nu a modificat EFI.
