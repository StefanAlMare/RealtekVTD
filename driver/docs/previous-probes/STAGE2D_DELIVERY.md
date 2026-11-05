# RealtekVTDNext 0.6.0 — STAGE2D RXBURST

STAGE2D este următorul increment conservator după single-RX validat: patru
descriptori și patru buffer-e RX mapate separat prin mapper-ul dispozitivului.
Toate adresele sunt IOVA pe 64 de biți; nu există physical-address fallback,
bounce sau limită sub 4 GiB.

OWN este publicat numai după configurarea tuturor descriptorilor și un sync al
ringului. Completion-ul este verificat individual. Succesul cere toate cele
patru OWN eliberate și descriptori validați fără erori.

Pollingul este limitat la 5000 × 1 ms. La timeout sau failure cu descriptori
încă owned, RX este oprit și bus mastering este dezactivat înainte de unmap.
Dacă oprirea sigură nu poate fi confirmată, mapările rămân reținute.

Nu există interrupturi, ring recycling continuu, IONetworkInterface sau packet
delivery. Acestea rămân în afara STAGE2D.
