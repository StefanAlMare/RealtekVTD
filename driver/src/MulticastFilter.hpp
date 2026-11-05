// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "RegisterAccess.hpp"
#include <stddef.h>
namespace realtek_next {
// IEEE Ethernet CRC, no final complement; incoming address bits are LSB first.
inline uint32_t multicastCRC(const uint8_t *address) {
 uint32_t crc=0xffffffffU;
 for(unsigned i=0;i<6;++i)for(unsigned bit=0;bit<8;++bit){
  const bool feedback=bool(crc&0x80000000U)^bool(address[i]&(1U<<bit));
  crc<<=1;if(feedback)crc^=0x04c11db7U;
 }
 return crc;
}
inline uint32_t multicastSwap(uint32_t n){
 return (n>>24)|((n>>8)&0xff00U)|((n<<8)&0xff0000U)|(n<<24);
}
enum class MulticastApplied:uint32_t { Unknown, Normal, Selective, Promiscuous, Fallback };
struct MulticastFilter {
 uint64_t mask=0,generation=0,appliedGeneration=0;
 uint32_t addressCount=0,observedLow=0,observedHigh=0,observedConfig=0,readValid=0;
 bool enabled=false,valid=false,fallback=false;
 MulticastApplied applied=MulticastApplied::Unknown;
 bool list(const uint8_t *addresses,uint32_t count){
  if(count&&(!addresses||count>UINT32_MAX/6))return false;
  uint64_t next=0;
  for(uint32_t i=0;i<count;++i){const auto *a=addresses+size_t(i)*6;
   if(!(a[0]&1))return false;
   next|=uint64_t(1)<<(multicastCRC(a)>>26);
  }
  mask=next;addressCount=count;return true;
 }
 void invalidate(){valid=false;fallback=false;applied=MulticastApplied::Unknown;readValid=0;observedLow=observedHigh=observedConfig=0;}
 bool program(RegisterIO &io,uint32_t low,uint32_t high,uint32_t config){
  readValid=0;observedLow=observedHigh=observedConfig=0;
  if(!io.write32(0x0c,high)||!io.write32(0x08,low)||!io.write32(0x44,config))return false;
  if(io.read32(0x08,observedLow))readValid|=1;
  if(io.read32(0x0c,observedHigh))readValid|=2;
  if(io.read32(0x44,observedConfig))readValid|=4;
  // Same RX-config readback mask as the unchanged baseline startNetwork.
  return readValid==7&&observedLow==low&&observedHigh==high&&
   observedConfig!=0xffffffffU&&(observedConfig&0xcf3fU)==(config&0xcf3fU);
 }
 bool apply(RegisterIO &io,bool promiscuous,unsigned macVersion,bool gu24=false){
  invalidate();if(generation!=UINT64_MAX)++generation;
  if(macVersion!=34&&macVersion!=44&&macVersion!=46&&!(gu24&&(macVersion==0||macVersion==42)))return false;
  uint32_t before=0;
  if(io.read32(0x44,before)&&before!=0xffffffffU){
   const uint64_t selected=promiscuous?UINT64_MAX:enabled?mask:0;
   const uint32_t accept=0x0aU|(promiscuous?5U:(enabled&&mask?4U:0U));
   // Linux uses swapped/reversed hash words on PCIe generations including 34/44/46.
   if(program(io,multicastSwap(uint32_t(selected>>32)),multicastSwap(uint32_t(selected)),(before&~0x0fU)|accept)){
    valid=true;appliedGeneration=generation;
    applied=promiscuous?MulticastApplied::Promiscuous:enabled&&mask?MulticastApplied::Selective:MulticastApplied::Normal;
    return true;
   }
  }
  // A fallback is successful only after all three readbacks agree.
  fallback=true;
  if(!io.read32(0x44,before)||before==0xffffffffU)return false;
  if(!program(io,0xffffffffU,0xffffffffU,(before&~0x0fU)|0x0eU|(promiscuous?1U:0U)))return false;
  valid=true;appliedGeneration=generation;applied=MulticastApplied::Fallback;return true;
 }
};
}
