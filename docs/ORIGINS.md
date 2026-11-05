# Origins and pinned references

## From the original goal to the cumulative driver

The owner's initial goal was a Realtek Ethernet solution compatible with AppleVTD, beginning with an attempt involving Mieze's RealtekRTL8111. The required behavior was not achieved on the project machines, so the project moved to a new native macOS implementation based on explicitly reviewed hardware behavior from Linux r8169 / Realtek PHY and controller-specific Realtek references.

This history is the owner's project account. It is not a statement that Mieze's driver fails on every system or that the original upstream project provides no useful implementation. Mieze remains an important credited reference, particularly for GU24/GU25 identification, PHY and MAC programming.

RealtekVTDNext is distinct from the historical RealtekVTD0.6.x branch. It uses its own macOS controller/datapath abstractions and public kernel APIs. Linux register behavior and firmware sequencing are ported into those abstractions; a Linux kernel module is not embedded as a compatibility layer.

The project progressively added NoIOMMU native64 alongside the device-specific IOMapper path, then retained cumulative profile changes across GU25, MAC44, MAC34 and MAC46. The repository name is **RealtekVTD**; the current driver implementation is **RealtekVTDNext**.

## References fixed in the source history

| Reference | Pinned revision / role |
|---|---|
| [Linux r8169 / Realtek PHY](https://github.com/torvalds/linux/commit/3cdeaef1754ab8155cdd828c958ada3d36e9ad9a) | `3cdeaef1754ab8155cdd828c958ada3d36e9ad9a`, principal hardware baseline |
| [Linux EEE MMD correction](https://github.com/torvalds/linux/commit/202fef9bbbf5784487eec27581389c6fb97c350d) | `202fef9bbbf5784487eec27581389c6fb97c350d` |
| [Linux MAC46 link-up LPI behavior](https://github.com/torvalds/linux/blob/f71ff7d406523748bfb8236830c7eaa6a308aaa1/drivers/net/ethernet/realtek/r8169_main.c) | `f71ff7d406523748bfb8236830c7eaa6a308aaa1`, profile-specific LPI reference |
| [Mieze / RealtekRTL8111](https://github.com/Mieze/RTL8111_driver_for_OS_X/tree/a3dad34ed4f1e72805ce42f6d506f6917fdf79a0) | `a3dad34ed4f1e72805ce42f6d506f6917fdf79a0`; Realtek r8168 8.047.04 reference |
| [MacKernelSDK](https://github.com/acidanthera/MacKernelSDK/tree/3f750085caa17ec3a7880f11c11bf4f48cd6a164) | `3f750085caa17ec3a7880f11c11bf4f48cd6a164`, kernel headers/stubs used by the native build |
| linux-firmware | `50c28bbf4158b310fa8463e8937a61940bff76c4`, exact provenance and component hashes in `driver/firmware/provenance.json` |

These pins are audit references, not a claim of synchronizing every Linux revision. `driver/SOURCES.md`, `driver/upstream-manifest.json`, firmware provenance and retained code notices are authoritative for their respective inputs. Historical source documents describe earlier stages and must not be read as current feature declarations.

Credits include Realtek Semiconductor, ShuChen, François Romieu and Linux r8169/Realtek PHY contributors; Mieze and the RealtekRTL8111 contributors; acidanthera and the original SDK/header copyright holders. Existing copyright and license notices are retained.
