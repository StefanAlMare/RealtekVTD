// SPDX-License-Identifier: GPL-2.0-only
#include "TraceBackend.hpp"
#include "../src/Stage2FProgram.hpp"
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
 bool mapper=true,busMaster=false,multiBuffer=false,failBusMasterOff=false;
 bool stallTX=false; unsigned txCPUReads=0,rxSpacing=100,totalAllocations=0;
 unsigned bufferAllocations=0,failAllocation=0;
 unsigned ringPolls=0,maxCycles=4,invalidCycle=99,invalidIndex=99;
 uint64_t txRingIOVA=0x0000001200200000ULL;
 uint64_t rxRingIOVA=0x0000003400300000ULL;
 uint64_t nextBufferIOVA=0x0000007800400000ULL;
 uint32_t hardwareCycles[4]{};

 bool deviceMapperPresent() const override{return mapper;}
 static bool txRing(const DMABuffer &b){return b.requestedLength==kTXRingLength;}
 static bool rxRing(const DMABuffer &b){return b.requestedLength==kRXRingLength;}
 static bool packetBuffer(const DMABuffer &b){return b.requestedLength==kStage2FRXBufferLength;}
 DMAStatus allocate(DMABuffer &b,uint64_t n,uint32_t,DMADirection direction)override{
  if(n==kStage2FRXBufferLength){assert(direction==DMADirection::FromDevice);++bufferAllocations;if(failAllocation==bufferAllocations)return DMAStatus::AllocationFailed;}
  ++totalAllocations; b.opaque=new Holder(size_t(n));b.requestedLength=n;b.allocated=true;return DMAStatus::Ok;
 }
 DMAStatus prepare(DMABuffer &b)override{b.prepared=true;return DMAStatus::Ok;}
 DMAStatus map(DMABuffer &b)override{
  b.mapped=true;b.mappedLength=b.requestedLength;
  b.segmentCount=packetBuffer(b)&&multiBuffer?2:1;
  const uint64_t address=txRing(b)?txRingIOVA:rxRing(b)?rxRingIOVA:
      nextBufferIOVA+uint64_t(totalAllocations-1)*0x10000;
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
   if(!stallTX)for(unsigned i=0;i<8;++i)if(d[i*4]&(1U<<31))d[i*4]&=~(1U<<31);
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
void exercise(unsigned mode){
 DuplexIO io;DuplexDMA dma;dma.io=&io;io.mmio[0]=0x12345602;io.mmio[4]=0xabcd;
 auto a=runStage2A(io,dma);assert(a.complete);
 if(mode==4)io.txError=true;
 if(mode==1)dma.maxCycles=0;
 if(mode==2)dma.stallTX=true;
 if(mode==3){dma.maxCycles=0;dma.failBusMasterOff=true;}
 auto r=runStage2FFromA(io,dma,a);
 if(mode==0){
  assert(r.complete && r.txCompleted==8 && r.txSubmitted==8 && r.txCompletionMask==255);
  assert(r.completedCount==16 && r.rearmedCount==12 && r.secondCycleMask==15);
  assert(r.txBufferIOVA>UINT32_MAX && r.chipCommandDuringRX==12 && r.chipCommandAfterRX==0);
  assert(r.combinedEngineObserved && !r.txErrorBits);
 }else{
  assert(!r.complete);
  if(mode!=4)assert(r.timeout);
  else assert(r.status==Stage2FStatus::TXDescriptorError && r.txErrorBits==8);
  if(mode==1)assert(r.status==Stage2FStatus::RXTimeout && r.txCompleted==8);
  if(mode==2)assert(r.status==Stage2FStatus::TXTimeout && r.completedCount==16);
  if(mode==3){assert(r.unsafeMappingRetained && r.txBuffer.opaque);dma.failBusMasterOff=false;
    assert(teardownStage2F(io,dma,a,r));}
  assert(!dma.busMaster && !a.busMasterEnabled);
 }
 unsigned enables=0;for(auto &e:io.events)if(e.kind=="w8"&&e.address==0x37&&e.value==12)++enables;
 assert(enables==1);teardownStage2A(io,dma,a);
}
void failureGates(){
 DuplexIO io;DuplexDMA dma;auto a=runStage2A(io,dma);Stage2CResult c;
 auto r=runStage2FFromC(io,dma,a,c);assert(r.status==Stage2FStatus::Stage2CIncomplete);
 c.complete=true; // Invalid all-zero source MAC must prevent OWN and combined enable.
 r=runStage2FFromC(io,dma,a,c);assert(r.status==Stage2FStatus::TXPrepareFailed);
 for(auto &e:io.events)assert(!(e.kind=="w8"&&e.address==0x37&&e.value==12));
 teardownStage2A(io,dma,a);
}
}
int main(){for(unsigned i=0;i<5;++i)exercise(i);failureGates();
 std::puts("PASS: STAGE2F 8 TX + 16 RX, 64-bit IOVA, one combined session, TX/RX timeouts and safe retention");}
