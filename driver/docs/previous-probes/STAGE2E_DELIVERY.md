# RealtekVTDNext 0.7.2 — STAGE2E RXRECYCLE

STAGE2E este incrementul conservator după burst-ul RX validat pe hardware.
Patru buffer-e mapate prin AppleVTD sunt păstrate pe două cicluri ale ringului.
După prima completare, fiecare descriptor este sincronizat pentru CPU, validat,
bufferul este pregătit din nou pentru device și descriptorul este rearmat cu
aceeași IOVA. Succesul cere două completări per descriptor, adică opt în total.

RingEnd rămâne numai pe descriptorul 3. Toate adresele sunt IOVA 64-bit într-un
singur segment. Nu există fallback fizic, bounce sau presupunere PA==IOVA.

Pollingul este finit. La timeout cu OWN activ, RX și bus mastering sunt oprite
înainte de unmap. Dacă quiesce-ul sigur eșuează, mapările sunt reținute.

Nu există interrupturi, `IONetworkInterface`, packet delivery sau trafic
permanent. Acestea rămân în afara STAGE2E.
