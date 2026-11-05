# RealtekVTDNext 0.5.2 — STAGE2C RXCFGMASK

## Cauza demonstrată pe hardware

Pe Acer RTL8168H/8111H cu AppleVTD activ, 0.5.1 a ajuns până la configurarea
RX. Scrierea `RxConfig=0x0000cf0e` a reușit, iar citirea a returnat
`0x0002cf0e`. Diferența unică este bitul 17, în zona FIFO threshold. O
comparare pe 32 de biți a oprit corect testul înainte de OWN și înainte de
activarea motorului RX (`Stage2CStatus=13`). Nu a existat fault IOMMU/AER.

## Corecția izolată

Citirea este verificată cu `0x0000cf3f`. Sunt obligatorii exact biții
programați, iar AcceptErr, AcceptRunt și AcceptAllPhys trebuie să rămână zero.
Biții din afara măștii nu decid succesul, dar sunt publicați separat. Nu s-au
schimbat descriptorul, bufferul, ordinea OWN, IOVA, pollingul sau teardown-ul.

Teste dedicate demonstrează că readback-ul Acer `0x0002cf0e` trece, iar lipsa
unui bit controlat (AcceptMyPhys) eșuează înainte de OWN.

## Limite păstrate

Aceasta rămâne o probă single-RX: fără interrupturi de trafic, packet delivery,
interfață de rețea, ring continuu ori STAGE2D. DMA folosește exclusiv mapper-ul
dispozitivului și IOVA pe 64 de biți; nu există fallback fizic, bounce sau
limită sub 4 GiB.
