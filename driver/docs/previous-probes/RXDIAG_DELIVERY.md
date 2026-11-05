# 0.5.1 STAGE2C RXDIAG

Baseline 0.5.0 păstrat pe USB. Secvența, valorile și criteriile RX sunt neschimbate; acest build adaugă diagnostic per acces.

Proprietăți noi: Stage2CRxConfigStep, Operation, Offset, Width (biți), Expected, Observed, AccessSucceeded, ObservedValid. Un acces de citire reușit cu valoare diferită are AccessSucceeded=true și ObservedValid=true; o eroare de acces are false. După succes Step=complete. Cleanup-ul nu suprascrie diagnosticul.

Build:
```bash
bash TEST_LOCAL.command
FIRMWARE_DIR="$PWD/firmware" bash BUILD_PROBE.command --fetch-sdk
```

Kext: build/Release/RealtekVTDNextProbe.kext. După boot colectează cu bash COLLECT_LOCAL.command. Versiunea încărcată trebuie să fie 0.5.1. Alex gestionează încărcarea și EFI.

0.5.0: STAGE1/2A/2B complete cu mapper AppleVTD, RXConfigurationFailed înainte de OWN. RxMaxSize=16384 este o ipoteză de investigat, nu o cauză demonstrată. Nu s-a schimbat această valoare.
