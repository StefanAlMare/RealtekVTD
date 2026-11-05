// SPDX-License-Identifier: GPL-2.0-only
#include "TraceBackend.hpp"
#include "../src/Stage2DProgram.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using namespace realtek_next;

namespace {
struct Holder { std::vector<uint8_t> bytes; explicit Holder(size_t n):bytes(n){} };

class BurstDMA final : public DMAProvider {
public:
 bool mapper=true,busMaster=false,multiBurst=false,failBusMasterOff=false;
 unsigned burstAllocations=0,failBurstAllocation=0,failBurstMap=0;
 unsigned ringPolls=0,completeAt=2,completionLimit=4,invalidIndex=99;
 uint64_t txRingIOVA=0x0000001200200000ULL;
 uint64_t rxRingIOVA=0x0000003400300000ULL;
 uint64_t nextBurstIOVA=0x0000007800400000ULL;
 std::vector<std::string> events;

 bool deviceMapperPresent() const override{return mapper;}
 static bool txRing(const DMABuffer &b){return b.requestedLength==kTXRingLength;}
 static bool rxRing(const DMABuffer &b){return b.requestedLength==kRXRingLength;}
 static bool burst(const DMABuffer &b){return b.requestedLength==kStage2DRXBufferLength;}
 const char *which(const DMABuffer &b)const{return txRing(b)?"tx-ring":rxRing(b)?"rx-ring":"burst";}
 DMAStatus allocate(DMABuffer &b,uint64_t n,uint32_t,DMADirection direction)override{
  if(n==kStage2DRXBufferLength){assert(direction==DMADirection::FromDevice);++burstAllocations;if(failBurstAllocation==burstAllocations)return DMAStatus::AllocationFailed;}
  b.opaque=new Holder(size_t(n));b.requestedLength=n;b.allocated=true;events.push_back("allocate");return DMAStatus::Ok;
 }
 DMAStatus prepare(DMABuffer &b)override{b.prepared=true;events.push_back("prepare");return DMAStatus::Ok;}
 DMAStatus map(DMABuffer &b)override{
  if(burst(b)&&failBurstMap==burstAllocations)return DMAStatus::MapFailed;
  b.mapped=true;b.mappedLength=b.requestedLength;b.segmentCount=burst(b)&&multiBurst?2:1;
  uint64_t address=txRing(b)?txRingIOVA:rxRing(b)?rxRingIOVA:nextBurstIOVA+uint64_t(burstAllocations-1)*0x10000;
  b.segments[0]={address,b.segmentCount==1?b.requestedLength:b.requestedLength/2};
  if(b.segmentCount==2)b.segments[1]={address+b.segments[0].length,b.requestedLength-b.segments[0].length};
  events.push_back("map");return DMAStatus::Ok;
 }
 DMAStatus zero(DMABuffer &b)override{std::memset(bytes(b),0,size_t(b.requestedLength));return DMAStatus::Ok;}
 void *bytes(DMABuffer &b)override{return b.opaque?static_cast<Holder*>(b.opaque)->bytes.data():nullptr;}
 DMAStatus syncForDevice(DMABuffer &)override{events.push_back("sync-device");return DMAStatus::Ok;}
 DMAStatus syncForCPU(DMABuffer &b)override{
  events.push_back(std::string("sync-cpu-")+which(b));
  if(rxRing(b)&&completeAt&&++ringPolls>=completeAt){
   auto *d=static_cast<uint32_t*>(bytes(b));
   for(unsigned i=0;i<completionLimit&&i<4;++i){
    uint32_t opts=(1U<<29)|(1U<<28)|(64+i);
    if(i==3)opts|=1U<<30;
    if(i==invalidIndex)opts|=1U<<21;
    d[i*4]=opts;
   }
  }
  return DMAStatus::Ok;
 }
 void memoryBarrier()override{events.push_back("barrier");}
 void unmap(DMABuffer &b)override{b.mapped=false;b.segmentCount=0;b.mappedLength=0;events.push_back("unmap");}
 void complete(DMABuffer &b)override{b.prepared=false;events.push_back("complete");}
 void release(DMABuffer &b)override{delete static_cast<Holder*>(b.opaque);b=DMABuffer{};events.push_back("release");}
 DMAStatus setBusMaster(bool enable)override{if(!enable&&failBusMasterOff)return DMAStatus::BusMasterFailed;busMaster=enable;events.push_back(enable?"bus-on":"bus-off");return DMAStatus::Ok;}
};

void prepare(TraceBackend &io,BurstDMA &dma,Stage2AResult &a,Stage2CResult &c){
 a=runStage2A(io,dma);assert(a.complete&&a.busMasterEnabled);c.complete=true;c.status=Stage2CStatus::Ok;
 dma.events.clear();dma.ringPolls=0;dma.burstAllocations=0;
}

void successHighIOVA(){
 TraceBackend io;BurstDMA dma;Stage2AResult a;Stage2CResult c;prepare(io,dma,a,c);
 auto r=runStage2D(io,dma,a,c);
 assert(r.status==Stage2DStatus::Ok&&r.complete&&r.packetDMAExercised);
 assert(r.completedCount==4&&r.ownershipPublishedMask==15&&r.ownershipClearedMask==15&&r.descriptorValidMask==15);
 assert(r.pollCount==2&&!r.timeout&&!r.unsafeMappingRetained);
 assert(r.chipCommandBeforeRX==0&&r.chipCommandDuringRX==8&&r.chipCommandAfterRX==0);
 assert(r.rxConfig==0x2cf0e||r.rxConfig==0xcf0e);
 for(unsigned i=0;i<4;++i){assert(r.bufferIOVA[i]>UINT32_MAX);assert(r.bufferSegmentCount[i]==1);assert(r.receivedLength[i]==64+i);assert(!r.descriptorErrorBits[i]);}
 assert(!r.trafficEnabled&&dma.busMaster&&a.busMasterEnabled);
 teardownStage2A(io,dma,a);
}

void failures(){
 {TraceBackend io;BurstDMA d;Stage2AResult a;Stage2CResult c;prepare(io,d,a,c);c.complete=false;
  auto r=runStage2D(io,d,a,c);assert(r.status==Stage2DStatus::Stage2CIncomplete);teardownStage2A(io,d,a);}
 {TraceBackend io;BurstDMA d;Stage2AResult a;Stage2CResult c;prepare(io,d,a,c);d.failBurstAllocation=3;
  auto r=runStage2D(io,d,a,c);assert(r.status==Stage2DStatus::BufferAllocationFailed&&!r.ownershipPublishedMask);teardownStage2A(io,d,a);}
 {TraceBackend io;BurstDMA d;Stage2AResult a;Stage2CResult c;prepare(io,d,a,c);d.multiBurst=true;
  auto r=runStage2D(io,d,a,c);assert(r.status==Stage2DStatus::BufferSegmentInvalid&&!r.ownershipPublishedMask);teardownStage2A(io,d,a);}
 {TraceBackend io;BurstDMA d;Stage2AResult a;Stage2CResult c;prepare(io,d,a,c);d.invalidIndex=1;
  auto r=runStage2D(io,d,a,c);assert(r.status==Stage2DStatus::RXDescriptorInvalid&&r.ownershipPublishedMask==15);assert(!d.busMaster&&!a.busMasterEnabled);teardownStage2A(io,d,a);}
}

void timeoutSafety(){
 {TraceBackend io;BurstDMA d;Stage2AResult a;Stage2CResult c;prepare(io,d,a,c);d.completeAt=0;
  auto r=runStage2D(io,d,a,c);assert(r.status==Stage2DStatus::RXTimeout&&r.timeout&&r.pollCount==kStage2DRXPollAttempts);assert(!d.busMaster&&!a.busMasterEnabled&&!r.unsafeMappingRetained);teardownStage2A(io,d,a);}
 {TraceBackend io;BurstDMA d;Stage2AResult a;Stage2CResult c;prepare(io,d,a,c);d.completeAt=1;d.completionLimit=2;
  auto r=runStage2D(io,d,a,c);assert(r.status==Stage2DStatus::RXTimeout&&r.completedCount==2&&r.ownershipClearedMask==3);assert(!d.busMaster&&!a.busMasterEnabled);teardownStage2A(io,d,a);}
 {TraceBackend io;BurstDMA d;Stage2AResult a;Stage2CResult c;prepare(io,d,a,c);d.completeAt=0;d.failBusMasterOff=true;
  auto r=runStage2D(io,d,a,c);assert(r.status==Stage2DStatus::SafeQuiesceFailed&&r.unsafeMappingRetained);d.failBusMasterOff=false;assert(teardownStage2D(io,d,a,r));teardownStage2A(io,d,a);}
}
}

int main(){successHighIOVA();failures();timeoutSafety();std::puts("PASS: STAGE2D four-buffer RX burst, 64-bit IOVA, bounded polling and ownership-safe teardown");}
