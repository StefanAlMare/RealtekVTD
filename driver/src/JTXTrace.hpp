// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "DMAProgram.hpp"

namespace realtek_next {
// Diagnostic metadata only. No packet payload, heap, MMIO or DMA operations.
struct JTXCopyEvidence {bool attempted=false,copied=false;};
struct JTXPHY {
 uint64_t observedNS=0;uint32_t valid=0;
 uint16_t bmcr=0,bmsrFirst=0,bmsrSecond=0,anar=0,anlpar=0;
 uint8_t raw=0xff,anPending=0,mtuState=0;
};
enum JTXFlags:uint32_t { JTXPublished=1,JTXCompleted=2,JTXCancelled=4,JTXRetained=8,
 JTXRetry=16,JTXRejected=32,JTXKickFailed=64,JTXPayloadSyncAttempted=128,JTXPayloadSynced=256,
 JTXRingSyncAttempted=512,JTXRingSynced=1024,JTXKickAttempted=2048,JTXKickSucceeded=4096 };
struct JTXRecord {
 uint64_t id=0,beginNS=0,mapDoneNS=0,publishNS=0,completeNS=0;
 uint32_t length=0,mtu=0,generation=0,slot=0xffffffffU,stride=0;
 uint32_t first=0xffffffffU,last=0xffffffffU,segments=0;
 uint64_t iova=0,segmentLength=0,iovaEnd=0,arenaOffset=0;
 uint32_t mapStatus=0xffffffffU,submitStatus=0xffffffffU,copyFlags=0;
 uint32_t publishOpts1=0,publishOpts2=0,completeOpts1=0,completeOpts2=0,irqCause=0,flags=0;
 uint16_t etherType=0,innerType=0;uint8_t destination[6]{},ipProtocol=0,icmpType=0xff;
 uint16_t ipID=0,ipLength=0,icmpID=0,icmpSequence=0,udpSource=0,udpDestination=0;
 uint64_t irqAtPublish=0,doorbellsAtPublish=0,irqAtComplete=0,doorbellsAtComplete=0;
 JTXPHY phyBegin,phyPublish,phyComplete;
 uint32_t pendingAtPublish=0;uint64_t cancelNS=0;uint32_t terminal=0;
};
class JTXTrace {
public:
 static constexpr unsigned Capacity=64,HeaderBytes=128,RecordBytes=320;
 static constexpr unsigned WireBytes=HeaderBytes+Capacity*RecordBytes;
 using Clock=uint64_t(*)(void *);
private:
 JTXRecord records[Capacity]{};uint8_t wire[WireBytes]{};
 unsigned count_=0;uint64_t attempts_=0,suppressed_=0,services_=0,doorbells_=0;
 uint64_t failedDoorbells_=0,txErrorIRQs_=0,stops_=0,unsafeStops_=0,revision_=0;
 Clock clock_=nullptr;void *clockContext_=nullptr;JTXPHY phy_;
 JTXRecord *find(uint64_t id){return id&&id<=count_?&records[id-1]:nullptr;}
 static uint16_t be16(const uint8_t *p){return uint16_t((unsigned(p[0])<<8)|p[1]);}
 static void u32(uint8_t *p,unsigned off,uint32_t v){for(unsigned i=0;i<4;++i)p[off+i]=uint8_t(v>>(8*i));}
 static void u64(uint8_t *p,unsigned off,uint64_t v){for(unsigned i=0;i<8;++i)p[off+i]=uint8_t(v>>(8*i));}
 static void u16(uint8_t *p,unsigned off,uint16_t v){p[off]=uint8_t(v);p[off+1]=uint8_t(v>>8);}
 static void encodePHY(uint8_t *p,unsigned off,const JTXPHY &v){
  u64(p,off,v.observedNS);u32(p,off+8,v.valid);u16(p,off+12,v.bmcr);
  u16(p,off+14,v.bmsrFirst);u16(p,off+16,v.bmsrSecond);u16(p,off+18,v.anar);
  u16(p,off+20,v.anlpar);p[off+22]=v.raw;p[off+23]=v.anPending;p[off+24]=v.mtuState;
 }
 static void headers(JTXRecord &r,const uint8_t *bytes,unsigned length,bool valid){
  if(!valid||!bytes||length<14)return;
  for(unsigned i=0;i<6;++i)r.destination[i]=bytes[i];
  r.etherType=be16(bytes+12);r.innerType=r.etherType;
  unsigned offset=14;
  if(r.etherType==0x8100){if(length<18)return;r.innerType=be16(bytes+16);offset=18;}
  // Called with the full private snapshot only; never read beyond its exact length.
  if(r.innerType!=0x0800||length<offset+20)return;
  const uint8_t *ip=bytes+offset;unsigned ihl=unsigned(ip[0]&15)*4;
  if((ip[0]>>4)!=4||ihl<20||length<offset+ihl)return;
  r.ipProtocol=ip[9];r.ipID=be16(ip+4);r.ipLength=be16(ip+2);
  if((be16(ip+6)&0x1fff)||length<offset+ihl+8)return;
  const uint8_t *transport=ip+ihl;
  if(r.ipProtocol==1){r.icmpType=transport[0];r.icmpID=be16(transport+4);r.icmpSequence=be16(transport+6);}
  if(r.ipProtocol==17){r.udpSource=be16(transport);r.udpDestination=be16(transport+2);}
 }
public:
 void setClock(Clock clock,void *context=nullptr){clock_=clock;clockContext_=context;}
 uint64_t now()const{return clock_?clock_(clockContext_):0;}
 unsigned count()const{return count_;}
 uint64_t suppressed()const{return suppressed_;}
 uint64_t revision()const{return revision_;}
 const JTXRecord *record(unsigned i)const{return i<count_?&records[i]:nullptr;}
 void observePHY(const JTXPHY &phy){phy_=phy;}
 uint64_t begin(const uint8_t *header,bool headerOK,unsigned length,unsigned mtu,unsigned generation,
  uint64_t beginNS,uint64_t mapDoneNS,uint32_t mapStatus,const JTXCopyEvidence &copy,
  const DMABuffer *mapped,const uint8_t *snapshot,uint64_t arena,unsigned stride){
  const bool tagged=headerOK&&header&&header[12]==0x81&&header[13]==0;
  if(length<=(tagged?1518U:1514U))return 0;
  ++attempts_;if(count_==Capacity){++suppressed_;return 0;}
  auto &r=records[count_];r.id=++count_;r.length=length;r.mtu=mtu;r.generation=generation;
  r.beginNS=beginNS;r.mapDoneNS=mapDoneNS;r.mapStatus=mapStatus;r.stride=stride;r.phyBegin=phy_;
  r.copyFlags=(headerOK?1U:0U)|(copy.attempted?2U:0U)|(copy.copied?4U:0U);
  // Header-only fallback on a failed snapshot. No IP parsing from an 18-byte prefix.
  headers(r,header,length<18?length:18,headerOK);
  if(mapped&&mapped->mapped&&mapped->segmentCount){
   r.segments=unsigned(mapped->segmentCount);r.iova=mapped->segments[0].address;
   r.segmentLength=mapped->segments[0].length;
   if(r.segmentLength&&r.iova<=UINT64_MAX-(r.segmentLength-1))r.iovaEnd=r.iova+r.segmentLength-1;
   if(stride&&r.iova>=arena){r.arenaOffset=r.iova-arena;
    if(r.arenaOffset%stride==0&&r.arenaOffset/stride<65)r.slot=unsigned(r.arenaOffset/stride);}
   if(snapshot&&mapped->mappedLength>=length){
    bool matches=headerOK;for(unsigned i=0;i<(length<18?length:18)&&matches;++i)matches=header[i]==snapshot[i];
    if(matches)r.copyFlags|=8U;headers(r,snapshot,length,true);
   }
  }
  ++revision_;return r.id;
 }
 void publication(uint64_t id,unsigned first,unsigned last,unsigned segments,uint32_t opts1,uint32_t opts2,unsigned pending){
  auto r=find(id);if(!r)return;r->first=first;r->last=last;r->segments=segments;
  r->publishOpts1=opts1;r->publishOpts2=opts2;r->publishNS=now();r->phyPublish=phy_;
  r->pendingAtPublish=pending;r->irqAtPublish=services_;r->doorbellsAtPublish=doorbells_;
  r->flags|=JTXPublished;++revision_;
 }
 void finish(uint64_t id,uint32_t status,bool consumed,bool retry=false){
  auto r=find(id);if(!r)return;r->submitStatus=status;
  if(!consumed){r->flags|=retry?JTXRetry:JTXRejected;r->terminal=retry?3:4;}
  ++revision_;
 }
 void doorbell(uint64_t id,bool success){
  ++doorbells_;if(!success)++failedDoorbells_;auto r=find(id);
  if(r){r->flags|=JTXKickAttempted|(success?JTXKickSucceeded:JTXKickFailed);++revision_;}
 }
 void synchronized(uint64_t id,bool ring,bool success){auto r=find(id);if(!r)return;
  r->flags|=ring?JTXRingSyncAttempted:JTXPayloadSyncAttempted;
  if(success)r->flags|=ring?JTXRingSynced:JTXPayloadSynced;++revision_;
 }
 void service(uint16_t causes){++services_;if(causes&8)++txErrorIRQs_;}
 void completion(uint64_t id,uint32_t opts1,uint32_t opts2,uint16_t causes){
  auto r=find(id);if(!r)return;r->completeOpts1=opts1;r->completeOpts2=opts2;r->irqCause=causes;
  r->completeNS=now();r->phyComplete=phy_;r->irqAtComplete=services_;r->doorbellsAtComplete=doorbells_;
  r->flags|=JTXCompleted;r->terminal=1;++revision_;
 }
 void stopped(bool safe){++stops_;if(!safe)++unsafeStops_;}
 void cancel(uint64_t id,bool safe,uint32_t opts1,uint32_t opts2){
  auto r=find(id);if(!r)return;r->completeOpts1=opts1;r->completeOpts2=opts2;
  if(safe){r->flags|=JTXCancelled;r->cancelNS=now();r->terminal=2;}
  else r->flags|=JTXRetained;
  ++revision_;
 }
 const uint8_t *encode(unsigned &size){
  // Explicit LE schema; no C++ padding, pointers, uninitialized bytes or payload exported.
  size=HeaderBytes+count_*RecordBytes;for(unsigned i=0;i<size;++i)wire[i]=0;
  wire[0]='J';wire[1]='T';wire[2]='X';wire[3]='1';u32(wire,4,1);u32(wire,8,HeaderBytes);
  u32(wire,12,RecordBytes);u32(wire,16,Capacity);u32(wire,20,count_);u32(wire,24,count_==Capacity);
  const uint64_t values[]={attempts_,suppressed_,services_,doorbells_,failedDoorbells_,txErrorIRQs_,stops_,unsafeStops_,revision_,now()};
  for(unsigned i=0;i<10;++i)u64(wire,32+i*8,values[i]);
  for(unsigned n=0;n<count_;++n){const auto &r=records[n];auto p=wire+HeaderBytes+n*RecordBytes;
   u64(p,0,r.id);u64(p,8,r.beginNS);u64(p,16,r.mapDoneNS);u64(p,24,r.publishNS);u64(p,32,r.completeNS);
   const uint32_t a[]={r.length,r.mtu,r.generation,r.slot,r.stride,r.first,r.last,r.segments};
   for(unsigned i=0;i<8;++i)u32(p,40+i*4,a[i]);
   const uint64_t b[]={r.iova,r.segmentLength,r.iovaEnd,r.arenaOffset};for(unsigned i=0;i<4;++i)u64(p,72+i*8,b[i]);
   const uint32_t c[]={r.mapStatus,r.submitStatus,r.copyFlags,r.publishOpts1,r.publishOpts2,r.completeOpts1,r.completeOpts2,r.irqCause,r.flags};
   for(unsigned i=0;i<9;++i)u32(p,104+i*4,c[i]);
   u16(p,140,r.etherType);u16(p,142,r.innerType);for(unsigned i=0;i<6;++i)p[144+i]=r.destination[i];
   p[150]=r.ipProtocol;p[151]=r.icmpType;
   const uint16_t d[]={r.ipID,r.ipLength,r.icmpID,r.icmpSequence,r.udpSource,r.udpDestination};for(unsigned i=0;i<6;++i)u16(p,152+i*2,d[i]);
   const uint64_t e[]={r.irqAtPublish,r.doorbellsAtPublish,r.irqAtComplete,r.doorbellsAtComplete};for(unsigned i=0;i<4;++i)u64(p,168+i*8,e[i]);
   encodePHY(p,200,r.phyBegin);encodePHY(p,232,r.phyPublish);encodePHY(p,264,r.phyComplete);
   u32(p,296,r.pendingAtPublish);u64(p,304,r.cancelNS);u32(p,312,r.terminal);
  }
  return wire;
 }
};
}
