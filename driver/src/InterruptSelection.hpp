// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <stdint.h>
namespace realtek_next {
struct InterruptSelection { bool found=false;unsigned index=0,count=0;int type=0;uint32_t validIndices=0; };
using InterruptQuery=bool (*)(void *,unsigned,int &);
// Query only indices published by the provider, bounded to 32. MSI/MSI-X share the flag.
inline InterruptSelection selectMessageInterrupt(void *context,unsigned count,uint32_t messageFlag,InterruptQuery query){
 InterruptSelection result;
 if(!query||!messageFlag||count>32)return result;
 for(unsigned i=0;i<count;++i){int type=0;if(!query(context,i,type))continue;
  ++result.count;result.validIndices|=uint32_t(1)<<i;
  if(!result.found&&(uint32_t(type)&messageFlag)){result.found=true;result.index=i;result.type=type;}
 }
 return result;
}
}
