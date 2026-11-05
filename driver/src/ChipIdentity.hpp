// SPDX-License-Identifier: GPL-2.0-only
// Hardware identification: pinned Linux r8169 behavior; see SOURCES.md.
#pragma once
#include <stdint.h>

namespace realtek_next {
enum class Chip { Unknown, RTL8411B, RTL8168H, RTL8168M, RTL8168EVL, RTL8168GU_Method24, RTL8168GU_Method25 };
enum class ProbeStatus { Supported, WrongPCI, ReadFailed, UnsupportedRevision, RequiresGigabitPHY };
struct Identity {
    ProbeStatus status;
    Chip chip;
    uint16_t xid;
    uint8_t linuxMacVersion;
};

// txConfig must be a host-endian 32-bit MMIO read at offset 0x40.
// This pure function performs no PCI, MMIO, PHY or DMA operation.
constexpr Identity identify(uint16_t vendor, uint16_t device,
                            uint32_t txConfig, bool gigabitPHY) {
    const uint16_t xid = static_cast<uint16_t>((txConfig >> 20) & 0xfcfU);
    if (vendor != 0x10ec || device != 0x8168)
        return {ProbeStatus::WrongPCI, Chip::Unknown, xid, 0};
    if (txConfig == 0xffffffffU)
        return {ProbeStatus::ReadFailed, Chip::Unknown, xid, 0};
    // Linux family rule, not one board's PCI revision, subsystem or MAC address.
    if ((xid & 0x7c8U) == 0x2c8U)
        return {ProbeStatus::Supported, Chip::RTL8168EVL, xid, 34};
    // Pinned Mieze/r8168: family 0x50800000 with ICVerID == 0 is CFG24.
    // CFG24 remains distinct from CFG25 and unknown ICVerID.
    // Subsystem IDs and the Ethernet MAC address are not acceptance criteria.
    if ((txConfig & 0x7cf00000U) == 0x50800000U)
        return {ProbeStatus::Supported, Chip::RTL8168GU_Method24, xid, 0};
    // Mieze family 0x508, ICVerID 1; Linux XID 0x509 => MAC_VER_42.
    // Other ICVerID values remain rejected instead of Mieze's unknown-revision fallback.
    if ((txConfig & 0x7cf00000U) == 0x50900000U) {
        if (!gigabitPHY)
            return {ProbeStatus::RequiresGigabitPHY, Chip::Unknown, xid, 0};
        return {ProbeStatus::Supported, Chip::RTL8168GU_Method25, xid, 42};
    }
    const uint16_t revision = xid & 0x7cfU;
    if (revision == 0x5c8)
        return {ProbeStatus::Supported, Chip::RTL8411B, xid, 44};
    if (revision == 0x541 || revision == 0x6c0) {
        // Linux redirects MAC_VER_46 with a non-GMII PHY to RTL8107e.
        if (!gigabitPHY)
            return {ProbeStatus::RequiresGigabitPHY, Chip::Unknown, xid, 0};
        return {ProbeStatus::Supported,
                revision == 0x541 ? Chip::RTL8168H : Chip::RTL8168M, xid, 46};
    }
    return {ProbeStatus::UnsupportedRevision, Chip::Unknown, xid, 0};
}
constexpr bool isGUProfile(Chip chip) {
    return chip==Chip::RTL8168GU_Method24||chip==Chip::RTL8168GU_Method25;
}
constexpr unsigned cfgMethod(Chip chip) {
    return chip==Chip::RTL8168GU_Method24?24:chip==Chip::RTL8168GU_Method25?25:0;
}
} // namespace realtek_next
