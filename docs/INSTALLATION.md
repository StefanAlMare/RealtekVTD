# Installation and rollback

1. Download `RealtekVTDNext-Beta1.zip` and verify it against `SHA256SUMS.txt` from the same beta.
2. Keep the exact previously working kext and boot configuration as a rollback copy.
3. Extract **`RealtekVTDNext.kext`**. The executable UUID is `09046FEC-48A9-3EE5-9622-B7EE8F3CD90C`; SHA-256 is `9b00c46b5e123c33eb851e922b0716019dcac3afa842488544f413347d3f2d37`.
4. Installation is manual in the existing configured kext location. If using OpenCore, the configured bundle path must match `RealtekVTDNext.kext`. Keep only the intended driver active for the device.

The bundle directory has a simplified distribution name; its executable, Info.plist, bundle identifier and settings are unchanged. The package is unsigned and includes no automatic installer, security-setting changes or network tests.

After an operator-chosen reboot, confirm the loaded UUID and actual DMA mode. Use only the [documented chip/mode scope](HARDWARE.md).

To roll back, restore the exact saved kext and matching boot configuration and reboot through the known working path. Do not substitute a renamed newer executable for the saved image.
