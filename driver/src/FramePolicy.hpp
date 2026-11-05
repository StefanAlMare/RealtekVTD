// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace realtek_next {
// Length excludes FCS. Only a single in-band 802.1Q tag grants four extra bytes.
// Caller provides at least min(length,18) accessible header bytes.
inline bool frameLengthValid(const uint8_t *header,size_t length,unsigned mtu=1500){
 if(!header||length<14)return false;
 const bool tagged=header[12]==0x81&&header[13]==0x00;
 const unsigned overhead=tagged?18:14;
 return length>=overhead&&length-overhead<=mtu;
}
constexpr unsigned maxFrameBytes(unsigned mtu){return mtu+18;}
}
