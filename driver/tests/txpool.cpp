// SPDX-License-Identifier: GPL-2.0-only
#include "../src/TXPool.hpp"
#include "../src/NetworkProgram.hpp"
#include "TraceBackend.hpp"
#include <vector>
#include <cstring>
#include <cassert>
#include <cstdio>
#include <thread>
using namespace realtek_next;
struct Memory{std::vector<uint8_t> data;explicit Memory(size_t size):data(size){}};
struct DMA: DMAProvider{
 TXPool *pool=nullptr;bool noIOMMU=false;
 unsigned allocations=0,releases=0,unmaps=0,completes=0;int fail=0;bool bme=false,failOff=false;
 bool deviceMapperPresent()const override{return fail!=1&&!noIOMMU;}
 bool dmaReady()const override{return fail!=1;}
 DMAStatus allocate(DMABuffer &b,uint64_t len,uint32_t alignment,DMADirection direction)override{
  assert(alignment==(direction==DMADirection::FromDevice?16U:256U));assert(direction==DMADirection::ToDevice||direction==DMADirection::Bidirectional||direction==DMADirection::FromDevice);
  if(fail==2)return DMAStatus::AllocationFailed;
  b.opaque=new Memory(len);b.allocated=true;b.requestedLength=len;++allocations;return DMAStatus::Ok;
 }
 void *bytes(DMABuffer &b)override{if(pool&&pool->owns(b))return pool->bytes(b);return fail==3?nullptr:static_cast<Memory*>(b.opaque)->data.data();}
 DMAStatus prepare(DMABuffer &b)override{if(fail==4)return DMAStatus::PrepareFailed;b.prepared=true;return DMAStatus::Ok;}
 DMAStatus map(DMABuffer &b)override{
  if(fail==5)return DMAStatus::MapFailed;
  b.mapped=true;b.mappedLength=b.requestedLength;b.segmentCount=fail==6?2:1;
  b.segments[0]={fail==8?UINT64_MAX-10:0x1234567800ULL,b.requestedLength-(fail==7?1:0)};return DMAStatus::Ok;
 }
 DMAStatus zero(DMABuffer &b)override{std::memset(bytes(b),0,size_t(b.requestedLength));return DMAStatus::Ok;}
 DMAStatus syncForDevice(DMABuffer &b)override{return pool&&pool->owns(b)?pool->synchronize(*this):DMAStatus::Ok;}
 DMAStatus syncForCPU(DMABuffer &)override{return DMAStatus::Ok;}
 void memoryBarrier()override{}
 void unmap(DMABuffer &b)override{if(pool&&pool->owns(b)){b.mapped=false;return;}assert(b.mapped);b.mapped=false;++unmaps;}
 void complete(DMABuffer &b)override{if(pool&&pool->owns(b)){b.prepared=false;return;}assert(b.prepared);b.prepared=false;++completes;}
 void release(DMABuffer &b)override{if(pool&&pool->release(b))return;assert(!b.mapped&&!b.prepared);delete static_cast<Memory*>(b.opaque);b=DMABuffer{};++releases;}
 DMAStatus setBusMaster(bool enabled)override{if(!enabled&&failOff)return DMAStatus::BusMasterFailed;bme=enabled;return DMAStatus::Ok;}
};
bool copy(void *source,void *destination,size_t size){std::memcpy(destination,source,size);return true;}
bool badCopy(void *,void *,size_t){return false;}
struct Sink: PacketSink{
 unsigned freed=0;
 bool receive(const uint8_t *,unsigned)override{return true;}
 void releasePacket(void *packet)override{assert(packet);++freed;}
};
void scenario(bool direct){
 uint8_t source[1514];std::memset(source,0x5a,sizeof(source));
 for(int fail=1;fail<=8;++fail){DMA d;d.noIOMMU=direct;d.fail=fail;TXPool pool;
  assert(!pool.initialize(d));assert(d.allocations==d.releases);assert(!pool.ready());
 }
 DMA d;d.noIOMMU=direct;TXPool pool;d.pool=&pool;assert(pool.initialize(d));
 assert(pool.iova()>UINT32_MAX);assert(d.allocations==1);
 DMABuffer leases[kTXPoolCount];
 // Concurrent reservation produces disjoint IOVAs, each backed by private bytes.
 std::vector<std::thread> threads;
 for(unsigned i=0;i<kTXPoolCount;++i)threads.emplace_back([&,i]{assert(pool.snapshot(leases[i],sizeof(source),copy,source)==DMAStatus::Ok);});
 for(auto &thread:threads)thread.join();assert(pool.leased()==kTXPoolCount);
 std::memset(source,0,sizeof(source));
 for(unsigned i=0;i<kTXPoolCount;++i){assert(static_cast<uint8_t*>(d.bytes(leases[i]))[0]==0x5a);
  for(unsigned j=0;j<i;++j)assert(leases[i].segments[0].address!=leases[j].segments[0].address);}
 DMABuffer extra;assert(pool.snapshot(extra,60,copy,source)==DMAStatus::PoolFull);
 const unsigned releases=d.releases;assert(!pool.shutdown(d));assert(d.releases==releases);
 for(auto &lease:leases){d.unmap(lease);d.complete(lease);d.release(lease);}assert(!pool.leased());
 assert(pool.snapshot(extra,60,badCopy,source)==DMAStatus::InvalidState);assert(!pool.leased());
 TraceBackend io;auto rings=runStage2A(io,d);assert(rings.complete);NetworkResult n;Sink sink;
 assert(prepareNetwork(d,rings,n));assert(startNetwork(io,d,rings,n));
 const unsigned allocations=d.allocations,unmaps=d.unmaps;
 for(unsigned cycle=0;cycle<3;++cycle){
  for(unsigned i=0;i<64;++i){DMABuffer lease;assert(pool.snapshot(lease,1514,copy,source)==DMAStatus::Ok);
   assert(submitNetwork(io,d,rings,n,lease,reinterpret_cast<void*>(uintptr_t(i+1)))==NetworkStatus::Ok);}
  assert(pool.leased()==64);assert(serviceNetwork(io,d,rings,n,sink,4));assert(pool.leased()==64);
  // Spare slot permits mapping before discovering a full ring, then returns on stall.
  DMABuffer spare;assert(pool.snapshot(spare,60,copy,source)==DMAStatus::Ok);
  assert(submitNetwork(io,d,rings,n,spare,reinterpret_cast<void*>(1))==NetworkStatus::Full);
  d.unmap(spare);d.complete(spare);d.release(spare);
  for(unsigned i=0;i<64;++i)n.tx[4*i]&=~(1U<<31);
  assert(serviceNetwork(io,d,rings,n,sink,4));assert(!pool.leased());
  assert(d.allocations==allocations&&d.unmaps==unmaps); // No DMA allocations/map teardown per frame.
 }
 DMABuffer pending;assert(pool.snapshot(pending,60,copy,source)==DMAStatus::Ok);
 assert(submitNetwork(io,d,rings,n,pending,reinterpret_cast<void*>(1))==NetworkStatus::Ok);
 d.failOff=true;assert(!stopNetwork(io,d,rings,n,sink));assert(pool.leased()==1);assert(!pool.shutdown(d));
 d.failOff=false;assert(stopNetwork(io,d,rings,n,sink));assert(!d.bme&&!pool.leased());
 assert(startNetwork(io,d,rings,n));assert(pool.ready()); // Same mapping survives disable/enable.
 assert(releaseNetwork(io,d,rings,n,sink));teardownStage2A(io,d,rings);
 assert(pool.shutdown(d));assert(d.allocations==d.releases);
 std::puts("PASS: TX pool concurrent reservations, private bytes, native64, exhaustion, OWN lifetime, three wraps, restart and allocation/map rollback");
}

int main(){scenario(false);scenario(true);}
