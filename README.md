# RealtekVTD — RealtekVTDNext

[Download private beta](https://github.com/StefanAlMare/RealtekVTD/releases/tag/v0.21.0-beta.1)

**Private beta · cumulative macOS Ethernet driver · Intel x86_64**

[Română](README.ro.md) · [Hardware and validation](docs/HARDWARE.md) · [Capabilities and limits](docs/CAPABILITIES.md) · [Installation / rollback](docs/INSTALLATION.md) · [Build](docs/BUILDING.md) · [License](LICENSE)

RealtekVTDNext is a native macOS driver for selected Realtek Gigabit Ethernet controllers. One cumulative codebase contains both **AppleVTD / device-specific IOMapper** DMA and **NoIOMMU native64** DMA. Hardware identity, actual mapper availability, initialized resources and per-profile policies determine which path and capabilities can be used. NoIOMMU is not forced when a mapper is active or still initializing.

## Current beta

**`v0.21.0-beta.1` packages the existing accepted B NoIOMMU image.** This is a distribution tag, not a new compiled driver version. The source and executable have not been changed or rebuilt for publication.

| Item | Identity |
|---|---|
| Accepted reference | MAC46-N64-TXPRECHECK-B, cumulative RealtekVTDNext 0.21.0 |
| Executable UUID | `09046FEC-48A9-3EE5-9622-B7EE8F3CD90C` |
| Executable SHA-256 | `9b00c46b5e123c33eb851e922b0716019dcac3afa842488544f413347d3f2d37` |
| Executable | x86_64, 691,824 bytes, unsigned kernel extension |
| Exact source | `driver/source-manifest-MAC46-N64-TXPRECHECK-B.json`, 281 entries |
| Physical acceptance | Dell MAC46 / XID541, **AppleVTD OFF / NoIOMMU** |

On 5 November 2026 the owner confirmed B as the final accepted, installed Dell reference, following physical validation and the 4 November decision to close development/testing. LPI1 is the superseded accepted checkpoint. **TXDIAG2 is an unvalidated diagnostic and is excluded from this beta.**

The reported Dell test reached **926.1 Mbps TCP TX**, with 397,650 submissions and corresponding completions, no remaining TX leases/descriptors and no watchdog in that session. Residual QueueDrops and retransmissions remain documented; this is not a claim of zero packet loss, universal stability or independent checksum/segmentation validation on the wire. See [validation](docs/VALIDATION.md).

**Acceptance on Dell NoIOMMU does not qualify the same binary on every other computer or on AppleVTD.** AppleVTD remains fully integrated, with earlier profile-specific evidence and unresolved MAC46 watchdog incidents clearly separated in the hardware matrix.

## Why the project exists

The project began with an attempt to make a Realtek solution work with Mieze's **RealtekRTL8111** in the AppleVTD environment. That approach did not produce the required result on the project machines. Development then moved to a new native macOS implementation, using contemporary Linux **r8169 / Realtek PHY** behavior as the principal hardware reference, with pinned Realtek/Mieze references for controller-specific sequences.

The design subsequently expanded to computers without AppleVTD, retaining both DMA paths in the same driver instead of maintaining separate incompatible drivers. This is **not a wrapper around the installed Mieze driver**, and no Linux kernel module runs inside macOS. References are pinned and credited; “based on recent Linux work” does not mean this beta tracks today's latest Linux automatically. See [origins and notices](docs/ORIGINS.md).

## Development machines and scope

- **HP — RTL8168EVL / MAC34:** accepted NoIOMMU TXQ4 work, with deliberate MAC34 offload restrictions.
- **ASUS Haswell — RTL8168GU/RTL8111GU / GU25 / CFG25 / MAC42:** NoIOMMU, cumulative STOPSAFE1 checkpoint.
- **ASUS1 / AsusCalin — RTL8411B / MAC44:** separate AppleVTD and NoIOMMU work, including VLAN/TSO corrections.
- **Acer — RTL8168H/8111H / MAC46:** AppleVTD ON private conditional acceptance; OFF implementation is retained with its own evidence limits.
- **Dell — RTL8168H/8111H / MAC46:** this beta's accepted NoIOMMU reference. AppleVTD watchdog investigation remains separate.

Retail computer model names are not inferred from PCI subsystem IDs. The historical Dell Precision 7720 work on **RealtekVTD 0.6.x** is a separate branch and is not silently promoted by publishing RealtekVTDNext.

## Where capability expansion stops

The cumulative target is reliable Gigabit Ethernet with negotiated link/PAUSE, IPv4/IPv6 TX/RX checksums where eligible, TSO4/TSO6 with controller-specific hardware eligibility and software fallbacks, software VLAN, and Jumbo only after resource preparation. **Default MTU remains 1500.** Hardware recognition is not permission to enable every feature on every chip.

MAC34 intentionally uses software TSO. Unsupported formats retain safe fallback rather than receiving false checksum validity or truncated descriptors. DMA mapping lifetimes, BME/OWN ordering, verified stop, retained-resource quarantine and ownership protection take priority over benchmark numbers.

There is no claim here of completed RSS/multiqueue, zero-copy, hardware VLAN, LRO/RSC, TSN/PTP, universal wake support, full automatic TX recovery or universal EEE policy. [The capability document](docs/CAPABILITIES.md) states the boundaries.

## Project status and access

This is a **project in development**, with the current Dell stage closed at B. No new optimization or diagnostic series is implied by this publication. Further work requires an explicit objective or a concrete problem, and future computers must extend this same cumulative base without discarding existing profiles or protections.

The repository and beta stay **private until the owner explicitly authorizes public access**. There is no automatic public publishing, installation or network test workflow.

The driver source is **GPL-2.0-only**: use, study, modification and redistribution are allowed under that license, including development and commercial use. Repository visibility controls access; it does not replace the license. Firmware and the vendored SDK retain their separate licenses. See [third-party notices](THIRD_PARTY_NOTICES.md).
