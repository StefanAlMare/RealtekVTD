// SPDX-License-Identifier: GPL-2.0-only
#define REALTEK_NETWORK_FIXTURES_ONLY
#include "network.cpp"
#include "../src/NetworkResources.hpp"
struct JumboDMA: DMA {
 TXPool *pool=nullptr;bool rejectLarge=false,splitLarge=false,failPrepareLarge=false,failMapLarge=false;
 DMAStatus allocate(DMABuffer &b,uint64_t size,uint32_t align,DMADirection direction)override{
  if(size==kJumboRXBytes||size==uint64_t(kTXPoolCount)*kJumboTXStride)assert(!bme);
  if(rejectLarge&&(size==kJumboRXBytes||size==uint64_t(kTXPoolCount)*kJumboTXStride))return DMAStatus::AllocationFailed;
  return DMA::allocate(b,size,align,direction);
 }
 DMAStatus prepare(DMABuffer &b)override{
  if(failPrepareLarge&&b.requestedLength==kJumboRXBytes)return DMAStatus::PrepareFailed;
  return DMA::prepare(b);
 }
 DMAStatus map(DMABuffer &b)override{
  if(failMapLarge&&b.requestedLength==kJumboRXBytes)return DMAStatus::MapFailed;
  auto s=DMA::map(b);if(splitLarge&&b.requestedLength==uint64_t(kTXPoolCount)*kJumboTXStride)b.segmentCount=2;return s;
 }
 void *bytes(DMABuffer &b)override{return pool&&pool->owns(b)?pool->bytes(b):DMA::bytes(b);}
 DMAStatus syncForDevice(DMABuffer &b)override{return pool&&pool->owns(b)?pool->synchronize(*this):DMA::syncForDevice(b);}
 void unmap(DMABuffer &b)override{if(pool&&pool->owns(b)){b.mapped=false;return;}DMA::unmap(b);}
 void complete(DMABuffer &b)override{if(pool&&pool->owns(b)){b.prepared=false;return;}DMA::complete(b);}
 void release(DMABuffer &b)override{if(pool&&pool->release(b))return;DMA::release(b);}
};
bool copyJumbo(void *context,void *bytes,size_t size){std::memcpy(bytes,context,size);return true;}
void jumboData(bool direct){
 TraceBackend io;JumboDMA d;d.noIOMMU=direct;TXPool pool;d.pool=&pool;
 auto rings=runStage2A(io,d);NetworkResult n;Sink sink;
 auto resources=prepareNetworkResources(d,rings,n,pool,true);assert(resources.ready&&resources.jumboReady&&!resources.jumboFallback);
 assert(n.bufferSize==16383&&n.mtu==1500);unsigned allocations=d.allocations;
 n.bufferSize=16384;assert(!startNetwork(io,d,rings,n));n.bufferSize=16383;
 for(unsigned mtu:{1500U,4000U,9000U,1500U}){
  assert(stopNetwork(io,d,rings,n,sink));n.mtu=mtu;assert(startNetwork(io,d,rings,n));
  assert(io.mmio[0xda]==16384&&(n.rx[0]&0x3fff)==16383&&(io.mmio[0xe0]&0x60)==0);
  for(unsigned k=0;k<192;++k){bool tagged=k&1;auto frame=ethernetFrame(mtu+(tagged?18:14),tagged,k&2);
   DMABuffer lease;assert(pool.snapshot(lease,frame.size(),copyJumbo,frame.data())==DMAStatus::Ok);
   auto *owned=static_cast<const uint8_t*>(d.bytes(lease));assert(std::memcmp(owned,frame.data(),frame.size())==0);
   const auto original=frame[42];frame[42]^=0xff;assert(owned[42]==original);frame[42]=original;
   const unsigned tx=n.producer,rx=n.rxConsumer;
   assert(submitNetwork(io,d,rings,n,lease,reinterpret_cast<void*>(uintptr_t(k+1)))==NetworkStatus::Ok);
   assert(n.tx[4*tx+1]==0&&(n.tx[4*tx]&0x3fff)==frame.size());n.tx[4*tx]&=~Own;
   auto *bytes=static_cast<uint8_t*>(d.bytes(n.rxBuffers[rx]));std::memcpy(bytes,frame.data(),frame.size());
   std::memset(bytes+frame.size(),0xab,4);n.rx[4*rx]=First|Last|uint32_t(frame.size()+4)|(rx==63?End:0);
   assert(serviceNetwork(io,d,rings,n,sink,5));assert(sink.frame==frame);
   assert((n.rx[4*rx]&0x3fff)==16383&&!pool.leased());
  }
  auto oversize=ethernetFrame(mtu+19,true);DMABuffer lease;
  auto ps=pool.snapshot(lease,oversize.size(),copyJumbo,oversize.data());
  if(ps==DMAStatus::Ok){assert(submitNetwork(io,d,rings,n,lease,reinterpret_cast<void*>(1))==NetworkStatus::InvalidPacket);d.discard(lease);}
  unsigned rx=n.rxConsumer;auto *bytes=static_cast<uint8_t*>(d.bytes(n.rxBuffers[rx]));
  std::memcpy(bytes,oversize.data(),oversize.size());unsigned before=sink.received;
  n.rx[4*rx]=First|Last|uint32_t(oversize.size()+4)|(rx==63?End:0);
  assert(serviceNetwork(io,d,rings,n,sink,1));assert(sink.received==before);
 }
 assert(d.allocations==allocations&&n.rxDelivered==768&&n.txCompleted==768);
 // Full pool, disjoint private data and reuse at maximum VLAN length.
 auto frame=ethernetFrame(9018,true);DMABuffer leases[kTXPoolCount];
 for(unsigned i=0;i<kTXPoolCount;++i){frame[42]=uint8_t(i);assert(pool.snapshot(leases[i],frame.size(),copyJumbo,frame.data())==DMAStatus::Ok);}
 DMABuffer extra;assert(pool.snapshot(extra,frame.size(),copyJumbo,frame.data())==DMAStatus::PoolFull);
 for(unsigned i=0;i<kTXPoolCount;++i){assert(leases[i].segments[0].address>UINT32_MAX);
  assert(static_cast<const uint8_t*>(d.bytes(leases[i]))[42]==i);
  for(unsigned j=i+1;j<kTXPoolCount;++j){auto a=leases[i].segments[0].address,b=leases[j].segments[0].address;assert((a>b?a-b:b-a)>=kJumboTXStride);}
  d.discard(leases[i]);}
 assert(releaseNetwork(io,d,rings,n,sink));teardownStage2A(io,d,rings);assert(pool.shutdown(d));assert(d.allocations==d.releases);
}
void allAllocationFailures(){
 for(unsigned offset=1;offset<=65;++offset){TraceBackend io;JumboDMA d;d.noIOMMU=true;TXPool pool;d.pool=&pool;
  auto rings=runStage2A(io,d);NetworkResult n;Sink sink;d.failAllocation=d.allocations+offset;
  auto resources=prepareNetworkResources(d,rings,n,pool,true);assert(resources.ready&&resources.jumboFallback&&!resources.jumboReady);
  assert(n.bufferSize==2048&&!d.bme);assert(startNetwork(io,d,rings,n));
  assert(releaseNetwork(io,d,rings,n,sink));teardownStage2A(io,d,rings);assert(pool.shutdown(d));assert(d.releases+1==d.allocations);
 }
 {TraceBackend io;JumboDMA d;TXPool pool;d.pool=&pool;auto rings=runStage2A(io,d);NetworkResult n;Sink sink;
  unsigned allocations=d.allocations;d.failOff=true;auto resources=prepareNetworkResources(d,rings,n,pool,true);
  assert(!resources.ready&&d.bme&&d.allocations==allocations);d.failOff=false;
  assert(releaseNetwork(io,d,rings,n,sink));teardownStage2A(io,d,rings);assert(pool.shutdown(d));}
}
void resourceFallback(){
 for(unsigned mode=0;mode<5;++mode){TraceBackend io;JumboDMA d;d.noIOMMU=true;TXPool pool;d.pool=&pool;
  auto rings=runStage2A(io,d);NetworkResult n;Sink sink;
  d.rejectLarge=mode==0;d.splitLarge=mode==1;d.failPrepareLarge=mode==2;d.failMapLarge=mode==3;
  if(mode==4)d.failAllocation=d.allocations+40;
  auto resources=prepareNetworkResources(d,rings,n,pool,true);
  assert(resources.ready&&!resources.jumboReady&&resources.jumboFallback&&n.bufferSize==2048);
  n.mtu=9000;assert(!startNetwork(io,d,rings,n));n.mtu=1500;assert(startNetwork(io,d,rings,n));
  auto frame=ethernetFrame(1518,true);DMABuffer b;assert(pool.snapshot(b,1518,copyJumbo,frame.data())==DMAStatus::Ok);d.discard(b);
  assert(releaseNetwork(io,d,rings,n,sink));teardownStage2A(io,d,rings);assert(pool.shutdown(d));
  assert(d.releases+(mode==4?1:0)==d.allocations);
 }
}
#ifndef REALTEK_JUMBO_FIXTURES_ONLY
int main(){
 CopperCapabilities caps;caps.valid=true;caps.bmsr=8;caps.estatusRead=true;caps.estatus=0x2000;
 for(unsigned xid:{0x2c9U,0x5c8U,0x541U,0x6c0U})assert(jumboIdentityValid(identify(0x10ec,0x8168,xid<<20,true),caps));
 caps.estatus=0xc000;assert(!jumboIdentityValid(identify(0x10ec,0x8168,0x541U<<20,true),caps));
 caps.estatus=0x2000;assert(!jumboIdentityValid(identify(0x10ec,0x8168,0x541U<<20,false),caps));
 unsigned mtu=0;for(unsigned m:{1500U,4000U,9000U})assert(packetSizeToMTU(m+18,9000,mtu)&&mtu==m);
 assert(!packetSizeToMTU(9019,9000,mtu)&&!packetSizeToMTU(1517,9000,mtu)&&!packetSizeToMTU(UINT32_MAX,9000,mtu));
 jumboData(false);jumboData(true);resourceFallback();allAllocationFailures();
 puts("PASS JUMBO1: 1500/4000/9000 VLAN+untagged integrity/wrap, 16383/16384, IOVA64, 65 slots, contiguous/map/prepare failures -> standard fallback");
}
#endif
