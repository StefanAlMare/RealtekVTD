// SPDX-License-Identifier: GPL-2.0-only
#include "TraceBackend.hpp"
#include "../src/Stage3AProgram.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace realtek_next;
namespace {
struct Holder { std::vector<uint8_t> bytes; explicit Holder(size_t n):bytes(n){} };
class DuplexDMA final : public DMAProvider {
public:
 TraceBackend *io=nullptr;
 Holder *txPackets=nullptr;uint64_t txPacketIOVA=0;
 uint32_t observedSequences=0, hardwareTXCount[8]{};
 bool mapper=true,busMaster=false,multiBuffer=false,failBusMasterOff=false;
 bool stallTX=false; unsigned txCPUReads=0,txSpacing=1,rxSpacing=100,totalAllocations=0;
 unsigned bufferAllocations=0,failAllocation=0;
 unsigned ringPolls=0,maxCycles=16,invalidCycle=99,invalidIndex=99;
 uint64_t txRingIOVA=0x0000001200200000ULL;
 uint64_t rxRingIOVA=0x0000003400300000ULL;
 uint64_t nextBufferIOVA=0x0000007800400000ULL;
 uint32_t hardwareCycles[4]{};

 bool deviceMapperPresent() const override{return mapper;}
 static bool txRing(const DMABuffer &b){return b.requestedLength==kTXRingLength;}
 static bool rxRing(const DMABuffer &b){return b.requestedLength==kRXRingLength;}
 static bool packetBuffer(const DMABuffer &b){return b.requestedLength==kStage3ARXBufferLength;}
 DMAStatus allocate(DMABuffer &b,uint64_t n,uint32_t,DMADirection direction)override{
  if(n==kStage3ARXBufferLength){assert(direction==DMADirection::FromDevice);++bufferAllocations;if(failAllocation==bufferAllocations)return DMAStatus::AllocationFailed;}
  ++totalAllocations; b.opaque=new Holder(size_t(n));b.requestedLength=n;b.allocated=true;if(n==480)txPackets=static_cast<Holder*>(b.opaque);return DMAStatus::Ok;
 }
 DMAStatus prepare(DMABuffer &b)override{b.prepared=true;return DMAStatus::Ok;}
 DMAStatus map(DMABuffer &b)override{
  b.mapped=true;b.mappedLength=b.requestedLength;
  b.segmentCount=packetBuffer(b)&&multiBuffer?2:1;
  const uint64_t address=txRing(b)?txRingIOVA:rxRing(b)?rxRingIOVA:
      nextBufferIOVA+uint64_t(totalAllocations-1)*0x10000;
  if(b.requestedLength==480)txPacketIOVA=address;
  b.segments[0]={address,b.segmentCount==1?b.requestedLength:b.requestedLength/2};
  if(b.segmentCount==2)b.segments[1]={address+b.segments[0].length,b.requestedLength-b.segments[0].length};
  return DMAStatus::Ok;
 }
 DMAStatus zero(DMABuffer &b)override{std::memset(bytes(b),0,size_t(b.requestedLength));return DMAStatus::Ok;}
 void *bytes(DMABuffer &b)override{return b.opaque?static_cast<Holder*>(b.opaque)->bytes.data():nullptr;}
 DMAStatus syncForDevice(DMABuffer &)override{return DMAStatus::Ok;}
 DMAStatus syncForCPU(DMABuffer &b)override{
  if(txRing(b)){
   ++txCPUReads;auto *d=static_cast<uint32_t*>(bytes(b));
   if(!stallTX && txCPUReads%txSpacing==0)for(unsigned i=0;i<8;++i)if(d[i*4]&(1U<<31)){
    assert(txPackets);unsigned seq=txPackets->bytes[i*60+43];
    assert(seq<32 && seq%8==i);assert(!(observedSequences&(1U<<seq)));
    assert(((uint64_t(d[i*4+3])<<32)|d[i*4+2])==txPacketIOVA+i*60);
    assert(bool(d[i*4]&(1U<<30))==(i==7));
    observedSequences|=1U<<seq;++hardwareTXCount[i];d[i*4]&=~(1U<<31);
   }
  }
  if(rxRing(b)){
   ++ringPolls;auto *d=static_cast<uint32_t*>(bytes(b));
   for(unsigned i=0;i<4;++i){
    if(ringPolls%rxSpacing==0 && (d[i*4]&(1U<<31))&&hardwareCycles[i]<maxCycles){
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
 void release(DMABuffer &b)override{if(busMaster&&io)assert(!(io->mmio[0x37]&12));delete static_cast<Holder*>(b.opaque);b=DMABuffer{};}
 DMAStatus setBusMaster(bool enable)override{if(!enable&&failBusMasterOff)return DMAStatus::BusMasterFailed;busMaster=enable;return DMAStatus::Ok;}
};

class DuplexIO : public TraceBackend {
public:
 bool txError=false;
 bool read16(uint16_t off,uint16_t &value) override {
  if(off==0x3e && txError && mmio[0x37]==12){value=8;return true;}
  return TraceBackend::read16(off,value);
 }
 bool write16(uint16_t off,uint16_t value) override {
  if(off==0x3e){mmio[off]&=~value;return true;}
  return TraceBackend::write16(off,value);
 }
};

void test(unsigned mode){
 DuplexIO io;DuplexDMA dma;dma.io=&io;io.mmio[0]=0x12345602;io.mmio[4]=0xabcd;
 auto a=runStage2A(io,dma);dma.rxSpacing=1;
 auto r=beginStage3A(io,dma,a);assert(r.running&&!r.complete);
 const auto elapsed=io.elapsed;
 if(mode==1)dma.stallTX=true;
 if(mode==2)dma.maxCycles=0;
 if(mode==3){dma.maxCycles=0;dma.failBusMasterOff=true;}
 if(mode==4)io.txError=true;
 for(unsigned i=0;i<40 && r.running;++i){
  scheduleStage3ATX(io,dma,a,r);
  // Scheduler does not collect completions, even if device owns descriptors.
  if(i==0)assert(r.txCompleted==0 && r.completedCount==0);
  processStage3AInterrupt(io,dma,a,r,5);
 }
 assert(io.elapsed==elapsed); // No polling delays in event processing.
 if(mode==0){assert(r.complete && !r.running && r.interruptActions>0);
  assert(r.txCompleted==32&&r.completedCount==64&&r.txRearmed==24&&r.rearmedCount==60);
  assert(r.chipCommandAfterRX==0);}
 else{
  abortStage3A(io,dma,a,r);assert(!r.complete&&!r.running);
  if(mode==3){assert(r.unsafeMappingRetained);dma.failBusMasterOff=false;assert(teardownStage3A(io,dma,a,r));}
 }
 teardownStage2A(io,dma,a);
}
void noInterrupt(){
 DuplexIO io;DuplexDMA dma;dma.io=&io;io.mmio[0]=0x12345602;io.mmio[4]=0xabcd;
 auto a=runStage2A(io,dma);auto r=beginStage3A(io,dma,a);
 for(unsigned i=0;i<40;++i)scheduleStage3ATX(io,dma,a,r);
 assert(r.txSubmitted==8&&r.txCompleted==0&&r.completedCount==0&&r.interruptActions==0);
 abortStage3A(io,dma,a,r);assert(!r.running&&r.timeout);teardownStage2A(io,dma,a);
}
}
int main(){for(unsigned i=0;i<5;++i)test(i);noInterrupt();
 puts("PASS: STAGE3A IRQ-only completions, timer submission, no polling sleeps, deadline and safe retention");}
