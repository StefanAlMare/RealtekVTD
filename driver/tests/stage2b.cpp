// SPDX-License-Identifier: GPL-2.0-only
#include "TraceBackend.hpp"
#include "../src/Stage2BProgram.hpp"
#include <algorithm>
#include <array>
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
 std::array<uint8_t,kStage2BFrameLength> lastFrame{};
 bool mapper=true,busMaster=false,multiPacket=false,failPacketSync=false;
 bool failPacketAllocate=false,failPacketPrepare=false,failPacketMap=false;
 bool failPollSync=false,mismatchDescriptor=false,failBusMasterOff=false;
 unsigned failRingSync=0;
 unsigned completeAt=1,polls=0,ringSyncsAfterSetup=0;
 uint32_t *txDescriptor=nullptr;
 uint64_t txRingIOVA=0x0000001200200000ULL;
 uint64_t rxRingIOVA=0x0000003400300000ULL;
 uint64_t packetIOVA=0x00000056abcdef00ULL;

 bool deviceMapperPresent() const override{return mapper;}
 static bool packet(const DMABuffer &b){return b.requestedLength==kStage2BFrameLength;}
 static bool txRing(const DMABuffer &b){return b.requestedLength==kTXRingLength;}
 const char *which(const DMABuffer &b)const{return packet(b)?"packet":txRing(b)?"tx-ring":"rx-ring";}
 DMAStatus allocate(DMABuffer &b,uint64_t n,uint32_t,DMADirection direction)override{
  events.push_back(std::string("allocate-")+((n==kStage2BFrameLength)?"packet":n==kTXRingLength?"tx-ring":"rx-ring"));
  if(n==kStage2BFrameLength)assert(direction==DMADirection::ToDevice);
  if(n==kStage2BFrameLength&&failPacketAllocate)return DMAStatus::AllocationFailed;
  b.opaque=new Holder(size_t(n));b.requestedLength=n;b.allocated=true;return DMAStatus::Ok;
 }
 DMAStatus prepare(DMABuffer &b)override{events.push_back(std::string("prepare-")+which(b));if(packet(b)&&failPacketPrepare)return DMAStatus::PrepareFailed;b.prepared=true;return DMAStatus::Ok;}
 DMAStatus map(DMABuffer &b)override{
  events.push_back(std::string("map-")+which(b));if(packet(b)&&failPacketMap)return DMAStatus::MapFailed;b.mapped=true;b.mappedLength=b.requestedLength;
  b.segmentCount=packet(b)&&multiPacket?2:1;
  const uint64_t address=packet(b)?packetIOVA:txRing(b)?txRingIOVA:rxRingIOVA;
  b.segments[0]={address,b.segmentCount==1?b.requestedLength:b.requestedLength/2};
  if(b.segmentCount==2)b.segments[1]={address+b.segments[0].length,b.requestedLength-b.segments[0].length};
  if(txRing(b))txDescriptor=static_cast<uint32_t*>(bytes(b));
  return DMAStatus::Ok;
 }
 DMAStatus zero(DMABuffer &b)override{events.push_back(std::string("zero-")+which(b));std::memset(bytes(b),0,size_t(b.requestedLength));return DMAStatus::Ok;}
 void *bytes(DMABuffer &b)override{return b.opaque?static_cast<Holder*>(b.opaque)->bytes.data():nullptr;}
 DMAStatus syncForDevice(DMABuffer &b)override{
  events.push_back(std::string("sync-device-")+which(b));
  if(packet(b)&&failPacketSync)return DMAStatus::SyncFailed;
  if(txRing(b)){++ringSyncsAfterSetup;
   if(failRingSync&&ringSyncsAfterSetup==failRingSync)return DMAStatus::SyncFailed;
   auto *d=static_cast<uint32_t*>(bytes(b));
   if(mismatchDescriptor&&!(d[0]&(1U<<31)))d[2]^=1;
  }
  return DMAStatus::Ok;
 }
 DMAStatus syncForCPU(DMABuffer &b)override{
  events.push_back(std::string("sync-cpu-")+which(b));
  if(txRing(b)&&failPollSync)return DMAStatus::SyncFailed;
  if(txRing(b)&&completeAt&&++polls>=completeAt){auto *d=static_cast<uint32_t*>(bytes(b));d[0]&=~(1U<<31);}
  return DMAStatus::Ok;
 }
 void memoryBarrier()override{
  if(txDescriptor&&(txDescriptor[0]&(1U<<31)))events.push_back("barrier-own");
  else if(txDescriptor&&txDescriptor[2])events.push_back("barrier-descriptor-ready");
  else events.push_back("barrier");
 }
 void unmap(DMABuffer &b)override{events.push_back(std::string("unmap-")+which(b));b.mapped=false;b.segmentCount=0;b.mappedLength=0;}
 void complete(DMABuffer &b)override{events.push_back(std::string("complete-")+which(b));b.prepared=false;}
 void release(DMABuffer &b)override{
  events.push_back(std::string("release-")+which(b));
  if(packet(b))std::copy_n(static_cast<Holder*>(b.opaque)->bytes.begin(),kStage2BFrameLength,lastFrame.begin());
  delete static_cast<Holder*>(b.opaque);b=DMABuffer{};
 }
 DMAStatus setBusMaster(bool enable)override{
  events.push_back(enable?"bus-master-on":"bus-master-off");
  if(!enable&&failBusMasterOff)return DMAStatus::BusMasterFailed;
  busMaster=enable;return DMAStatus::Ok;
 }
};

size_t where(const FakeDMA &dma,const std::string &event){
 auto it=std::find(dma.events.begin(),dma.events.end(),event);
 return it==dma.events.end()?dma.events.size():size_t(it-dma.events.begin());
}

void prepare(TraceBackend &io,FakeDMA &dma,Stage2AResult &stage2a){
 io.mmio[0x00]=0x44332202;io.mmio[0x04]=0x6655;io.phy[0xa402]=4;
 stage2a=runStage2A(io,dma);assert(stage2a.complete&&dma.busMaster);
 dma.events.clear();dma.ringSyncsAfterSetup=0;dma.polls=0;
}

void successHighIOVAAndOrdering(){
 TraceBackend io;FakeDMA dma;Stage2AResult a;prepare(io,dma,a);
 auto r=runStage2B(io,dma,a);
 assert(r.status==Stage2BStatus::Ok&&r.complete&&r.packetDMAExercised);
 assert(r.linkPollCount==1&&!r.linkTimedOut&&r.linkWaitUs==0);
 assert(r.txRingRegisterIOVA==dma.txRingIOVA&&r.rxRingRegisterIOVA==dma.rxRingIOVA);
 assert(r.txBufferMapped&&r.txBufferIOVA==dma.packetIOVA&&r.txBufferIOVA>UINT32_MAX);
 assert(r.txBufferSegmentCount==1&&r.txBufferLength==kStage2BFrameLength);
 assert(r.txDesc0AddressHigh==0x56&&r.txDesc0AddressLow==0xabcdef00U);
 assert(((uint64_t(r.txDesc0AddressHigh)<<32)|r.txDesc0AddressLow)==r.txBufferIOVA);
 assert(r.txDesc0Opts1BeforeOwn==(1U<<28|kStage2BFrameLength));
 assert(r.ownershipPublished&&r.ownershipCleared&&r.txCompleted&&!r.timeout);
 assert(r.chipCommandBeforeTX==0&&r.chipCommandDuringTX==4&&r.chipCommandAfterTX==0&&!r.trafficEnabled);
 assert(io.mmio[0x37]==0&&io.mmio[0x3c]==0&&io.mmio[0x38]==0x40);
 assert(dma.busMaster&&a.busMasterEnabled);
  assert(where(dma,"sync-device-packet")<where(dma,"sync-device-tx-ring"));
  assert(where(dma,"barrier-descriptor-ready")<where(dma,"barrier-own"));
 assert(where(dma,"sync-device-tx-ring")<where(dma,"sync-cpu-tx-ring"));
 assert(where(dma,"sync-cpu-tx-ring")<where(dma,"unmap-packet"));
 assert(dma.lastFrame[0]==0x02&&dma.lastFrame[6]==0x02&&dma.lastFrame[12]==0x88&&dma.lastFrame[13]==0xb5);
 assert(std::memcmp(dma.lastFrame.data()+14,"RealtekVTDNext-STAGE2B",22)==0);
 for(const auto &e:io.events)assert(!(e.address==0x3c&&e.value));
 teardownStage2A(io,dma,a);
}

void gatesAndValidation(){
 {TraceBackend io;FakeDMA d;Stage2AResult a;prepare(io,d,a);io.phy[0xa402]=0;
  auto r=runStage2B(io,d,a);assert(r.status==Stage2BStatus::LinkDown&&!r.complete&&!r.txBufferMapped);
  assert(r.linkPollCount==kStage2BLinkPollAttempts&&r.linkTimedOut);
  assert(r.linkWaitUs==uint64_t(kStage2BLinkPollAttempts-1)*kStage2BLinkPollIntervalUs);
  assert(where(d,"allocate-packet")==d.events.size());teardownStage2A(io,d,a);}
 {TraceBackend io;FakeDMA d;Stage2AResult a;prepare(io,d,a);io.phy[0xa402]=0;
  io.delayCalls=0;io.linkUpAfterDelayCount=3;
  auto r=runStage2B(io,d,a);assert(r.status==Stage2BStatus::Ok&&r.complete);
  assert(r.phyLink&&r.linkPollCount==4&&!r.linkTimedOut);
  assert(r.linkWaitUs==3ULL*kStage2BLinkPollIntervalUs);teardownStage2A(io,d,a);}
 {TraceBackend io;FakeDMA d;Stage2AResult a;prepare(io,d,a);d.mapper=false;
  auto r=runStage2B(io,d,a);assert(r.status==Stage2BStatus::Stage2AIncomplete&&!r.ownershipPublished);d.mapper=true;teardownStage2A(io,d,a);}
 {TraceBackend io;FakeDMA d;Stage2AResult a;prepare(io,d,a);io.mmio[0x24]^=1;
  auto r=runStage2B(io,d,a);assert(r.status==Stage2BStatus::RingRegisterMismatch&&!r.ownershipPublished);teardownStage2A(io,d,a);}
 {TraceBackend io;FakeDMA d;Stage2AResult a;prepare(io,d,a);d.multiPacket=true;
  auto r=runStage2B(io,d,a);assert(r.status==Stage2BStatus::BufferSegmentInvalid&&!r.ownershipPublished);teardownStage2A(io,d,a);}
 {TraceBackend io;FakeDMA d;Stage2AResult a;prepare(io,d,a);d.failPacketSync=true;
  auto r=runStage2B(io,d,a);assert(r.status==Stage2BStatus::BufferSyncFailed&&!r.ownershipPublished);teardownStage2A(io,d,a);}
 {TraceBackend io;FakeDMA d;Stage2AResult a;prepare(io,d,a);d.mismatchDescriptor=true;
  auto r=runStage2B(io,d,a);assert(r.status==Stage2BStatus::DescriptorMismatch&&!r.ownershipPublished);teardownStage2A(io,d,a);}
 {TraceBackend io;FakeDMA d;Stage2AResult a;prepare(io,d,a);d.failPacketAllocate=true;
  auto r=runStage2B(io,d,a);assert(r.status==Stage2BStatus::BufferAllocationFailed&&!r.ownershipPublished);teardownStage2A(io,d,a);}
 {TraceBackend io;FakeDMA d;Stage2AResult a;prepare(io,d,a);d.failPacketPrepare=true;
  auto r=runStage2B(io,d,a);assert(r.status==Stage2BStatus::BufferPrepareFailed&&!r.ownershipPublished);teardownStage2A(io,d,a);}
 {TraceBackend io;FakeDMA d;Stage2AResult a;prepare(io,d,a);d.failPacketMap=true;
  auto r=runStage2B(io,d,a);assert(r.status==Stage2BStatus::BufferMapFailed&&!r.ownershipPublished);teardownStage2A(io,d,a);}
 {TraceBackend io;FakeDMA d;Stage2AResult a;prepare(io,d,a);d.failRingSync=1;
  auto r=runStage2B(io,d,a);assert(r.status==Stage2BStatus::DescriptorSyncFailed&&!r.ownershipPublished);teardownStage2A(io,d,a);}
 {TraceBackend io;FakeDMA d;Stage2AResult a;prepare(io,d,a);d.failPollSync=true;
  auto r=runStage2B(io,d,a);assert(r.status==Stage2BStatus::PollSyncFailed&&r.ownershipPublished);
  assert(where(d,"bus-master-off")<where(d,"unmap-packet"));teardownStage2A(io,d,a);}
}

void timeoutSafety(){
 {TraceBackend io;FakeDMA d;Stage2AResult a;prepare(io,d,a);d.completeAt=0;
  auto r=runStage2B(io,d,a);assert(r.status==Stage2BStatus::TXTimeout&&r.timeout&&!r.complete);
  assert(r.pollCount==kStage2BPollAttempts&&!d.busMaster&&!a.busMasterEnabled);
  assert(where(d,"bus-master-off")<where(d,"unmap-packet"));
  assert(!r.txBuffer.opaque&&!r.unsafeMappingRetained);teardownStage2A(io,d,a);}
 {TraceBackend io;FakeDMA d;Stage2AResult a;prepare(io,d,a);d.completeAt=0;d.failBusMasterOff=true;
  auto r=runStage2B(io,d,a);assert(r.status==Stage2BStatus::SafeQuiesceFailed&&r.unsafeMappingRetained);
  assert(r.txBuffer.opaque&&where(d,"unmap-packet")==d.events.size());
  d.failBusMasterOff=false;assert(teardownStage2B(io,d,a,r));
  assert(where(d,"bus-master-off")<where(d,"unmap-packet"));
  teardownStage2A(io,d,a);}
}
}

int main(){successHighIOVAAndOrdering();gatesAndValidation();timeoutSafety();
 std::puts("PASS: STAGE2B bounded link wait, single TX IOVA, OWN completion, finite polling and timeout-safe teardown");}
