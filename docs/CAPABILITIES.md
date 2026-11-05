# Capabilities — Beta 1

- **Ethernet:** copper10/100/1000 negotiation, TX/RX and negotiated PAUSE.
- **DMA:** AppleVTD/device-specific IOMapper and separately guarded NoIOMMU native64.
- **Checksums:** IPv4 and IPv6 TX/RX policies. Hardware is used only for eligible chips/formats; unconfirmed RX metadata receives no false hardware-valid flags.
- **TCP segmentation:** IPv4/IPv6 hardware eligibility plus software fallback. RTL8168EVL intentionally uses software TSO and narrower checksum offload.
- **VLAN:** software tag handling; no hardware insertion/stripping.
- **Jumbo:** up to MTU9000 after resource preparation. Default MTU1500. Existing PAUSE withdrawal/restoration policy is retained.
- **Safety:** mapping lifetimes, synchronization, OWN ordering, ownership validation and confirmed-stop requirements. Resources are retained if safe shutdown cannot be confirmed.

Software fallback remains necessary for unsupported offload formats. The beta does not add RSS/multiqueue, zero-copy, hardware VLAN, LRO/RSC, TSN/PTP or complete automatic TX-stall recovery.

EEE/LPI behavior remains chip-specific. No global EEE-disable policy is applied to unrelated chips. [Compatibility](HARDWARE.md) · [Validation](VALIDATION.md)
