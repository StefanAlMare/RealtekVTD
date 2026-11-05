# STAGE2B single-TX bounded-link-wait delivery

## Baseline înghețat

`RealtekVTDNext-0.3.1-STAGE2A-MAPPER.zip`, SHA-256
`389c40a7ece4f3c61afe61377afdb86aedc39fde011ea0e539b3715b055d14e3`.

Testul hardware Acer pentru 0.4.0 a validat mapper-ul AppleVTD, ringurile IOVA
și bus mastering, apoi s-a oprit sigur la link-down înainte de alocarea
bufferului TX. 0.4.1 este o copie separată; baseline-urile 0.3.1 și 0.4.0 nu
sunt modificate.

## Delimitare

Această versiune demonstrează un singur packet DMA TX prin descriptorul 0 și
polling. Nu este driver Ethernet funcțional. Nu există RX packets, interrupturi
de trafic, interfață de rețea, DHCP, ARP, IP sau trafic repetitiv.

## Timeout

Un mapping încă owned de NIC nu este eliberat. Calea de timeout oprește TX,
dezactivează bus mastering și verifică readback-ul. Numai după confirmare face
complete/unmap/release. Dacă confirmarea eșuează, mapping-ul rămâne intenționat
reținut pentru a evita DMA către memorie eliberată.

## Gate hardware

Cu link activ, PASS cere ring readback egal cu IOVA, buffer single-segment,
adresa descriptorului egală cu IOVA bufferului, OWN 1→0, fără fault IOMMU și
TX/RX oprite la final. Gate-ul face maximum 60 de citiri BMSR duble la 100 ms.
Cu link down după această fereastră, rezultatul corect este `link-down`,
`Stage2BLinkTimedOut=true`, fără buffer TX și fără OWN.
