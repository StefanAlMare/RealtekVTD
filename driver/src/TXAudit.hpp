// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "DMAProgram.hpp"
namespace realtek_next {
enum class TXDrop:unsigned { Inactive, Size, Padding, Mapping, Submit, Count };
struct TXAudit {
 uint64_t attempts=0,accepted=0,acceptedBytes=0,full=0,gated=0;
 uint64_t drops[unsigned(TXDrop::Count)]{},mapStatus[16]{};
 uint64_t mapCalls=0,mapTotalNS=0,mapMaxNS=0,mapOver1MS=0;
 uint64_t segments[5]{},chainMax=0,lengthMax=0,pendingChecksum=0,checksumFlags=0;
 void drop(TXDrop reason){++drops[unsigned(reason)];}
 uint64_t totalDrops() const {uint64_t total=0;for(auto count:drops)total+=count;return total;}
 void mapped(DMAStatus status,uint64_t ns,unsigned segmentCount,unsigned chainCount,uint64_t length,uint32_t flags){
  ++mapCalls;mapTotalNS+=ns;if(ns>mapMaxNS)mapMaxNS=ns;if(ns>=1000000)++mapOver1MS;
  if(unsigned(status)<16)++mapStatus[unsigned(status)];
  if(segmentCount<=4)++segments[segmentCount];if(chainCount>chainMax)chainMax=chainCount;if(length>lengthMax)lengthMax=length;
  if(flags)++pendingChecksum;checksumFlags|=flags;
 }
};
}
