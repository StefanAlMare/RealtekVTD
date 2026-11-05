# Livrare STAGE 1 — 1 octombrie 2026

## 1. Fișiere create/modificate

ChipIdentity.hpp și RegisterAccess.hpp sunt păstrate și extinse. Fișierele
noi sunt enumerate mai jos. Nicio implementare istorică nu este importată.

- `BUILD_PROBE.command`
- `COLLECT_LOCAL.command`
- `LICENSE`
- `README.md`
- `SOURCES.md`
- `TEST_LOCAL.command`
- `macos/KextEntry.cpp`
- `macos/MacOSBackend.cpp`
- `macos/MacOSBackend.hpp`
- `macos/RealtekVTDNextProbe.cpp`
- `src/ChipIdentity.hpp`
- `src/FirmwareProgram.cpp`
- `src/FirmwareProgram.hpp`
- `src/HardwareInit.cpp`
- `src/HardwareInit.hpp`
- `src/HardwareInterfaces.hpp`
- `src/PhyInit.cpp`
- `src/PhyInit.hpp`
- `src/ProbeProgram.cpp`
- `src/ProbeProgram.hpp`
- `src/RTL8411BPatch.inc`
- `src/RegisterAccess.cpp`
- `src/RegisterAccess.hpp`
- `tests/TraceBackend.hpp`
- `tests/chip_identity.cpp`
- `tests/register_access.cpp`
- `tests/stage1.cpp`
- `tools/package_probe.py`
- `tools/verify_kext.py`
- `upstream-manifest.json`

## 2. Diferențe arhitecturale

Core C++14 fără IOKit, cu backend/timing/PCI injectabile. Programe MAC și PHY
separate de VM-ul firmware. Baza OCP este comună între targets PHY/MAC MCU,
conform upstream, și revine la 0xa400 după execuție sau eroare. Backend-ul
IOPCIDevice serializează întreaga probă prin IOCommandGate/IOWorkLoop.

Politica 46 scrie/verifică EEE advertisement 0 la 0xa5d0 și TX-LPI OFF la
ERI 0x1b0[1:0]; 44 nu primește automat această politică. Secvența veche EEE MAC
nu este reintrodusă. Descriptor-independent MAC init este executabil; toate
scrierile către ring addresses și activarea TX/RX sunt blocate de backend.

Nicio implementare DMA/bounce/low-32-bit IOVA nu este prezentă. STAGE 2 este
rezervat native 64-bit DMA + mapper/IOVA macOS.

## 3. Teste

Trei executabile locale, clang++ C++14 cu -Wall -Wextra -Werror:
chip_identity, register_access, stage1. Toate au rezultat PASS. Logul exact
este în build/test-results.txt.

Testele acoperă EPHY/ERI, byte masks/aliniere/timeout, toate opcode-urile,
header/checksum/offset/count și branches, execuție finită, target switch,
baza OCP comună, restaurare PHY, timeout/IOFailure din backend, reset,
ordinea MAC 44/46, EEE/TX-LPI, lipsa ring programming și injectarea unui
failure la fiecare acces al traseelor complete 0x5c8, 0x541 și 0x6c0.

Sanitizers ASan/UBSan au fost încercate; primul executabil ASan a primit SIGILL
în mediul sesiunii. Verificarea finală folosește testele normale, trecute.
Nu se pretinde sanitizer PASS. Nicio compilare CI/GitHub Actions.

## 4. Upstream și funcții încă neportate

Secvențele cerute rtl_hw_init_8168g, rtl_hw_reset, rtl_hw_start_8411_2,
rtl8411b_fix_phy_down, rtl_hw_start_8168h_1, rtl8168g_2_hw_phy_config,
rtl8168h_2_hw_phy_config și VM-ul firmware sunt implementate semantic în
modulele proiectului. Nu există apeluri la funcții Linux.

Nu sunt implementate integral PHYLIB/genphy_config_aneg și descoperirea
capabilităților/advertising, phy_start/phy_state_machine, monitorizarea
continuă link, management complet suspend/resume sau networking. Calea
EEE-disabled/link-up există ca metodă; deocamdată este aplicată la inițializare
și verificare, fără monitor continuu. Wake și soft-reset sunt implementate.

Firmware extern obligatoriu pentru un STAGE 1 hardware PASS complet:
rtl_nic/rtl8411-2.fw, respectiv rtl_nic/rtl8168h-2.fw. Nu a fost furnizat în
această sesiune și nu este inclus. Probe fără firmware = PARTIAL, nu PASS.

## 5. Target macOS

Compilare + link + audit PASS, Xcode 26.1.1, MacKernelSDK pinned. Binarele
sunt Mach-O x86_64 kext. Plist și entry symbols sunt validate; nu există
importuri de runtime userland sau IODMACommand. Importurile kernel/IOPCIFamily
se rezolvă numai la încărcare; compatibilitatea runtime Tahoe nu este dovedită.
Nu s-a încărcat și nu s-a instalat kextul. Rezultatul exact și SHA256 sunt în
build/build-results.txt și build/Release/build-verification.json.

## 6. Următorul pas LOCAL pe ASUS și Dell

În directorul dezarhivat:

```sh
bash TEST_LOCAL.command
bash BUILD_PROBE.command --fetch-sdk
bash COLLECT_LOCAL.command
```

Pentru pachet complet cu firmware furnizat:

```sh
FIRMWARE_DIR="/cale/catre/firmware" bash BUILD_PROBE.command --fetch-sdk
```

Trimite build/local-evidence/local-*.txt și build-results/test-results plus
manifestul firmware. Aceste comenzi nu încarcă drivere și nu modifică EFI,
OpenCore sau DMAR. README.md conține comanda exactă pentru prima încărcare
ulterioară, numai pe placa liberă și fără schimbarea protecțiilor macOS.

Decizia următoare, după preflight: alegerea mediului de probă în care
10ec:8168 nu este deținut de driverul existent și furnizarea firmware-urilor
originale. Abia după aceea facem testul runtime STAGE 1, cu TX/RX OFF.
