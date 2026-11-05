// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "ChipIdentity.hpp"
#include "DMAProgram.hpp"
namespace realtek_next {
// P13 A/B variable: exact GU25 identity only. No PCI subsystem/host/MAC-address binding.
constexpr unsigned txDescriptorCountForIdentity(const Identity &id){
 return id.status==ProbeStatus::Supported&&id.chip==Chip::RTL8168GU_Method25&&
        id.xid==0x509&&id.linuxMacVersion==42&&cfgMethod(id.chip)==25?256:kTXDescriptorCount;
}
}
