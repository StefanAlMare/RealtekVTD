# STAGE2A.1 mapper delivery

## Baseline

`RealtekVTDNext-0.2.0-STAGE1-FW.zip`, SHA-256
`1af89d0be862978fa7ccdddf6a7b304b4850c056f46f98bc14af531170752d91`.

Baseline-urile 0.2.0 și 0.3.0 nu au fost modificate. Această versiune este o
copie de lucru separată, 0.3.1-STAGE2A-MAPPER.

## Corecția 0.3.1

Testul hardware 0.3.0 s-a oprit curat la `mapper-check`, înainte de orice
alocare, deoarece provider-ul PCI nu avea mapper asociat. Personalitatea
`RealtekVTDNextProbe` conține acum `IOPCIUseDeviceMapper=true` înainte de
matching/attach. Obținerea mapper-ului rămâne exclusiv prin
`IOMapper::copyMapperForDevice(provider)`.

Diagnosticele read-only disting prezența proprietăților provider-ului,
tipul `iommu-parent`, `iommu-selection`, serviciul AppleVTD și mapper-ul de
platformă. `ERI1B0` este construit explicit ca `OSNumber` unsigned pe 32 biți.

## Arhitectură

`DMAProgram` deține politica portabilă: stări, validarea segmentului, split
high/low, programarea registrelor, activarea târzie a bus mastering-ului și
rollback. `MacOSBackend` deține exclusiv obiectele IOKit și durata lor de viață.

Alocarea și `IODMACommand::prepare()` rulează în contextul de start, în afara
acțiunii `IOCommandGate`, deoarece API-ul SDK documentează că poate bloca.
STAGE1 hardware access rămâne serializat prin gate. În STAGE2A nu există încă
interrupturi, packet queues sau alte accesări concurente ale registrelor.

Maparea rămâne pregătită după succes până la `stop/free`. La oprire:

```text
disable bus master
mask interrupts / stop TX-RX / clear ring registers
unmap RX / complete RX / release RX
unmap TX / complete TX / release TX
restore PCI state and BAR mapping
```

## Gate hardware următor

AppleVTD și `AppleVTDDeviceMapper` trebuie să fie active. Probe-ul trebuie să
dețină `10ec:8168`, iar provider-ul trebuie să arate
`IOPCIUseDeviceMapper=Yes` și `iommu-parent`. Succesul cere toate proprietățile
enumerate în README și niciun timeout/fault DMA sau IOMMU în kernel log.

Nu se testează trafic. `CmdTxEnb` și `CmdRxEnb` trebuie să rămână zero.
