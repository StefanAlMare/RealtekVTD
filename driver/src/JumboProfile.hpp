// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "ChipIdentity.hpp"
#include "CopperAutoneg.hpp"
namespace realtek_next {
constexpr unsigned kJumboMTU=9000,kJumboRXBytes=16383,kJumboTXStride=16384;
// GU25 Jumbo is eligible only for the identified CFG25/MAC42 variant.
// Pinned r8169 rtl_jumbo_max allows 9K on this GMII MAC; this is not HW validation.
constexpr bool gu25JumboIdentity(const Identity &id){
 return id.status==ProbeStatus::Supported&&id.chip==Chip::RTL8168GU_Method25&&
  id.xid==0x509&&id.linuxMacVersion==42&&cfgMethod(id.chip)==25;
}
inline bool jumboIdentityValid(const Identity &id,const CopperCapabilities &caps){
 return id.status==ProbeStatus::Supported&&(id.linuxMacVersion==34||id.linuxMacVersion==44||id.linuxMacVersion==46||gu25JumboIdentity(id))&&
  caps.valid&&caps.autoneg()&&caps.estatusRead&&(caps.estatus&0x3000)!=0;
}
inline bool packetSizeToMTU(uint32_t packetSize,unsigned maximum,unsigned &mtu){
 if(packetSize<1518||packetSize>maximum+18)return false;
 mtu=packetSize-18;return true; // SDK includes the 14-byte header and 4-byte FCS, not VLAN.
}
}
