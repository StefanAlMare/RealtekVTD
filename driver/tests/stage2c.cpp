// SPDX-License-Identifier: GPL-2.0-only
#include "TraceBackend.hpp"
#include "../src/Stage2CProgram.hpp"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using namespace realtek_next;

namespace {
struct Holder { std::vector<uint8_t> bytes; explicit Holder(size_t n):bytes(n){} };

class FakeDMA final : public DMAProvider {
public:
 std::vector<std::string> events;
 bool mapper=true,busMaster=false,multiPacket=false;
 bool failPacketAllocate=false,failPacketPrepare=false,failPacketMap=false;
 bool failPacketZero=false,failPacketSyncCPU=false,failPollSync=false;
 bool failBusMasterOff=false;
 unsigned failRXRingSync=0,rxRingSyncs=0,completeAt=2,polls=0;
 uint32_t receivedOpts1=(1U<<30)|(1U<<29)|(1U<<28)|64;
 uint32_t receivedOpts2=0;
 uint64_t txRingIOVA=0x0000001200200000ULL;
 uint64_t rxRingIOVA=0x0000003400300000ULL;
 uint64_t packetIOVA=0x00000078abcdf000ULL;

 bool deviceMapperPresent() const override{return mapper;}
 static bool packet(const DMABuffer &b){return b.requestedLength==kStage2CRXBufferLength;}
 static bool txRing(const DMABuffer &b){return b.requestedLength==kTXRingLength;}
 static bool rxRing(const DMABuffer &b){return b.requestedLength==kRXRingLength;}
 const char *which(const DMABuffer &b)const{return packet(b)?"packet":txRing(b)?"tx-ring":"rx-ring";}
 DMAStatus allocate(DMABuffer &b,uint64_t n,uint32_t,DMADirection direction)override{
  events.push_back(std::string("allocate-")+whichLength(n));
  if(n==kStage2CRXBufferLength)assert(direction==DMADirection::FromDevice);
  if(n==kStage2CRXBufferLength&&failPacketAllocate)return DMAStatus::AllocationFailed;
  b.opaque=new Holder(size_t(n));b.requestedLength=n;b.allocated=true;return DMAStatus::Ok;
 }
 const char *whichLength(uint64_t n)const{return n==kStage2CRXBufferLength?"packet":n==kTXRingLength?"tx-ring":"rx-ring";}
 DMAStatus prepare(DMABuffer &b)override{events.push_back(std::string("prepare-")+which(b));if(packet(b)&&failPacketPrepare)return DMAStatus::PrepareFailed;b.prepared=true;return DMAStatus::Ok;}
 DMAStatus map(DMABuffer &b)override{
  events.push_back(std::string("map-")+which(b));if(packet(b)&&failPacketMap)return DMAStatus::MapFailed;
  b.mapped=true;b.mappedLength=b.requestedLength;b.segmentCount=packet(b)&&multiPacket?2:1;
  const uint64_t address=packet(b)?packetIOVA:txRing(b)?txRingIOVA:rxRingIOVA;
  b.segments[0]={address,b.segmentCount==1?b.requestedLength:b.requestedLength/2};
  if(b.segmentCount==2)b.segments[1]={address+b.segments[0].length,b.requestedLength-b.segments[0].length};
  return DMAStatus::Ok;
 }
 DMAStatus zero(DMABuffer &b)override{events.push_back(std::string("zero-")+which(b));if(packet(b)&&failPacketZero)return DMAStatus::ZeroFailed;std::memset(bytes(b),0,size_t(b.requestedLength));return DMAStatus::Ok;}
 void *bytes(DMABuffer &b)override{return b.opaque?static_cast<Holder*>(b.opaque)->bytes.data():nullptr;}
 DMAStatus syncForDevice(DMABuffer &b)override{
  events.push_back(std::string("sync-device-")+which(b));
  if(rxRing(b)&&++rxRingSyncs==failRXRingSync)return DMAStatus::SyncFailed;
  return DMAStatus::Ok;
 }
 DMAStatus syncForCPU(DMABuffer &b)override{
  events.push_back(std::string("sync-cpu-")+which(b));
  if(packet(b))return failPacketSyncCPU?DMAStatus::SyncFailed:DMAStatus::Ok;
  if(rxRing(b)){
   if(failPollSync)return DMAStatus::SyncFailed;
   if(completeAt&&++polls>=completeAt){auto *d=static_cast<uint32_t*>(bytes(b));d[0]=receivedOpts1;d[1]=receivedOpts2;}
  }
  return DMAStatus::Ok;
 }
 void memoryBarrier()override{events.push_back("barrier");}
 void unmap(DMABuffer &b)override{events.push_back(std::string("unmap-")+which(b));b.mapped=false;b.segmentCount=0;b.mappedLength=0;}
 void complete(DMABuffer &b)override{events.push_back(std::string("complete-")+which(b));b.prepared=false;}
 void release(DMABuffer &b)override{events.push_back(std::string("release-")+which(b));delete static_cast<Holder*>(b.opaque);b=DMABuffer{};}
 DMAStatus setBusMaster(bool enable)override{events.push_back(enable?"bus-master-on":"bus-master-off");if(!enable&&failBusMasterOff)return DMAStatus::BusMasterFailed;busMaster=enable;return DMAStatus::Ok;}
};

size_t where(const FakeDMA &dma,const std::string &event){auto i=std::find(dma.events.begin(),dma.events.end(),event);return i==dma.events.end()?dma.events.size():size_t(i-dma.events.begin());}

void prepare(TraceBackend &io,FakeDMA &dma,Stage2AResult &a,Stage2BResult &b){
 a=runStage2A(io,dma);assert(a.complete&&dma.busMaster);b.complete=true;b.phyLink=true;
 dma.events.clear();dma.rxRingSyncs=0;dma.polls=0;
}

void successHighIOVA(){
 TraceBackend io;FakeDMA dma;Stage2AResult a;Stage2BResult b;prepare(io,dma,a,b);
 auto r=runStage2C(io,dma,a,b);
 assert(r.status==Stage2CStatus::Ok&&r.complete&&r.packetDMAExercised);
 assert(r.rxRingRegisterIOVA==dma.rxRingIOVA);
 assert(r.rxBufferMapped&&r.rxBufferIOVA==dma.packetIOVA&&r.rxBufferIOVA>UINT32_MAX);
 assert(r.rxBufferSegmentCount==1&&r.rxBufferLength==kStage2CRXBufferLength);
 assert(r.rxDesc0AddressHigh==0x78&&r.rxDesc0AddressLow==0xabcdf000U);
 assert(r.rxDesc0Opts1BeforeOwn==((1U<<30)|kStage2CRXBufferLength));
 assert(r.ownershipPublished&&r.ownershipCleared&&r.rxCompleted&&r.descriptorValid);
 assert(r.receivedLength==64&&!r.descriptorErrorBits&&r.pollCount==2&&!r.timeout);
 assert(r.chipCommandBeforeRX==0&&r.chipCommandDuringRX==8&&r.chipCommandAfterRX==0);
 assert(!r.trafficEnabled&&io.mmio[0x37]==0&&io.mmio[0x3c]==0&&io.mmio[0x44]==0);
 assert(io.mmio[0x08]==0&&io.mmio[0x0c]==0);
 assert(dma.busMaster&&a.busMasterEnabled);
 assert(where(dma,"zero-packet")<where(dma,"sync-cpu-packet"));
 assert(where(dma,"sync-cpu-rx-ring")<where(dma,"unmap-packet"));
 teardownStage2A(io,dma,a);
}

void gatesAndFailures(){
 {TraceBackend io;FakeDMA d;Stage2AResult a;Stage2BResult b;prepare(io,d,a,b);b.complete=false;
  auto r=runStage2C(io,d,a,b);assert(r.status==Stage2CStatus::Stage2BIncomplete&&!r.ownershipPublished);teardownStage2A(io,d,a);}
 {TraceBackend io;FakeDMA d;Stage2AResult a;Stage2BResult b;prepare(io,d,a,b);io.mmio[0xe8]^=1;
  auto r=runStage2C(io,d,a,b);assert(r.status==Stage2CStatus::RingRegisterMismatch&&!r.ownershipPublished);teardownStage2A(io,d,a);}
 {TraceBackend io;FakeDMA d;Stage2AResult a;Stage2BResult b;prepare(io,d,a,b);d.multiPacket=true;
  auto r=runStage2C(io,d,a,b);assert(r.status==Stage2CStatus::BufferSegmentInvalid&&!r.ownershipPublished);teardownStage2A(io,d,a);}
 {TraceBackend io;FakeDMA d;Stage2AResult a;Stage2BResult b;prepare(io,d,a,b);d.failPacketAllocate=true;
  auto r=runStage2C(io,d,a,b);assert(r.status==Stage2CStatus::BufferAllocationFailed&&!r.ownershipPublished);teardownStage2A(io,d,a);}
 {TraceBackend io;FakeDMA d;Stage2AResult a;Stage2BResult b;prepare(io,d,a,b);d.failPacketPrepare=true;
  auto r=runStage2C(io,d,a,b);assert(r.status==Stage2CStatus::BufferPrepareFailed&&!r.ownershipPublished);teardownStage2A(io,d,a);}
 {TraceBackend io;FakeDMA d;Stage2AResult a;Stage2BResult b;prepare(io,d,a,b);d.failPacketMap=true;
  auto r=runStage2C(io,d,a,b);assert(r.status==Stage2CStatus::BufferMapFailed&&!r.ownershipPublished);teardownStage2A(io,d,a);}
 {TraceBackend io;FakeDMA d;Stage2AResult a;Stage2BResult b;prepare(io,d,a,b);d.failPacketZero=true;
  auto r=runStage2C(io,d,a,b);assert(r.status==Stage2CStatus::BufferSyncFailed&&!r.ownershipPublished);teardownStage2A(io,d,a);}
 {TraceBackend io;FakeDMA d;Stage2AResult a;Stage2BResult b;prepare(io,d,a,b);d.failRXRingSync=1;
  auto r=runStage2C(io,d,a,b);assert(r.status==Stage2CStatus::DescriptorSyncFailed&&!r.ownershipPublished);teardownStage2A(io,d,a);}
 {TraceBackend io;FakeDMA d;Stage2AResult a;Stage2BResult b;prepare(io,d,a,b);io.failWriteOffset=0x44;
  auto r=runStage2C(io,d,a,b);assert(r.status==Stage2CStatus::RXConfigurationFailed&&!r.ownershipPublished);assert(r.rxConfigOffset==0x44 && r.rxConfigWidth==32 && !r.rxConfigAccessSucceeded && !r.rxConfigObservedValid);io.failWriteOffset=0xffff;teardownStage2A(io,d,a);}
 {TraceBackend io;FakeDMA d;Stage2AResult a;Stage2BResult b;prepare(io,d,a,b);d.failRXRingSync=2;
  auto r=runStage2C(io,d,a,b);assert(r.status==Stage2CStatus::DescriptorSyncFailed&&r.ownershipPublished);
  assert(where(d,"bus-master-off")<where(d,"unmap-packet"));teardownStage2A(io,d,a);}
 {TraceBackend io;FakeDMA d;Stage2AResult a;Stage2BResult b;prepare(io,d,a,b);io.failWriteOffset=0x37;
  auto r=runStage2C(io,d,a,b);assert(r.status==Stage2CStatus::RXEnableFailed&&r.ownershipPublished);
  assert(where(d,"bus-master-off")<where(d,"unmap-packet"));io.failWriteOffset=0xffff;teardownStage2A(io,d,a);}
 {TraceBackend io;FakeDMA d;Stage2AResult a;Stage2BResult b;prepare(io,d,a,b);d.failPollSync=true;
  auto r=runStage2C(io,d,a,b);assert(r.status==Stage2CStatus::PollSyncFailed&&r.ownershipPublished);
  assert(where(d,"bus-master-off")<where(d,"unmap-packet"));teardownStage2A(io,d,a);}
 {TraceBackend io;FakeDMA d;Stage2AResult a;Stage2BResult b;prepare(io,d,a,b);d.receivedOpts1=(1U<<30)|(1U<<29)|(1U<<28)|(1U<<21)|64;
  auto r=runStage2C(io,d,a,b);assert(r.status==Stage2CStatus::RXDescriptorInvalid&&r.ownershipCleared&&!r.packetDMAExercised);teardownStage2A(io,d,a);}
 {TraceBackend io;FakeDMA d;Stage2AResult a;Stage2BResult b;prepare(io,d,a,b);d.failPacketSyncCPU=true;
  auto r=runStage2C(io,d,a,b);assert(r.status==Stage2CStatus::RXBufferSyncFailed&&r.ownershipCleared);teardownStage2A(io,d,a);}
 {TraceBackend io;FakeDMA d;Stage2AResult a;Stage2BResult b;prepare(io,d,a,b);d.failRXRingSync=3;
  auto r=runStage2C(io,d,a,b);assert(r.status==Stage2CStatus::DescriptorCleanupFailed&&r.ownershipCleared);teardownStage2A(io,d,a);}
}

void timeoutSafety(){
 {TraceBackend io;FakeDMA d;Stage2AResult a;Stage2BResult b;prepare(io,d,a,b);d.completeAt=0;
  auto r=runStage2C(io,d,a,b);assert(r.status==Stage2CStatus::RXTimeout&&r.timeout&&!r.complete);
  assert(r.pollCount==kStage2CRXPollAttempts&&!d.busMaster&&!a.busMasterEnabled);
  assert(where(d,"bus-master-off")<where(d,"unmap-packet"));assert(!r.rxBuffer.opaque&&!r.unsafeMappingRetained);
  teardownStage2A(io,d,a);}
 {TraceBackend io;FakeDMA d;Stage2AResult a;Stage2BResult b;prepare(io,d,a,b);d.completeAt=0;d.failBusMasterOff=true;
  auto r=runStage2C(io,d,a,b);assert(r.status==Stage2CStatus::SafeQuiesceFailed&&r.unsafeMappingRetained);
  assert(r.rxBuffer.opaque&&where(d,"unmap-packet")==d.events.size());d.failBusMasterOff=false;
  assert(teardownStage2C(io,d,a,r));assert(where(d,"bus-master-off")<where(d,"unmap-packet"));teardownStage2A(io,d,a);}
}
}

class RXReadDiagnosticBackend : public TraceBackend {
public:
 bool mismatch = false;
 bool read16(uint16_t offset, uint16_t &value) override {
  if(offset == 0xda) {
   if(!mismatch)return false;
   value=0; return true;
  }
  return TraceBackend::read16(offset,value);
 }
};
void diagnosticReadFailures() {
 for(bool mismatch : {false,true}) {
  RXReadDiagnosticBackend io; FakeDMA dma; Stage2AResult a; Stage2BResult b;
  prepare(io,dma,a,b);io.mismatch=mismatch;
  auto r=runStage2C(io,dma,a,b);
  assert(r.status==Stage2CStatus::RXConfigurationFailed && !r.ownershipPublished);
  assert(std::string(r.rxConfigStep)=="max-size-read");
  assert(r.rxConfigOffset==0xda && r.rxConfigWidth==16);
  assert(r.rxConfigExpected==16384 && r.rxConfigObserved==0);
  assert(r.rxConfigAccessSucceeded==mismatch && r.rxConfigObservedValid==mismatch);
  teardownStage2A(io,dma,a);
 }
}

class RXConfigReadbackBackend : public TraceBackend {
public:
 uint32_t setBits = 0;
 uint32_t clearBits = 0;
 bool read32(uint16_t offset, uint32_t &value) override {
  if (!TraceBackend::read32(offset, value)) return false;
  if (offset == 0x44) value = (value | setBits) & ~clearBits;
  return true;
 }
};

void maskedRXConfigReadback() {
 {RXConfigReadbackBackend io; FakeDMA dma; Stage2AResult a; Stage2BResult b;
  prepare(io,dma,a,b);io.setBits=1U<<17;
  auto r=runStage2C(io,dma,a,b);
  assert(r.status==Stage2CStatus::Ok&&r.complete&&r.packetDMAExercised);
  assert(r.rxConfigExpected==0xcf0e&&r.rxConfigObserved==0x2cf0e);
  assert(r.rxConfigVerifyMask==0xcf3f);
  assert(r.rxConfigMaskedExpected==0xcf0e&&r.rxConfigMaskedObserved==0xcf0e);
  assert(r.rxConfigHardwareManagedBits==0x20000);
  teardownStage2A(io,dma,a);}
 {RXConfigReadbackBackend io; FakeDMA dma; Stage2AResult a; Stage2BResult b;
  prepare(io,dma,a,b);io.clearBits=0x02;
  auto r=runStage2C(io,dma,a,b);
  assert(r.status==Stage2CStatus::RXConfigurationFailed&&!r.ownershipPublished);
  assert(r.rxConfigMaskedExpected==0xcf0e&&r.rxConfigMaskedObserved==0xcf0c);
  teardownStage2A(io,dma,a);}
}

int main(){diagnosticReadFailures();maskedRXConfigReadback();successHighIOVA();gatesAndFailures();timeoutSafety();
 std::puts("PASS: STAGE2C single RX IOVA, one-descriptor ownership, finite polling and timeout-safe teardown");}
