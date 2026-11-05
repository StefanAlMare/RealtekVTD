# Licensing and third-party notices

## Driver

RealtekVTDNext is distributed under **GPL-2.0-only**, as declared by the original source SPDX notices and `driver/SOURCES.md`. The complete GPLv2 text is retained in root `LICENSE` and `driver/LICENSE`.

The owner wants use and further development to be free. This distribution preserves those rights through the existing license; it does not replace GPLv2 with MIT or add a personal-use-only restriction. Use, study, modification and redistribution, including commercial activity, remain subject to GPLv2's conditions and existing notices. A private repository is an access decision, not a separate proprietary license.

## Realtek firmware

`driver/firmware/LICENSE.r8169` and `driver/firmware/provenance.json` are retained. Realtek firmware has its own distribution notice and must not be described as newly licensed GPL source. Retain its notice when redistributing the supplied firmware or kext that embeds it.

## SDK and headers

The pinned `dependencies/MacKernelSDK/` tree is vendored for a reproducible build input. Its `LICENSE.txt` and original per-file notices apply separately; the tree includes Apple Public Source License2.0 material and other original notices. It is not relicensed as GPL by being included here.

## Attribution

- Realtek Semiconductor and the original Realtek contributors.
- ShuChen, François Romieu and Linux r8169 / Realtek PHY contributors.
- Mieze / RealtekRTL8111 and contributors for reviewed controller-specific references.
- acidanthera / MacKernelSDK and the original Apple/other header copyright holders.

No affiliation, endorsement, Apple signing or notarization is claimed. See `docs/ORIGINS.md` for pinned source references.
