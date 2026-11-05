# RealtekVTD — RealtekVTDNext

[Download private beta](https://github.com/StefanAlMare/RealtekVTD/releases/tag/v0.21.0-beta.1)

**Beta privat · un singur driver cumulativ pentru macOS Intel x86_64**

[English](README.md) · [Calculatoare și validare](docs/HARDWARE.md) · [Capabilități și limite](docs/CAPABILITIES.md)

RealtekVTDNext este un driver Ethernet nativ pentru anumite controllere Realtek Gigabit. Aceeași sursă păstrează atât traseul **AppleVTD / DeviceMapper**, cât și **NoIOMMU native64**. Alegerea depinde de hardware și de disponibilitatea reală a mapperului; nu ocolim un mapper activ sau în curs de inițializare.

## Versiunea aleasă pentru beta

`v0.21.0-beta.1` distribuie **B NoIOMMU**, versiunea finală acceptată și instalată pe Dell, conform confirmării proprietarului din 5 noiembrie și închiderii etapei din 4 noiembrie 2026.

- UUID: `09046FEC-48A9-3EE5-9622-B7EE8F3CD90C`.
- SHA-256 executabil: `9b00c46b5e123c33eb851e922b0716019dcac3afa842488544f413347d3f2d37`.
- Sursa exactă: manifestul cu 281 de intrări din `driver/`.
- Binarul original este păstrat **fără recompilare, modificări sau schimbare de UUID**.

LPI1 este checkpointul acceptat anterior, depășit de B. TXDIAG2 este un diagnostic nevalidat fizic și **nu face parte din beta**. Eticheta beta este o identitate de distribuție GitHub; numele lung al buildului din driver rămâne cel original.

Pe Dell NoIOMMU s-au raportat **926,1 Mbps TCP TX**, 397.650 predări și completări corespunzătoare, zero resurse TX rămase ocupate și niciun watchdog în sesiunea respectivă. QueueDrops și retransmisiile reziduale nu sunt ascunse. Publicarea nu reprezintă o nouă probă fizică și nu certifică automat toate celelalte calculatoare.

## De unde am pornit

Ideea inițială a fost obținerea unei soluții Realtek în zona AppleVTD folosind driverul **RealtekRTL8111 al lui Mieze**. Încercarea nu a dat rezultatul necesar pe calculatoarele proiectului. Am trecut la construirea unei implementări macOS proprii, pornind de la comportamentul hardware documentat în Linux **r8169 / Realtek PHY**, cu referințe fixate și cu secvențe specifice verificate inclusiv față de Mieze/Realtek.

Ulterior am extins aceeași bază și pentru calculatoarele fără AppleVTD. Nu avem un driver HP separat de ASUS, Acer sau Dell: păstrăm nucleul comun și condiții explicite pentru fiecare profil. Nu este un modul Linux încărcat în macOS și nici o extensie care necesită driverul Mieze instalat simultan.

Referințele sunt fixate pentru audit. Nu pretindem că acest beta încorporează automat toate modificările din cel mai nou Linux disponibil astăzi.

## Calculatoarele pe care am lucrat

| Calculator | Controller / profil | AppleVTD ON | AppleVTD OFF / NoIOMMU |
|---|---|---|---|
| HP | RTL8168EVL / MAC34 | Traseu păstrat; noile funcții MAC34 nu sunt automat activate cu mapper | TXQ4 acceptat condiționat în domeniul testat |
| ASUS Haswell | RTL8168GU/RTL8111GU / GU25, CFG25, MAC42, XID509 | Integrat; fără validare fizică GU25 ON declarată | STOPSAFE1 acceptat în domeniul privat |
| ASUS1 / AsusCalin | RTL8411B / MAC44, XID5C8 | Portări și dovezi distincte ON | Suport explicit NoIOMMU și corecții VLAN/TSO; limitele probelor sunt documentate |
| Acer | RTL8168H/8111H / MAC46, XID541 | FULL ON cu CONDITIONAL GO; LPI1 aplicat fizic, fără dovada dispariției definitive a întreruperilor | FULL OFF implementat și verificat offline; nu transferăm validarea Dell asupra Acer |
| Dell curent | RTL8168H/8111H / MAC46, XID541 | Blocajele watchdog rămân o investigație distinctă | **B — referința acceptată a acestui beta** |

Nu inventăm modelele comerciale care nu sunt confirmate în dovezi. Dell Precision 7720 din istoricul RealtekVTD 0.6.x rămâne separat de identificarea Dell curent. GU24/CFG24/XID508 și RTL8168M/XID6C0 sunt profiluri de sursă distincte, fără acceptare fizică nouă în această distribuție.

## Capabilități și limita pe care ne-am oprit

Păstrăm linkul Gigabit, negocierea PAUSE, checksum IPv4/IPv6, TSO4/TSO6, VLAN software și Jumbo, fiecare numai în domeniul permis de hardware și de resurse. Calea hardware și fallbackul software sunt distincte. MAC34 folosește intenționat TSO software și are restricții checksum proprii. MTU implicit rămâne **1500**.

Nu activăm automat toate funcțiile pe un controller doar fiindcă sunt validate pe altul. Nu declarăm finalizate RSS/multiqueue, zero-copy, VLAN hardware, LRO/RSC, TSN/PTP sau recuperarea automată completă după blocaj. Nu declarăm checksumurile ori segmentarea pe fir validate doar din contoarele driverului sau testele host.

Protecțiile DMA/BME/OWN, ownershipul, sincronizările, oprirea verificată și retenția resurselor când oprirea nu poate fi confirmată rămân obligatorii. Un rezultat mai mare într-un benchmark nu justifică eliminarea lor.

## Statut și licență

Proiectul rămâne **în dezvoltare**, dar etapa Dell se încheie la B. Nu reluăm testele și nu introducem modificări suplimentare pentru această publicare. Dezvoltarea poate continua ulterior pentru o problemă concretă sau un obiectiv nou, pe aceeași bază cumulativă.

Depozitul și beta-ul rămân **private până la autorizarea explicită a proprietarului pentru publicare**.

Codul este **GPL-2.0-only**, cu drept de folosire, studiu, modificare și redistribuire în condițiile licenței. Nu introducem o restricție „numai uz personal”. Licențele firmware-ului și SDK-ului se păstrează separat. Accesul privat la GitHub nu schimbă licența codului.

Instrucțiuni: [instalare și rollback manual](docs/INSTALLATION.md), [build](docs/BUILDING.md), [validare și limite](docs/VALIDATION.md), [atribuiri](THIRD_PARTY_NOTICES.md).
