# STAGE2C single-RX delivery

## Baseline hardware înghețat

`RealtekVTDNext-0.4.1-STAGE2B-LINKWAIT.zip`, SHA-256
`53478187193b19651793ea1937285dba5836b1752363256a7d0028762cf87c82`.

Captura hardware Acer:
`stage2b-linkwait-20261002-002754.tgz`, SHA-256
`5c85d151fa43b540991c14e47aec73fad9b5d1ee701f97fc7018bd63a0f72d87`.

Captura validează STAGE2B complet: MAC_VER_46/XID 0x541, firmware aplicat,
AppleVTD mapper prezent, ringuri IOVA corecte, link după 30 polls/2,9 s,
single TX complet în primul poll, OWN eliberat și niciun fault IOMMU. Motoarele
TX/RX sunt oprite la final.

## Delimitare 0.5.1

STAGE2C adaugă un singur buffer RX FromDevice și un singur descriptor RX cu
polling finit. Nu livrează frame-ul către macOS și nu creează interfață de
rețea, interrupturi de trafic sau ring RX continuu.

La timeout cu OWN încă publicat, RX și bus mastering trebuie oprite înainte de
unmap. Dacă oprirea nu poate fi confirmată, mapping-ul se păstrează pentru a
evita DMA către memorie eliberată.
