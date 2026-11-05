# Referințe fixate — RealtekVTDNext STAGE3B

Baseline principal: torvalds/linux, commit
`3cdeaef1754ab8155cdd828c958ada3d36e9ad9a` — r8169: disable EEE on RTL8168h/8111h.

Sursele consultate și SHA256 sunt în `upstream-manifest.json`:

- drivers/net/ethernet/realtek/r8169_main.c
- drivers/net/ethernet/realtek/r8169_phy_config.c
- drivers/net/ethernet/realtek/r8169_firmware.c
- drivers/net/ethernet/realtek/r8169_firmware.h
- drivers/net/phy/realtek/realtek_main.c

[Commit principal](https://github.com/torvalds/linux/commit/3cdeaef1754ab8155cdd828c958ada3d36e9ad9a)
și [corecția mapării EEE MMD](https://github.com/torvalds/linux/commit/202fef9bbbf5784487eec27581389c6fb97c350d).
Ambele descrieri de commit au fost citite împreună cu sursele.
Linux v6.17 nu mai este baseline-ul principal.

STAGE2B folosește din `r8169_main.c` la același commit layout-ul
`TxDesc { opts1, opts2, addr }`, biții `DescOwn`, `FirstFrag`, `LastFrag`,
registrele `ChipCmd=0x37`, `TxPoll=0x38`, comanda `NPQ=0x40` și ordinea
upstream în care OWN este publicat după adresă/opțiuni și bariera DMA.

STAGE2C folosește din aceeași sursă layout-ul `RxDesc`, `DescOwn`, `RingEnd`,
`FirstFrag`, `LastFrag`, lungimea RX pe 14 biți, biții de eroare RX, bufferul
de 16383 bytes, `RxMaxSize=0xda` și configurația RX pentru MAC_VER_40…52.
Filtrul temporar acceptă numai broadcast, multicast și adresa fizică proprie;
nu activează promiscuous mode.

Implementarea este scrisă în abstracțiile proiectului. Funcțiile Linux nu sunt
copiate textual. Valorile registrelor și microprogramul numeric 8411B provin
din comportamentul hardware documentat de upstream. Proveniența include
Realtek, ShuChen, Francois Romieu și contributorii Linux r8169/Realtek PHY.
Licența sursei de referință și a proiectului: GPL-2.0-only; text inclus în LICENSE.

MacKernelSDK: acidanthera/MacKernelSDK,
`3f750085caa17ec3a7880f11c11bf4f48cd6a164`, folosit numai pentru buildul kextului.
[SDK](https://github.com/acidanthera/MacKernelSDK/tree/3f750085caa17ec3a7880f11c11bf4f48cd6a164).
Nu s-a modificat vreun SDK sau baseline istoric. SDK-ul descărcat este în `build/deps/`,
nu este inclus în arhiva livrată și are propria licență.

Firmware-urile incluse sunt fișierele originale linux-firmware, cu licența și
proveniența păstrate în `firmware/`. Nu sunt hardcodate sau reconstruite.

STAGE3B folosește ordinea r8169 pentru fragmente TX (OWN al primului fragment publicat ultimul), consumatori circulari, CRC/FCS exclus la livrarea RX și re-kick TX după completări când mai există descriptorii pending. Integrarea macOS este nouă; nu reutilizează driverul RealtekRTL8111 sau CORE0. API-urile IONetworkingFamily, mbuf și IOMemoryDescriptor/IODMACommand sunt verificate în headerele SDK-ului fixat. Pregătirea mapărilor TX are loc în afara gate-ului; nu se impune un plafon de 4 GiB sau o mască fizică.

## STAGE3D: private TX snapshot

0.13.0 uses mbuf_copydata (sys/kpi_mbuf.h in the pinned SDK) to copy each complete frame into an owned IOBufferMemoryDescriptor before preparing IODMACommand. It retains the device IOMapper, native64 IOVAs, IRQ-only completion, and BME-before-release lifetime. The per-packet copy is a diagnostic change and is not represented as Linux zero-copy behavior. Historical instructions: docs/previous-probes/STAGE3D_DELIVERY.md. Current instructions: docs/previous-probes/STAGE3E_DELIVERY.md.

## STAGE3E

A single IOBufferMemoryDescriptor and mapped native64 IODMACommand back the 65-slot TX pool. The IOMapper remains device-specific. Slots are reserved with compiler atomic intrinsics; DMA synchronization/submit remain under the controller lock. Kernel headers and API signatures are from the same pinned MacKernelSDK. No physical-address bypass, low4G allocation, TSO or checksum offload is added.

## STAGE3F IRQ instrumentation

The hardware path, interrupt mask and bounded service order remain from 0.14.0. Added atomic counters distinguish filter calls, workloop callbacks and serviceNetwork passes; bits correspond to the existing r8169 interrupt definitions (RxOK, RxErr, TxOK, TxErr). The pinned SDK IOFilterInterruptEventSource/IOInterruptEventSource API remains in use. New compiler atomics must compile inline without user-space dependencies, checked by the kext import audit. Historical instructions: docs/previous-probes/STAGE3F_DELIVERY.md.

## STAGE3G PCI messaged selection

Provider-declared indices only, at most 32, queried through getInterruptType. Uses kIOInterruptTypePCIMessaged from the pinned SDK; MSI-X retains this flag in Apple IOPCIMessagedInterruptController. IOFilterInterruptEventSource registers the selected index, and IOPCIFamily enables the message capability. Primary Apple source: https://github.com/apple-oss-distributions/IOPCIFamily/blob/main/IOPCIMessagedInterruptController.cpp . No manual message-address/vector/table programming. Historical instructions: docs/previous-probes/STAGE3G_DELIVERY.md.

## STAGE4A power management

Uses the pinned SDK IONetworkController registerWithPolicyMaker hook, IOService setPowerState/registerPowerDriver/changePowerStateTo, and the controller command gate. IONetworkController already calls PMinit and joins the provider PM tree; no duplicate initialization is added. PCI parent handles sleep power, native mapper and MSI restoration. The driver verifies D0/memory-enable/BME-off before rebuilding hardware state and uses the existing safe stop/restart core. It retains prepared mappings during sleep; this must be validated on the actual Acer.

Primary Apple sources: https://github.com/apple-oss-distributions/IONetworkingFamily/blob/main/IONetworkController.cpp and https://github.com/apple-oss-distributions/IONetworkingFamily/blob/main/IOEthernetInterface.cpp . Current instructions: STAGE4A_DELIVERY.md.
