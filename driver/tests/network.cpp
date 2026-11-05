// SPDX-License-Identifier: GPL-2.0-only
#include "TraceBackend.hpp"
#include "../src/NetworkProgram.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace realtek_next;
constexpr uint32_t Own=1U<<31,End=1U<<30,First=1U<<29,Last=1U<<28;
struct Memory {std::vector<uint8_t> data;explicit Memory(size_t size):data(size){};};
class DMA:public DMAProvider {
public:
 bool noIOMMU=false;
 unsigned allocations=0,releases=0,failAllocation=0;bool bme=false,failOff=false,failSync=false;
 uint64_t next=0x1234000000ULL;
 bool deviceMapperPresent() const override{return !noIOMMU;}
 bool dmaReady() const override{return true;}
 DMAStatus allocate(DMABuffer &b,uint64_t size,uint32_t,DMADirection)override{
  if(++allocations==failAllocation)return DMAStatus::AllocationFailed;
  b.opaque=new Memory(size);b.requestedLength=size;b.allocated=true;return DMAStatus::Ok;
 }
 DMAStatus prepare(DMABuffer &b)override{b.prepared=true;return DMAStatus::Ok;}
 DMAStatus map(DMABuffer &b)override{b.mapped=true;b.mappedLength=b.requestedLength;b.segmentCount=1;b.segments[0]={next,b.requestedLength};next+=0x10000;return DMAStatus::Ok;}
 DMAStatus zero(DMABuffer &b)override{std::memset(bytes(b),0,size_t(b.requestedLength));return DMAStatus::Ok;}
 void *bytes(DMABuffer &b)override{return b.opaque?static_cast<Memory*>(b.opaque)->data.data():nullptr;}
 DMAStatus syncForDevice(DMABuffer &)override{return failSync?DMAStatus::SyncFailed:DMAStatus::Ok;}
 DMAStatus syncForCPU(DMABuffer &)override{return failSync?DMAStatus::SyncFailed:DMAStatus::Ok;}
 void memoryBarrier()override{}
 void unmap(DMABuffer &b)override{b.mapped=false;}
 void complete(DMABuffer &b)override{b.prepared=false;}
 void release(DMABuffer &b)override{assert(!b.mapped&&!b.prepared);delete static_cast<Memory*>(b.opaque);b=DMABuffer{};++releases;}
 DMAStatus setBusMaster(bool on)override{if(!on&&failOff)return DMAStatus::BusMasterFailed;bme=on;return DMAStatus::Ok;}
 DMABuffer packet(unsigned segments=1){DMABuffer b;assert(allocate(b,60,1,DMADirection::ToDevice)==DMAStatus::Ok);prepare(b);map(b);
  if(segments>1){b.segmentCount=segments;for(unsigned i=0;i<segments;++i)b.segments[i]={0x9876000000ULL+i*0x10000,60/segments};}return b;}
 void discard(DMABuffer &b){unmap(b);complete(b);release(b);}
};
struct Sink:PacketSink {
 unsigned received=0,freed=0;bool accept=true;std::vector<uint8_t> frame;
 bool receive(const uint8_t *bytes,unsigned size)override{frame.assign(bytes,bytes+size);++received;return accept;}
 void releasePacket(void *p)override{assert(p);++freed;}
};
struct Fixture {
 TraceBackend io;DMA dma;Stage2AResult a;NetworkResult n;Sink sink;
 Fixture(bool direct=false){dma.noIOMMU=direct;a=runStage2A(io,dma);assert(a.complete&&a.mapperPresent==!direct);assert(prepareNetwork(dma,a,n));assert(startNetwork(io,dma,a,n));}
 ~Fixture(){assert(releaseNetwork(io,dma,a,n,sink));teardownStage2A(io,dma,a);assert(dma.releases==dma.allocations);}
 void completeTX(unsigned descriptors){unsigned i=n.consumer;for(unsigned k=0;k<descriptors;++k){n.tx[4*i]&=~Own;i=(i+1)%64;}}
 void receive(unsigned i,bool invalid=false){auto b=static_cast<uint8_t*>(dma.bytes(n.rxBuffers[i]));for(unsigned j=0;j<64;++j)b[j]=uint8_t(j);
  n.rx[4*i]=First|Last|64|(i==63?End:0)|(invalid?(1U<<19):0);}
};
void cycles(bool direct=false){Fixture f(direct);
 // Three complete wraps, not the finite probe's 32/64 limits; 64-bit IOVAs are preserved.
 for(unsigned cycle=0;cycle<3;++cycle){for(unsigned i=0;i<64;++i){auto p=f.dma.packet();
  assert(submitNetwork(f.io,f.dma,f.a,f.n,p,reinterpret_cast<void*>(uintptr_t(i+1)))==NetworkStatus::Ok);assert(!p.opaque);
  assert(f.n.tx[4*i+3]!=0);assert(bool(f.n.tx[4*i]&End)==(i==63));
 }
 auto full=f.dma.packet();assert(submitNetwork(f.io,f.dma,f.a,f.n,full,reinterpret_cast<void*>(1))==NetworkStatus::Full);assert(full.opaque);f.dma.discard(full);
 assert(f.sink.freed==cycle*64); // OWN must return before any packet is freed.
 f.completeTX(64);for(unsigned i=0;i<64;++i)f.receive(i);
 assert(serviceNetwork(f.io,f.dma,f.a,f.n,f.sink,5));assert(f.n.used==0&&f.n.running);
 assert(f.sink.freed==(cycle+1)*64&&f.n.rxDelivered==(cycle+1)*64);
 for(unsigned i=0;i<64;++i)assert(f.n.rx[4*i]&Own);
 assert(f.sink.frame.size()==60&&f.sink.frame.back()==59); // Strip CRC bytes 60..63.
 }
 assert(f.n.txCompleted==192&&f.n.rxDelivered==192);
 assert(stopNetwork(f.io,f.dma,f.a,f.n,f.sink));assert(!f.dma.bme);assert(startNetwork(f.io,f.dma,f.a,f.n));
 auto p=f.dma.packet();assert(submitNetwork(f.io,f.dma,f.a,f.n,p,reinterpret_cast<void*>(1))==NetworkStatus::Ok);
}
void fragmentedWrap(){Fixture f;
 // Position producer at 63, then wrap a two-segment packet across ring end.
 for(unsigned i=0;i<63;++i){auto p=f.dma.packet();assert(submitNetwork(f.io,f.dma,f.a,f.n,p,reinterpret_cast<void*>(1))==NetworkStatus::Ok);}
 f.completeTX(63);assert(serviceNetwork(f.io,f.dma,f.a,f.n,f.sink,4));
 auto p=f.dma.packet(2);assert(submitNetwork(f.io,f.dma,f.a,f.n,p,reinterpret_cast<void*>(2))==NetworkStatus::Ok);
 assert((f.n.tx[63*4]&(Own|First|End|Last))==(Own|First|End));assert((f.n.tx[0]&(Own|First|Last|End))==(Own|Last));
 const unsigned freed=f.sink.freed;f.completeTX(1);assert(serviceNetwork(f.io,f.dma,f.a,f.n,f.sink,4));assert(f.sink.freed==freed);
 f.completeTX(1);assert(serviceNetwork(f.io,f.dma,f.a,f.n,f.sink,4));assert(f.sink.freed==freed+1);
}
void drops(){Fixture f;f.receive(0,true);f.receive(1);f.sink.accept=false;assert(serviceNetwork(f.io,f.dma,f.a,f.n,f.sink,1));
 assert(f.n.rxDropped==2&&f.n.rxDelivered==0);assert((f.n.rx[0]&Own)&&(f.n.rx[4]&Own));
 auto p=f.dma.packet();p.segments[0].address=UINT64_MAX-10;
 assert(submitNetwork(f.io,f.dma,f.a,f.n,p,reinterpret_cast<void*>(1))==NetworkStatus::InvalidPacket);f.dma.discard(p);
}
void unsafe(){Fixture f;auto p=f.dma.packet();assert(submitNetwork(f.io,f.dma,f.a,f.n,p,reinterpret_cast<void*>(1))==NetworkStatus::Ok);
 const unsigned releases=f.dma.releases;f.dma.failOff=true;
 assert(!stopNetwork(f.io,f.dma,f.a,f.n,f.sink));assert(f.n.unsafeRetained&&f.dma.releases==releases&&f.sink.freed==0);
 assert(!startNetwork(f.io,f.dma,f.a,f.n));f.dma.failOff=false;assert(stopNetwork(f.io,f.dma,f.a,f.n,f.sink));assert(f.sink.freed==1);
}
void failures(){
 for(unsigned offset:{0x20U,0xe4U,0x44U,0x37U}){Fixture f;assert(stopNetwork(f.io,f.dma,f.a,f.n,f.sink));f.io.failWriteOffset=uint16_t(offset);
  assert(!startNetwork(f.io,f.dma,f.a,f.n));f.io.failWriteOffset=0xffff;assert(stopNetwork(f.io,f.dma,f.a,f.n,f.sink));}
 for(unsigned fail=3;fail<67;++fail){TraceBackend io;DMA d;auto a=runStage2A(io,d);NetworkResult n;Sink sink;d.failAllocation=fail;
  assert(!prepareNetwork(d,a,n));assert(releaseNetwork(io,d,a,n,sink));teardownStage2A(io,d,a);assert(d.releases+1==d.allocations);}
 Fixture f;auto p=f.dma.packet();f.io.failWriteOffset=0x38;
 assert(submitNetwork(f.io,f.dma,f.a,f.n,p,reinterpret_cast<void*>(1))==NetworkStatus::IO);assert(!p.opaque&&f.sink.freed==0);
 f.io.failWriteOffset=0xffff;assert(stopNetwork(f.io,f.dma,f.a,f.n,f.sink));assert(f.sink.freed==1);
}

std::vector<uint8_t> ethernetFrame(unsigned length,bool tagged,bool multicast=false){
 std::vector<uint8_t> b(length);for(unsigned i=0;i<length;++i)b[i]=uint8_t(i*37+19);
 if(length>=14){b[0]=multicast?1:2;b[12]=tagged?0x81:0x08;b[13]=0;}
 if(tagged&&length>=18){b[14]=0xb0;b[15]=100;b[16]=0x08;b[17]=0;}
 return b;
}
void vlanSoftware(bool direct){Fixture f(direct);
 for(unsigned k=0;k<192;++k){bool tagged=k&1;auto frame=ethernetFrame(tagged?1518:1514,tagged,k&2);
  DMABuffer p;assert(f.dma.allocate(p,frame.size(),256,DMADirection::ToDevice)==DMAStatus::Ok);
  f.dma.prepare(p);f.dma.map(p);std::memcpy(f.dma.bytes(p),frame.data(),frame.size());
  const unsigned tx=f.n.producer,rx=f.n.rxConsumer;
  assert(submitNetwork(f.io,f.dma,f.a,f.n,p,reinterpret_cast<void*>(uintptr_t(k+1)))==NetworkStatus::Ok);
  assert(f.n.tx[4*tx+1]==0);assert((f.n.tx[4*tx]&0x3fff)==frame.size());
  f.completeTX(1);auto *dst=static_cast<uint8_t*>(f.dma.bytes(f.n.rxBuffers[rx]));
  std::memcpy(dst,frame.data(),frame.size());std::memset(dst+frame.size(),0xc7,4);
  f.n.rx[4*rx]=First|Last|uint32_t(frame.size()+4)|(rx==63?End:0);
  assert(serviceNetwork(f.io,f.dma,f.a,f.n,f.sink,5));assert(f.sink.frame==frame);
  assert((f.n.rx[4*rx]&0x3fff)==2048);assert(f.n.rx[4*rx+1]==0);
 }
 assert(f.n.rxDelivered==192&&f.n.txCompleted==192&&f.n.rxDropped==0);
 for(auto c:{std::make_pair(1515U,false),std::make_pair(1518U,false),std::make_pair(1519U,true),std::make_pair(17U,true)}){
  auto frame=ethernetFrame(c.first,c.second);DMABuffer p;
  assert(f.dma.allocate(p,frame.size(),256,DMADirection::ToDevice)==DMAStatus::Ok);f.dma.prepare(p);f.dma.map(p);
  std::memcpy(f.dma.bytes(p),frame.data(),frame.size());
  assert(submitNetwork(f.io,f.dma,f.a,f.n,p,reinterpret_cast<void*>(1))==NetworkStatus::InvalidPacket);f.dma.discard(p);
  unsigned rx=f.n.rxConsumer;auto *dst=static_cast<uint8_t*>(f.dma.bytes(f.n.rxBuffers[rx]));
  std::memcpy(dst,frame.data(),frame.size());f.n.rx[4*rx]=First|Last|(c.first+4)|(rx==63?End:0);
  unsigned before=f.sink.received;assert(serviceNetwork(f.io,f.dma,f.a,f.n,f.sink,1));assert(f.sink.received==before);
 }
 unsigned rx=f.n.rxConsumer;f.receive(rx);auto span=f.n.rxBuffers[rx].mappedLength;
 f.n.rxBuffers[rx].mappedLength=32;unsigned before=f.sink.received;
 assert(serviceNetwork(f.io,f.dma,f.a,f.n,f.sink,1));assert(f.sink.received==before);f.n.rxBuffers[rx].mappedLength=span;
 puts("PASS VLAN-SW1: tagged/untagged multicast integrity, 3 wraps, TCI/FCS, bounds, both DMA modes");
}
#ifndef REALTEK_NETWORK_FIXTURES_ONLY
int main(){vlanSoftware(false);vlanSoftware(true);cycles();cycles(true);fragmentedWrap();drops();unsafe();failures();puts("PASS: network continuous TX/RX, FCS stripping, fragmented TX wrap, backpressure, restart, allocation rollback and unsafe DMA retention");}

#endif
