// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <stdint.h>
#include <stddef.h>
namespace realtek_next {
// Diagnostic only. Caller holds the existing controller lock. No addresses,
// payload, tag values, allocation, logging, MMIO, new locks or packet mutation.
struct VlanTSOFailure {
 enum Site:unsigned {InputBounds,HeaderCopy,PlanParse,AggregateLimit,SingleSnapshot,HardwareSnapshot,SingleSubmit,SinglePostOwn,HardwarePostOwn,PumpSnapshot,PumpSubmit,SiteCount};
 static constexpr unsigned Words=26,PerSite=2;
 struct Shape {uint64_t valid=0,available=0,outer=0,tags=0,link=0,version=0,ipBytes=0,ipHeader=0,tcp=0,tcpHeader=0,tcpFlags=0,fragment=0;};
 uint64_t total=0,stored=0,suppressed=0,counts[SiteCount]{};
 uint64_t samples[SiteCount][PerSite][Words]{};
 static uint16_t get(const uint8_t*p){return uint16_t(uint16_t(p[0])<<8|p[1]);}
 // Reads only bytes already captured by the existing path, never the payload.
 // Out-of-band VLAN metadata is NOT queried: absent validity is not absence.
 static Shape inspect(const uint8_t*b,size_t available){
  Shape s;s.available=available;if(!b||available<14)return s;
  size_t off=14;unsigned type=get(b+12);s.outer=type;s.valid=1;
  for(unsigned i=0;i<2&&(type==0x8100||type==0x88a8);++i){if(off+4>available)return s;type=get(b+off+2);off+=4;++s.tags;}
  s.link=off;s.valid|=2;
  if(type==0x800&&off+20<=available){s.version=b[off]>>4;s.ipBytes=get(b+off+2);s.ipHeader=(b[off]&15U)*4;s.fragment=get(b+off+6);s.valid|=4;
   if(s.version==4&&s.ipHeader>=20&&off+s.ipHeader+20<=available&&b[off+9]==6){s.tcp=off+s.ipHeader;s.tcpHeader=(b[s.tcp+12]>>4)*4;s.tcpFlags=b[s.tcp+13];s.valid|=8;}
  }else if(type==0x86dd&&off+40<=available){s.version=b[off]>>4;s.ipBytes=40U+get(b+off+4);s.ipHeader=40;s.valid|=4;
   if(s.version==6&&b[off+6]==6&&off+60<=available){s.tcp=off+40;s.tcpHeader=(b[s.tcp+12]>>4)*4;s.tcpFlags=b[s.tcp+13];s.valid|=8;}
  }return s;
 }
 void note(bool eligible,Site site,uint64_t frame,unsigned mss,unsigned mtu,unsigned epoch,unsigned code,unsigned planError,Shape shape,unsigned flags=0,unsigned checksums=0,int copy=-1,bool owned=false,unsigned index=0,unsigned segments=0){
  if(!eligible||unsigned(site)>=SiteCount)return;
  ++total;const uint64_t ordinal=counts[site]++;
  if(ordinal>=PerSite){++suppressed;return;}
  ++stored;auto &r=samples[site][unsigned(ordinal)];
  const uint64_t values[Words]={total,unsigned(site),epoch,frame,mss,mtu,code,planError,flags,checksums,uint64_t(int64_t(copy)),owned,index,segments,shape.valid,shape.available,shape.outer,shape.tags,shape.link,shape.version,shape.ipBytes,shape.ipHeader,shape.tcp,shape.tcpHeader,shape.tcpFlags,shape.fragment};
  for(unsigned k=0;k<Words;++k)r[k]=values[k];
 }
};
}
