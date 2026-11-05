# RealtekVTDNext

**Beta 1 — driver Realtek Gigabit Ethernet pentru macOS Intel x86_64**

[Descarcă Beta 1](https://github.com/StefanAlMare/RealtekVTD/releases/tag/beta-1) · [English](README.md) · [Compilare](docs/BUILDING.md) · [Contributori](CONTRIBUTORS.md)

**Dezvoltat de [StefanAlMare](https://github.com/StefanAlMare), cu asistență ChatGPT / OpenAI.**

Referințe pentru driver:

- **Dezvoltatorii Linux r8169 / Realtek PHY** — driverul folosit de Ubuntu și alte distribuții Linux.
- **Mieze / RealtekRTL8111** — referințe de implementare specifice controllerelor.

## Cipuri compatibile

| Cip | AppleVTD | NoIOMMU |
|---|---|---|
| RTL8411B — XID `0x5C8` | Suportat | Suportat |
| RTL8168H / RTL8111H — XID `0x541` | Suportat* | Suportat |
| RTL8168EVL | Necalificat în acest beta | Suportat |
| RTL8168GU / RTL8111GU — XID `0x509` | Necalificat în acest beta | Suportat |

**Dublu mod:** RTL8411B și RTL8168H/8111H, cu limita de mai jos. **Calificate numai NoIOMMU în Beta 1:** RTL8168EVL și RTL8168GU/8111GU XID509.

\* Cu AppleVTD pe RTL8168H/RTL8111H a fost întâlnit un blocaj TX foarte rar, aparent aleatoriu, încă nerezolvat. Cauza nu este cunoscută; pot fi implicate condiții software sau particularități hardware.

## Ce oferă Beta 1

- TX/RX Gigabit, negocierea linkului și flow control PAUSE negociat.
- Checksum IPv4/IPv6 și segmentare TCP, hardware unde cipul permite și software în celelalte cazuri. RTL8168EVL folosește TSO software.
- VLAN software și Jumbo până la MTU9000 după pregătirea resurselor; MTU implicit1500.
- Trasee DMA AppleVTD și NoIOMMU distincte, cu protecții de ownership, sincronizare și oprire sigură.

VLAN hardware, RSS/multiqueue și recuperarea automată completă după blocaj TX nu fac parte din acest beta. [Capabilități](docs/CAPABILITIES.md) · [Limitele validării](docs/VALIDATION.md)

## De unde am plecat și unde am ajuns

Proiectul a pornit de la încercarea de a obține funcționarea necesară cu AppleVTD folosind RealtekRTL8111 al lui Mieze. Am trecut apoi la o implementare macOS proprie, după comportamentul verificat al Linux r8169/Realtek PHY și referințe Realtek/Mieze, extinsă ulterior la NoIOMMU. Ambele trasee DMA și profilurile suportate rămân în aceeași bază cumulativă.

Beta 1 distribuie executabilul acceptat, fără recompilare. Proiectul continuă să fie în dezvoltare.

Folosirea sursei și contribuțiile respectă **GPL-2.0-only**. Firmware-ul și SDK-ul au notificări separate. [Licență](LICENSE) · [Atribuiri](THIRD_PARTY_NOTICES.md) · [Instalare / rollback](docs/INSTALLATION.md)
