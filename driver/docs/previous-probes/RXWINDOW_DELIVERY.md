# RealtekVTDNext 0.7.2 STAGE2E RXWINDOW

Baseline 0.7.0 este păstrat. Testul cere tot 8 completări și 4 rearmări. RX polling este extins de la 8 la 30 secunde; bootul poate dura cu aproximativ 22 secunde mai mult dacă testul expiră.

Diagnosticul citește comanda RX, PHYStatus (0x6c), IntrStatus (0x3e, fără acknowledgement) și PCI Command înainte de polling și la fiecare 100 de încercări. Păstrează ultimii descriptori și adresele lor înainte de cleanup. Nu resetează placa între D și E și nu schimbă configurația RX.

Stage2ERxCommandLostCount și Stage2EBusMasterLostCount numără eșantioanele în care RX sau bus mastering apar dezactivate. Stage2EDiagnosticReadFailures numără accesurile de diagnostic eșuate. Stage2EOwnMaskLast, descriptorii și adresele sunt capturi înainte de oprire. Valorile finale ChipCommandAfterRx/BusMasterEnabled pot reflecta cleanup-ul, nu cauza timeout-ului.

## Test cu trafic controlat

Pe ALT calculator din aceeași rețea cablată, pornește înainte de bootul Acerului:

```bash
python3 tools/send_rx_test.py --source IP_CALCULATOR_EMITATOR --broadcast BROADCAST_RETEA --seconds 180
```

Înlocuiește cele două valori cu adresele reale ale interfeței Ethernet și ale broadcastului rețelei. Exemplu NUMAI pentru o rețea 192.168.1.0/24 cu emitător 192.168.1.20: `--source 192.168.1.20 --broadcast 192.168.1.255`. Scriptul trimite 10 datagrame broadcast pe secundă, timp finit, către portul 49071. Acerul nu are nevoie de IP pentru această probă Ethernet. Păstrează ieșirea emitătorului; dacă bootul depășește 180 secunde folosește până la 600 secunde.

Cu scriptul în execuție, bootează Acerul cu kext 0.7.2, AppleVTD activ și cablul conectat. După boot rulează `bash COLLECT_LOCAL.command` și trimite arhiva .tgz împreună cu rezultatul emitătorului. Alex gestionează EFI și încărcarea.

Emiterea broadcast nu demonstrează că switchul a livrat cadrele Acerului. Dacă avem încă 0/8 cu RX/BME/link active, următorul diagnostic trebuie să distingă livrarea cadrelor de starea internă RX la repornire.

## Build

```bash
bash TEST_LOCAL.command
FIRMWARE_DIR="$PWD/firmware" bash BUILD_PROBE.command --fetch-sdk
```

Rezultat: `build/Release/RealtekVTDNextProbe.kext`. Firmware inclus. Hardware 0.7.2: încă netestat.

## Verificare locală

Toate cele opt suite C++ și verificările politicilor au trecut. Testele noi verifică snapshot-ul înainte de cleanup, citire PCI eșuată și bus mastering raportat dezactivat. Buildul cu firmware și verificarea kext x86_64 au trecut. Scriptul emitătorului a fost verificat fără a trimite trafic în această sesiune.
