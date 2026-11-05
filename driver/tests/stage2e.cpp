// SPDX-License-Identifier: GPL-2.0-only
#include "TraceBackend.hpp"
#include "../src/Stage2EProgram.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace realtek_next;

namespace {
struct Holder { std::vector<uint8_t> bytes; explicit Holder(size_t n):bytes(n){} };

class RecycleDMA final : public DMAProvider {
public:
 bool mapper=true,busMaster=false,multiBuffer=false,failBusMasterOff=false;
 unsigned bufferAllocations=0,failAllocation=0;
 unsigned ringPolls=0,maxCycles=2,invalidCycle=99,invalidIndex=99;
 uint64_t txRingIOVA=0x0000001200200000ULL;
 uint64_t rxRingIOVA=0x0000003400300000ULL;
 uint64_t nextBufferIOVA=0x0000007800400000ULL;
 uint32_t hardwareCycles[4]{};

 bool deviceMapperPresent() const override{return mapper;}
 static bool txRing(const DMABuffer &b){return b.requestedLength==kTXRingLength;}
 static bool rxRing(const DMABuffer &b){return b.requestedLength==kRXRingLength;}
 static bool packetBuffer(const DMABuffer &b){return b.requestedLength==kStage2ERXBufferLength;}
 DMAStatus allocate(DMABuffer &b,uint64_t n,uint32_t,DMADirection direction)override{
  if(n==kStage2ERXBufferLength){assert(direction==DMADirection::FromDevice);++bufferAllocations;if(failAllocation==bufferAllocations)return DMAStatus::AllocationFailed;}
  b.opaque=new Holder(size_t(n));b.requestedLength=n;b.allocated=true;return DMAStatus::Ok;
 }
 DMAStatus prepare(DMABuffer &b)override{b.prepared=true;return DMAStatus::Ok;}
 DMAStatus map(DMABuffer &b)override{
  b.mapped=true;b.mappedLength=b.requestedLength;
  b.segmentCount=packetBuffer(b)&&multiBuffer?2:1;
  const uint64_t address=txRing(b)?txRingIOVA:rxRing(b)?rxRingIOVA:
      nextBufferIOVA+uint64_t(bufferAllocations-1)*0x10000;
  b.segments[0]={address,b.segmentCount==1?b.requestedLength:b.requestedLength/2};
  if(b.segmentCount==2)b.segments[1]={address+b.segments[0].length,b.requestedLength-b.segments[0].length};
  return DMAStatus::Ok;
 }
 DMAStatus zero(DMABuffer &b)override{std::memset(bytes(b),0,size_t(b.requestedLength));return DMAStatus::Ok;}
 void *bytes(DMABuffer &b)override{return b.opaque?static_cast<Holder*>(b.opaque)->bytes.data():nullptr;}
 DMAStatus syncForDevice(DMABuffer &)override{return DMAStatus::Ok;}
 DMAStatus syncForCPU(DMABuffer &b)override{
  if(rxRing(b)){
   ++ringPolls;auto *d=static_cast<uint32_t*>(bytes(b));
   for(unsigned i=0;i<4;++i){
    if((d[i*4]&(1U<<31))&&hardwareCycles[i]<maxCycles){
     ++hardwareCycles[i];uint32_t opts=(1U<<29)|(1U<<28)|(64+hardwareCycles[i]+i);
     if(i==3)opts|=1U<<30;
     if(hardwareCycles[i]==invalidCycle&&i==invalidIndex)opts|=1U<<21;
     d[i*4]=opts;
    }
   }
  }
  return DMAStatus::Ok;
 }
 void memoryBarrier()override{}
 void unmap(DMABuffer &b)override{b.mapped=false;b.segmentCount=0;b.mappedLength=0;}
 void complete(DMABuffer &b)override{b.prepared=false;}
 void release(DMABuffer &b)override{delete static_cast<Holder*>(b.opaque);b=DMABuffer{};}
 DMAStatus setBusMaster(bool enable)override{if(!enable&&failBusMasterOff)return DMAStatus::BusMasterFailed;busMaster=enable;return DMAStatus::Ok;}
};

void prepare(TraceBackend &io,RecycleDMA &dma,Stage2AResult &a,Stage2DResult &d){
 a=runStage2A(io,dma);assert(a.complete&&a.busMasterEnabled);d.complete=true;d.status=Stage2DStatus::Ok;
}

void successHighIOVA(){
 TraceBackend io;RecycleDMA dma;Stage2AResult a;Stage2DResult d;prepare(io,dma,a,d);
 auto r=runStage2E(io,dma,a,d);
 assert(r.status==Stage2EStatus::Ok&&r.complete&&r.packetDMAExercised);
 assert(r.completedCount==8&&r.rearmedCount==4);
 assert(r.firstCycleMask==15&&r.secondCycleMask==15&&r.descriptorValidMask==15);
 assert(r.chipCommandBeforeRX==0&&r.chipCommandDuringRX==8&&r.chipCommandAfterRX==0);
 for(unsigned i=0;i<4;++i){
  assert(r.bufferIOVA[i]>UINT32_MAX&&r.bufferSegmentCount[i]==1);
  assert(r.completionCount[i]==2&&r.rearmCount[i]==1);
  assert(r.firstReceivedLength[i]==65+i&&r.secondReceivedLength[i]==66+i);
  assert(!r.lastDescriptorErrorBits[i]);
 }
 assert(!r.timeout&&!r.unsafeMappingRetained&&!r.trafficEnabled);
 assert(dma.busMaster&&a.busMasterEnabled);teardownStage2A(io,dma,a);
}

void failures(){
 {TraceBackend io;RecycleDMA x;Stage2AResult a;Stage2DResult d;prepare(io,x,a,d);d.complete=false;
  auto r=runStage2E(io,x,a,d);assert(r.status==Stage2EStatus::Stage2DIncomplete);teardownStage2A(io,x,a);}
 {TraceBackend io;RecycleDMA x;Stage2AResult a;Stage2DResult d;prepare(io,x,a,d);x.failAllocation=3;
  auto r=runStage2E(io,x,a,d);assert(r.status==Stage2EStatus::BufferAllocationFailed);teardownStage2A(io,x,a);}
 {TraceBackend io;RecycleDMA x;Stage2AResult a;Stage2DResult d;prepare(io,x,a,d);x.multiBuffer=true;
  auto r=runStage2E(io,x,a,d);assert(r.status==Stage2EStatus::BufferSegmentInvalid);teardownStage2A(io,x,a);}
 {TraceBackend io;RecycleDMA x;Stage2AResult a;Stage2DResult d;prepare(io,x,a,d);x.invalidCycle=2;x.invalidIndex=2;
  auto r=runStage2E(io,x,a,d);assert(r.status==Stage2EStatus::RXDescriptorInvalid);assert(x.busMaster&&a.busMasterEnabled);teardownStage2A(io,x,a);}
}

void timeoutSafety(){
 {TraceBackend io;RecycleDMA x;Stage2AResult a;Stage2DResult d;prepare(io,x,a,d);x.maxCycles=1;
  auto r=runStage2E(io,x,a,d);assert(r.status==Stage2EStatus::RXTimeout&&r.timeout&&r.completedCount==4&&r.rearmedCount==4);assert(!x.busMaster&&!a.busMasterEnabled&&!r.unsafeMappingRetained);teardownStage2A(io,x,a);}
 {TraceBackend io;RecycleDMA x;Stage2AResult a;Stage2DResult d;prepare(io,x,a,d);x.maxCycles=0;x.failBusMasterOff=true;
  auto r=runStage2E(io,x,a,d);assert(r.status==Stage2EStatus::SafeQuiesceFailed&&r.unsafeMappingRetained);x.failBusMasterOff=false;assert(teardownStage2E(io,x,a,r));teardownStage2A(io,x,a);}
}
}

class DiagnosticBackend : public TraceBackend {
public:
 bool failCommandRead=false, reportDisabled=false;
 bool readConfig16(uint16_t offset,uint16_t &value) override {
  if(offset==4 && failCommandRead)return false;
  if(offset==4 && reportDisabled){value=2;return true;}
  return TraceBackend::readConfig16(offset,value);
 }
};
void diagnosticTimeoutSnapshots(){
 for(unsigned mode=0;mode<3;++mode){
  DiagnosticBackend io;RecycleDMA dma;Stage2AResult a;Stage2DResult d;
  prepare(io,dma,a,d);io.pci[4]=6;io.mmio[0x6c]=2;
  io.failCommandRead=mode==1;io.reportDisabled=mode==2;dma.maxCycles=0;
  auto r=runStage2E(io,dma,a,d,&io);
  assert(r.timeout && r.completedCount==0 && r.rearmedCount==0);
  assert(r.pollCount==30000 && r.diagnosticSamples==303);
  assert(r.chipCommandAtStart==8 && r.chipCommandLast==8 && r.chipCommandAfterRX==0);
  assert(r.ownMaskLast==15 && r.firstCompletionPoll==0 && !r.rxCommandLostCount);
  for(unsigned i=0;i<4;++i){
   assert(r.lastObservedOpts1[i]&(1U<<31));
   assert(r.lastObservedAddress[i]==r.bufferIOVA[i]);
  }
  assert(r.pciCommandObserved==(mode!=1));
  assert(r.diagnosticReadFailures==(mode==1?303U:0U));
  assert(r.busMasterLostCount==(mode==2?303U:0U));
  assert(!dma.busMaster && !a.busMasterEnabled);
  teardownStage2A(io,dma,a);
 }
}

void integratedSession(){
 for(unsigned cycles : {0U,1U,2U}){
  TraceBackend io;RecycleDMA dma;Stage2AResult a;Stage2DResult unused;
  prepare(io,dma,a,unused);Stage2CResult c;c.complete=true;dma.maxCycles=cycles;
  auto r=runStage2EFromC(io,dma,a,c);
  assert(r.completedCount==cycles*4 && r.rearmedCount==(cycles?4U:0U));
  assert(r.firstCycleMask==(cycles?15U:0U));
  assert(r.firstCycleCompletePoll==(cycles?1U:0U));
  assert(r.complete==(cycles==2));
  unsigned enables=0,stops=0;bool enabled=false;
  for(const auto &e:io.events)if(e.kind=="w8" && e.address==0x37){
   if(e.value==8){++enables;enabled=true;}
   if(e.value==0 && enabled)++stops;
  }
  assert(enables==1 && stops==1);
  teardownStage2A(io,dma,a);
 }
 TraceBackend io;RecycleDMA dma;Stage2AResult a;Stage2DResult unused;
 prepare(io,dma,a,unused);Stage2CResult c;
 auto r=runStage2EFromC(io,dma,a,c);
 assert(r.status==Stage2EStatus::Stage2CIncomplete && !dma.bufferAllocations);
 teardownStage2A(io,dma,a);
}

int main(){integratedSession();diagnosticTimeoutSnapshots();successHighIOVA();failures();timeoutSafety();std::puts("PASS: STAGE2E two-cycle RX descriptor recycling, ring wrap, 64-bit IOVA and ownership-safe teardown");}
