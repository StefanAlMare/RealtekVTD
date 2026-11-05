// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "ChipIdentity.hpp"
namespace realtek_next {
// Software mbuf admission only. Bootstrap queue remains 256 until identity is known.
constexpr unsigned outputQueueCapacityForIdentity(const Identity &id){
 return id.status==ProbeStatus::Supported&&id.chip==Chip::RTL8168GU_Method25&&
        id.xid==0x509&&id.linuxMacVersion==42&&cfgMethod(id.chip)==25?1024:256;
}
}
