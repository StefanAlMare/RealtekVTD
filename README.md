# RealtekVTDNext

**Beta 1 — Realtek Gigabit Ethernet for macOS Intel x86_64**

[Download Beta 1](https://github.com/StefanAlMare/RealtekVTD/releases/tag/beta-1) · [Română](README.ro.md) · [Build](docs/BUILDING.md) · [Contributors](CONTRIBUTORS.md)

**Developed by [StefanAlMare](https://github.com/StefanAlMare), with ChatGPT / OpenAI assistance.**

Driver references:

- **Linux r8169 / Realtek PHY developers** — the driver work used by Ubuntu and other Linux distributions.
- **Mieze / RealtekRTL8111** — controller-specific implementation references.

## Compatible chips

| Chip | AppleVTD | NoIOMMU |
|---|---|---|
| RTL8411B — XID `0x5C8` | Supported | Supported |
| RTL8168H / RTL8111H — XID `0x541` | Supported* | Supported |
| RTL8168EVL | Not qualified for this beta | Supported |
| RTL8168GU / RTL8111GU — XID `0x509` | Not qualified for this beta | Supported |

**Dual mode:** RTL8411B; RTL8168H/8111H with the limitation below. **NoIOMMU-only qualification in Beta 1:** RTL8168EVL and RTL8168GU/8111GU XID509.

\* A very rare, apparently random TX stall has been reported with AppleVTD on RTL8168H/RTL8111H. It remains unresolved; the cause is unknown and may involve software or hardware-specific conditions.

## What Beta 1 provides

- Gigabit TX/RX, negotiated link and PAUSE flow control.
- IPv4/IPv6 checksums and TCP segmentation, using chip-specific hardware offload or safe software fallback. RTL8168EVL uses software TSO.
- Software VLAN and resource-guarded Jumbo up to MTU9000; default MTU1500.
- Separate AppleVTD and NoIOMMU DMA paths, with ownership, synchronization and safe-stop protections.

Hardware VLAN, RSS/multiqueue and complete automatic TX-stall recovery are outside this beta. [Capabilities](docs/CAPABILITIES.md) · [Validation limits](docs/VALIDATION.md)

## From idea to driver

The project began with an attempt to obtain the required AppleVTD behavior using Mieze's RealtekRTL8111. It then became a native macOS implementation based on reviewed Linux r8169/Realtek PHY behavior and Realtek/Mieze references, and expanded to NoIOMMU. Both DMA paths and supported chip profiles remain in one cumulative codebase.

Beta 1 packages the accepted executable without recompilation. The project remains in development.

Source use and contributions follow **GPL-2.0-only**. Firmware and SDK retain separate notices. [License](LICENSE) · [Notices](THIRD_PARTY_NOTICES.md) · [Installation / rollback](docs/INSTALLATION.md)
