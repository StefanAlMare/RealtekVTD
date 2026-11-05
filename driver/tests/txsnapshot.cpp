// SPDX-License-Identifier: GPL-2.0-only
#include "../src/TXSnapshot.hpp"
#include "../src/NetworkProgram.hpp"
#include "TraceBackend.hpp"
#include <vector>
#include <cstring>
#include <cassert>
#include <cstdio>
using namespace realtek_next;
struct Memory{std::vector<uint8_t> data;explicit Memory(size_t size):data(size){}};
struct DMA: DMAProvider{
 unsigned allocations=0,releases=0,unmaps=0,completes=0;int fail=0;bool bme=false;
 bool deviceMapperPresent()const override{return fail!=1;}
 DMAStatus allocate(DMABuffer &b,uint64_t len,uint32_t alignment,DMADirection direction)override{
  assert(alignment==(direction==DMADirection::FromDevice?16U:256U));assert(direction==DMADirection::ToDevice||direction==DMADirection::Bidirectional||direction==DMADirection::FromDevice);
  if(fail==2)return DMAStatus::AllocationFailed;
  b.opaque=new Memory(len);b.allocated=true;b.requestedLength=len;++allocations;return DMAStatus::Ok;
 }
 void *bytes(DMABuffer &b)override{return fail==3?nullptr:static_cast<Memory*>(b.opaque)->data.data();}
 DMAStatus prepare(DMABuffer &b)override{if(fail==4)return DMAStatus::PrepareFailed;b.prepared=true;return DMAStatus::Ok;}
 DMAStatus map(DMABuffer &b)override{
  if(fail==5)return DMAStatus::MapFailed;
  b.mapped=true;b.mappedLength=b.requestedLength;b.segmentCount=fail==6?2:1;
  b.segments[0]={fail==8?UINT64_MAX-10:0x1234567800ULL,b.requestedLength-(fail==7?1:0)};return DMAStatus::Ok;
 }
 DMAStatus zero(DMABuffer &b)override{std::memset(bytes(b),0,size_t(b.requestedLength));return DMAStatus::Ok;}
 DMAStatus syncForDevice(DMABuffer &)override{return DMAStatus::Ok;}
 DMAStatus syncForCPU(DMABuffer &)override{return DMAStatus::Ok;}
 void memoryBarrier()override{}
 void unmap(DMABuffer &b)override{assert(b.mapped);b.mapped=false;++unmaps;}
 void complete(DMABuffer &b)override{assert(b.prepared);b.prepared=false;++completes;}
 void release(DMABuffer &b)override{assert(!b.mapped&&!b.prepared);delete static_cast<Memory*>(b.opaque);b=DMABuffer{};++releases;}
 DMAStatus setBusMaster(bool enabled)override{bme=enabled;return DMAStatus::Ok;}
};
bool copy(void *source,void *destination,size_t size){std::memcpy(destination,source,size);return true;}
bool badCopy(void *,void *,size_t){return false;}
struct Sink: PacketSink{
 unsigned freed=0;
 bool receive(const uint8_t *,unsigned)override{return true;}
 void releasePacket(void *packet)override{assert(packet);++freed;}
};
int main(){
 uint8_t source[1514];for(unsigned i=0;i<1514;++i)source[i]=uint8_t(i);
 for(int fail=1;fail<=8;++fail){DMA d;d.fail=fail;DMABuffer b;
  assert(prepareTXSnapshot(d,b,sizeof(source),copy,source)!=DMAStatus::Ok);
  assert(!b.opaque&&!b.mapped&&!b.prepared);assert(d.releases==d.allocations);
 }
 {DMA d;DMABuffer b;assert(prepareTXSnapshot(d,b,60,badCopy,source)!=DMAStatus::Ok);assert(d.releases==1);}
 {DMA d;DMABuffer b;assert(prepareTXSnapshot(d,b,59,copy,source)==DMAStatus::InvalidState);
  assert(prepareTXSnapshot(d,b,9019,copy,source)==DMAStatus::InvalidState);assert(!d.allocations);}
 {DMA d;DMABuffer b;std::vector<uint8_t> frame(9018,0x5a);frame[12]=0x81;frame[13]=0;
  assert(prepareTXSnapshot(d,b,frame.size(),copy,frame.data())==DMAStatus::Ok);
  assert(std::memcmp(d.bytes(b),frame.data(),frame.size())==0);d.unmap(b);d.complete(b);d.release(b);assert(d.releases==d.allocations);}
 // Actual submit/completion lifetime and ring wrap, with source mutation after copy.
 TraceBackend io;DMA d;auto rings=runStage2A(io,d);assert(rings.complete);NetworkResult n;Sink sink;
 assert(prepareNetwork(d,rings,n));assert(startNetwork(io,d,rings,n));
 for(unsigned cycle=0;cycle<3;++cycle){
  for(unsigned i=0;i<64;++i){
   std::memset(source,int(i+cycle),sizeof(source));DMABuffer b;
   assert(prepareTXSnapshot(d,b,sizeof(source),copy,source)==DMAStatus::Ok);
   auto *owned=static_cast<const uint8_t*>(d.bytes(b));std::memset(source,0xee,sizeof(source));
   for(unsigned j=0;j<sizeof(source);++j)assert(owned[j]==uint8_t(i+cycle));
   assert(submitNetwork(io,d,rings,n,b,reinterpret_cast<void*>(uintptr_t(i+1)))==NetworkStatus::Ok);
   assert(!b.opaque);assert(n.tx[4*i+3]!=0);assert((n.tx[4*i]&((1U<<29)|(1U<<28)))==((1U<<29)|(1U<<28)));
  }
  const unsigned releases=d.releases;assert(sink.freed==cycle*64);
  assert(serviceNetwork(io,d,rings,n,sink,4));assert(d.releases==releases); // OWN still set.
  for(unsigned i=0;i<64;++i)n.tx[4*i]&=~(1U<<31);
  assert(serviceNetwork(io,d,rings,n,sink,4));assert(sink.freed==(cycle+1)*64);
 }
 assert(releaseNetwork(io,d,rings,n,sink));teardownStage2A(io,d,rings);assert(d.releases==d.allocations);
 std::puts("PASS: private TX bytes survive source mutation, 64-bit IOVA, OWN lifetime, three wraps and copy/map failure rollback");
}
