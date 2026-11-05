# RealtekVTDNext 0.5.3 — STAGE2C hardware-validated baseline

Captura Acer 0.5.2, SHA-256
`734a9eee843be936b155d00c60084eb9696ebab90301f35fb275d0d735a4299f`,
validează complet single TX și single RX prin IOVA cu AppleVTD activ.

STAGE2C a primit un frame de 68 bytes după 141 polling-uri, cu descriptor
valid, fără biți de eroare și cu OWN eliberat. `ChipCommandDuringRx=8`, iar
după oprire `ChipCommand=0`, TX/RX disabled și fără fault IOMMU/AER.

0.5.3 păstrează byte-for-byte logica Stage2C și schimbă numai reprezentarea
numerică ERI1B0. Valoarea hardware uint32 este zero-extinsă într-un OSNumber
de 64 de biți pentru a evita serializarea `ioreg` cu extensie de semn;
`ERI1B0Width=32`, valoarea hex și bytes păstrează semantica hardware explicită.

Nu sunt introduse STAGE2D, interfață de rețea, interrupturi de trafic, cozi
continue sau packet delivery către sistem.
