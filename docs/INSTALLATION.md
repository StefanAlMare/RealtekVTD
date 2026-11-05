# Manual installation and rollback

The downloadable beta contains the **original unsigned x86_64 B kext**, not an installer. Packaging performs no installation, EFI mounting, configuration edits, kext loading, AppleVTD changes, SIP changes or traffic tests.

The accepted scope is the owner's validated Dell MAC46/XID541 **NoIOMMU** configuration. Consult `HARDWARE.md` before treating another machine or DMA mode as qualified. Kernel minimum flags used for compilation do not establish support on every macOS version; the project validation has used the recorded macOS26/Tahoe environments.

## Before a manual change

1. Keep the exact working kext and current boot configuration in an independently accessible rollback copy. Maintain a working alternative boot path appropriate to your own system.
2. Download `KEXT-MAC46-N64-TXPRECHECK-B.zip` and `SHA256SUMS.txt` from the same beta. Verify the archive hash before extraction. The expected original archive SHA-256 is `db03a827d550b618f8d606bff20f3b501f336ec73b3b8ee1d6bf6a36aae1d314`.
3. Extract `RealtekVTDNextEthernet.kext`. Expected executable SHA-256: `9b00c46b5e123c33eb851e922b0716019dcac3afa842488544f413347d3f2d37`.

An operator who already maintains a working RealtekVTDNext/OpenCore setup can manually substitute only this bundle in the existing configured location. Keep one intended Ethernet driver active; do not load competing Realtek drivers for the same PCI device. This document does not prescribe new security settings or change AppleVTD to make the beta appear compatible.

After the operator-chosen reboot, confirm the loaded executable UUID `09046FEC-48A9-3EE5-9622-B7EE8F3CD90C` and actual DMA mode. The beta tag does not replace the long original `DriverVersion`. No benchmark repetition is required by this publication.

## Rollback

If the beta is unsuitable for a system, manually restore that system's exact previously working bundle and boot configuration, then reboot through the known working path. Do not rebuild a rollback image or rename a newer binary to an old version. Existing LPI1, FC1, STOPSAFE1 and other machine-specific originals remain in the project owner's private archive; they are not interchangeable universal rollback images.

This repository does not distribute the unvalidated TXDIAG2 diagnostic as a fallback or silently enable automatic TX recovery.
