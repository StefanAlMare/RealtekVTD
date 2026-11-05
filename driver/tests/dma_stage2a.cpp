// SPDX-License-Identifier: GPL-2.0-only
#include "TraceBackend.hpp"
#include "../src/DMAProgram.hpp"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
#include <array>
using namespace realtek_next;

class FakeDMA final : public DMAProvider {
public:
 enum class Fail {None,TXAllocate,TXPrepare,TXMap,TXZero,TXSync,
                  RXAllocate,RXPrepare,RXMap,RXZero,RXSync,BusMaster};
 std::vector<std::string> events;
 Fail fail=Fail::None;
 bool mapper=true,busMaster=false,multipleTX=false,multipleRX=false;
 uint64_t txAddress=0x00000012abcdef00ULL;
 uint64_t rxAddress=0x00000034fedcba00ULL;
 std::array<uint8_t,kRXRingLength> storage{};

 bool deviceMapperPresent() const override {return mapper;}
 bool isTX(const DMABuffer &b) const {return b.requestedLength==kTXRingLength;}
 bool failing(const DMABuffer &b,Fail tx,Fail rx)const{return fail==(isTX(b)?tx:rx);}
 const char *name(const DMABuffer &b,const char *tx,const char *rx)const{return isTX(b)?tx:rx;}
 DMAStatus allocate(DMABuffer &b,uint64_t length,uint32_t alignment,DMADirection direction) override {
  (void)alignment;(void)direction;b.requestedLength=length;
  events.push_back(length==kTXRingLength?"allocate-tx":"allocate-rx");
  if(fail==(length==kTXRingLength?Fail::TXAllocate:Fail::RXAllocate))return DMAStatus::AllocationFailed;
  b.opaque=this;b.allocated=true;return DMAStatus::Ok;
 }
 DMAStatus prepare(DMABuffer &b) override {events.push_back(name(b,"prepare-tx","prepare-rx"));
  if(failing(b,Fail::TXPrepare,Fail::RXPrepare))return DMAStatus::PrepareFailed;b.prepared=true;return DMAStatus::Ok;}
 DMAStatus map(DMABuffer &b) override {events.push_back(name(b,"map-tx","map-rx"));
  if(failing(b,Fail::TXMap,Fail::RXMap))return DMAStatus::MapFailed;
  b.mapped=true;b.mappedLength=b.requestedLength;b.segmentCount=1;
  b.segments[0]={isTX(b)?txAddress:rxAddress,b.requestedLength};
  if((isTX(b)&&multipleTX)||(!isTX(b)&&multipleRX)){b.segmentCount=2;b.segments[0].length=b.requestedLength/2;b.segments[1]={b.segments[0].address+b.segments[0].length,b.requestedLength/2};}
  return DMAStatus::Ok;
 }
 DMAStatus zero(DMABuffer &b) override {events.push_back(name(b,"zero-tx","zero-rx"));
  return failing(b,Fail::TXZero,Fail::RXZero)?DMAStatus::ZeroFailed:DMAStatus::Ok;}
 void *bytes(DMABuffer &) override {return storage.data();}
 DMAStatus syncForDevice(DMABuffer &b) override {events.push_back(name(b,"sync-tx","sync-rx"));
  return failing(b,Fail::TXSync,Fail::RXSync)?DMAStatus::SyncFailed:DMAStatus::Ok;}
 DMAStatus syncForCPU(DMABuffer &) override {return DMAStatus::Ok;}
 void memoryBarrier() override {events.push_back("barrier");}
 void unmap(DMABuffer &b) override {events.push_back(name(b,"unmap-tx","unmap-rx"));b.mapped=false;b.segmentCount=0;b.mappedLength=0;}
 void complete(DMABuffer &b) override {events.push_back(name(b,"complete-tx","complete-rx"));b.prepared=false;}
 void release(DMABuffer &b) override {events.push_back(name(b,"release-tx","release-rx"));b=DMABuffer{};}
 DMAStatus setBusMaster(bool enable) override {events.push_back(enable?"bus-master-on":"bus-master-off");
  if(enable&&fail==Fail::BusMaster)return DMAStatus::BusMasterFailed;busMaster=enable;return DMAStatus::Ok;}
};

static size_t where(const FakeDMA &d,const char *event){for(size_t i=0;i<d.events.size();++i)if(d.events[i]==event)return i;return d.events.size();}
static size_t mmioWhere(const TraceBackend &io,unsigned address,uint32_t value){
 for(size_t i=0;i<io.events.size();++i)if(io.events[i].kind=="w32"&&io.events[i].address==address&&io.events[i].value==value)return i;
 return io.events.size();
}
static void highAddressAndLifetime(){
 TraceBackend io;FakeDMA dma;auto result=runStage2A(io,dma);
 assert(result.status==DMAStatus::Ok&&result.complete&&result.mapperPresent&&result.busMasterEnabled);
 assert(result.tx.mapped&&result.rx.mapped&&result.tx.segmentCount==1&&result.rx.segmentCount==1);
 assert(io.mmio[0x20]==0xabcdef00U&&io.mmio[0x24]==0x12U);
 assert(io.mmio[0xe4]==0xfedcba00U&&io.mmio[0xe8]==0x34U);
 assert(mmioWhere(io,0x20,0xabcdef00U)<mmioWhere(io,0x24,0x12U));
 assert(mmioWhere(io,0x24,0x12U)<mmioWhere(io,0xe4,0xfedcba00U));
 assert(mmioWhere(io,0xe4,0xfedcba00U)<mmioWhere(io,0xe8,0x34U));
 assert(io.mmio[0x37]==0&&dma.busMaster);
 assert(where(dma,"sync-tx")<where(dma,"allocate-rx"));
 teardownStage2A(io,dma,result);
 assert(!dma.busMaster&&!result.complete&&!result.tx.opaque&&!result.rx.opaque);
 assert(where(dma,"bus-master-off")<where(dma,"unmap-rx"));
 assert(where(dma,"unmap-rx")<where(dma,"complete-rx"));
 assert(where(dma,"complete-rx")<where(dma,"release-rx"));
 assert(where(dma,"release-rx")<where(dma,"unmap-tx"));
}
static void validationFailures(){
 {TraceBackend io;FakeDMA d;d.multipleTX=true;auto r=runStage2A(io,d);assert(r.status==DMAStatus::SegmentCountInvalid&&!r.complete&&!r.tx.opaque);}
 {TraceBackend io;FakeDMA d;d.multipleRX=true;auto r=runStage2A(io,d);assert(r.status==DMAStatus::SegmentCountInvalid&&!r.complete&&!r.tx.opaque&&!r.rx.opaque);}
 {TraceBackend io;FakeDMA d;d.txAddress|=1;auto r=runStage2A(io,d);assert(r.status==DMAStatus::AlignmentInvalid&&!r.complete);}
 {TraceBackend io;FakeDMA d;d.txAddress=0xffffffffffffff00ULL;auto r=runStage2A(io,d);assert(r.status==DMAStatus::RangeOverflow&&!r.complete);}
 {TraceBackend io;FakeDMA d;d.mapper=false;auto r=runStage2A(io,d);assert(r.status==DMAStatus::MapperMissing&&d.events.empty());}
}
static void injectedFailures(){
 const FakeDMA::Fail failures[]={FakeDMA::Fail::TXAllocate,FakeDMA::Fail::TXPrepare,FakeDMA::Fail::TXMap,
  FakeDMA::Fail::TXZero,FakeDMA::Fail::TXSync,FakeDMA::Fail::RXAllocate,FakeDMA::Fail::RXPrepare,
  FakeDMA::Fail::RXMap,FakeDMA::Fail::RXZero,FakeDMA::Fail::RXSync,FakeDMA::Fail::BusMaster};
 for(auto failure:failures){TraceBackend io;FakeDMA d;d.fail=failure;auto r=runStage2A(io,d);
  assert(r.status!=DMAStatus::Ok&&!r.complete&&!r.tx.opaque&&!r.rx.opaque&&!d.busMaster);
  if(failure==FakeDMA::Fail::RXAllocate)assert(where(d,"release-tx")<d.events.size());
  if(failure==FakeDMA::Fail::TXMap)assert(where(d,"complete-tx")<where(d,"release-tx"));
  if(failure==FakeDMA::Fail::TXSync)assert(where(d,"unmap-tx")<where(d,"complete-tx"));
  if(failure==FakeDMA::Fail::BusMaster)assert(where(d,"bus-master-off")<where(d,"unmap-rx"));
 }
}
static void registerAndBusMasterFailures(){
 {TraceBackend io;FakeDMA d;io.failAt=4;auto r=runStage2A(io,d);assert(r.status==DMAStatus::RegisterVerificationFailed&&!r.complete&&!r.tx.opaque&&!r.rx.opaque);}
 {TraceBackend io;FakeDMA d;d.fail=FakeDMA::Fail::BusMaster;auto r=runStage2A(io,d);assert(r.status==DMAStatus::BusMasterFailed&&!r.complete&&!r.tx.opaque&&!r.rx.opaque);}
}
int main(){highAddressAndLifetime();validationFailures();injectedFailures();registerAndBusMasterFailures();
 puts("PASS: STAGE2A 64-bit IOVA, ring validation, rollback, lifetime and no-low4G assumptions");}
