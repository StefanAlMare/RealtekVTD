// SPDX-License-Identifier: GPL-2.0-only
#include "../src/ChipIdentity.hpp"
#include <assert.h>
#include <stdio.h>
using namespace realtek_next;
int main() {
    struct Vector { unsigned xid; Chip chip; unsigned mac; };
    const Vector vectors[] = {{0x5c8, Chip::RTL8411B, 44},
                              {0x2c9, Chip::RTL8168EVL, 34},
                              {0x541, Chip::RTL8168H, 46},
                              {0x6c0, Chip::RTL8168M, 46}};
    for (const auto &v : vectors) {
        // Exhaust all low TxConfig bits, which must not affect identity.
        for (unsigned low = 0; low < (1U << 20); ++low) {
            const auto r = identify(0x10ec, 0x8168, (v.xid << 20) | low, true);
            assert(r.status == ProbeStatus::Supported && r.chip == v.chip);
            assert(r.xid == v.xid && r.linuxMacVersion == v.mac);
        }
        const auto masked = identify(0x10ec, 0x8168, (v.xid | 0x800U) << 20, true);
        assert(masked.status == ProbeStatus::Supported && masked.chip == v.chip);
    }
    assert(identify(0x10ec, 0x8168, 0xffffffffU, true).status == ProbeStatus::ReadFailed);
    assert(identify(0x8086, 0x8168, 0x54100000U, true).status == ProbeStatus::WrongPCI);
    assert(identify(0x10ec, 0x8125, 0x54100000U, true).status == ProbeStatus::WrongPCI);
    assert(identify(0x10ec, 0x8168, 0x54100000U, false).status == ProbeStatus::RequiresGigabitPHY);
    assert(identify(0x10ec, 0x8168, 0x6c000000U, false).status == ProbeStatus::RequiresGigabitPHY);
    // Every other masked XID must be rejected, with no fallback to an H chip.
    for (unsigned xid = 0; xid < 4096; ++xid) {
        const unsigned masked = xid & 0x7cfU;
        if ((xid&0x7c8)==0x2c8 || masked == 0x508 || masked == 0x509 || masked == 0x5c8 || masked == 0x541 || masked == 0x6c0) continue;
        assert(identify(0x10ec, 0x8168, xid << 20, true).status == ProbeStatus::UnsupportedRevision);
    }
    puts("PASS: chip identities, mask, irrelevant bits, failed reads, PCI and PHY gates");
}
