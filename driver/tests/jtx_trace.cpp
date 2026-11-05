// SPDX-License-Identifier: GPL-2.0-only
#define REALTEK_JUMBO_FIXTURES_ONLY
#include "jumbo.cpp"
#include <string>

static uint64_t fakeClock(void *p){return ++*static_cast<uint64_t*>(p);}
struct LoggedDMA: JumboDMA {
 std::vector<std::string> log;
 bool failRingSync=false;
 void entry(const char *s,const DMABuffer &b){log.push_back(std::string(s)+":"+std::to_string(b.requestedLength)+":"+std::to_string(b.segmentCount?b.segments[0].address:0));}
 DMAStatus syncForDevice(DMABuffer &b)override{entry("device",b);if(failRingSync&&b.requestedLength==kTXRingLength)return DMAStatus::SyncFailed;return JumboDMA::syncForDevice(b);}
 DMAStatus syncForCPU(DMABuffer &b)override{entry("cpu",b);return DMA::syncForCPU(b);}
 void memoryBarrier()override{log.push_back("barrier");}
 DMAStatus setBusMaster(bool on)override{log.push_back(on?"bme-on":"bme-off");return DMA::setBusMaster(on);}
 void unmap(DMABuffer &b)override{entry("unmap",b);JumboDMA::unmap(b);}
 void complete(DMABuffer &b)override{entry("complete",b);JumboDMA::complete(b);}
 void release(DMABuffer &b)override{entry("release",b);JumboDMA::release(b);}
};
struct JTXFixture {
 TraceBackend io;LoggedDMA dma;TXPool pool;Stage2AResult rings;NetworkResult n;Sink sink;
 JTXTrace trace;uint64_t clock=1000;
 explicit JTXFixture(bool traced=true,bool direct=false){
  dma.pool=&pool;dma.noIOMMU=direct;rings=runStage2A(io,dma);
  assert(prepareNetworkResources(dma,rings,n,pool,true).jumboReady);
  trace.setClock(fakeClock,&clock);if(traced)n.trace=&trace;
  assert(startNetwork(io,dma,rings,n));
 }
 ~JTXFixture(){dma.failOff=dma.failSync=dma.failRingSync=false;io.failWriteOffset=0xffff;
  assert(releaseNetwork(io,dma,rings,n,sink));teardownStage2A(io,dma,rings);assert(pool.shutdown(dma));}
};
std::vector<uint8_t> probe(unsigned length,bool tagged,unsigned seq){
 auto frame=ethernetFrame(length,tagged);const unsigned o=tagged?18:14;
 frame[0]=0x02;frame[1]=0x11;frame[2]=0x22;frame[3]=0x33;frame[4]=0x44;frame[5]=0x55;
 frame[o]=0x45;frame[o+2]=uint8_t((length-o)>>8);frame[o+3]=uint8_t(length-o);
 frame[o+4]=0x12;frame[o+5]=uint8_t(seq);frame[o+6]=0x40;frame[o+7]=0;
 frame[o+9]=1;frame[o+20]=8;frame[o+24]=0xca;frame[o+25]=0xfe;
 frame[o+26]=uint8_t(seq>>8);frame[o+27]=uint8_t(seq);return frame;
}
uint64_t snapshot(JTXFixture &f,DMABuffer &b,std::vector<uint8_t> &frame){
 assert(f.pool.snapshot(b,frame.size(),copyJumbo,frame.data())==DMAStatus::Ok);
 JTXCopyEvidence copy;copy.attempted=copy.copied=true;
 return f.trace.begin(frame.data(),true,unsigned(frame.size()),f.n.mtu,3,100,101,0,copy,&b,
  static_cast<const uint8_t*>(f.dma.bytes(b)),f.pool.iova(),f.pool.slotStride());
}
void completeOne(JTXFixture &f,unsigned descriptor,uint32_t raw=0){
 f.n.tx[4*descriptor]=(f.n.tx[4*descriptor]&~Own)|raw;
 assert(serviceNetwork(f.io,f.dma,f.rings,f.n,f.sink,5));
}
struct Result {std::vector<Event> mmio;std::vector<std::string> dma;uint64_t submitted,completed;unsigned freed;};
Result traffic(bool traced,bool direct){
 JTXFixture f(traced,direct);
 struct Size {unsigned length,mtu;bool tagged;};
 const Size cases[]={{1514,1500,false},{1518,1500,true},{1518,4000,false},{1614,4000,false},
  {1618,4000,true},{4014,4000,false},{4018,4000,true},{9014,9000,false},{9018,9000,true}};
 unsigned seq=0;
 for(auto size:cases){assert(stopNetwork(f.io,f.dma,f.rings,f.n,f.sink));f.n.mtu=size.mtu;assert(startNetwork(f.io,f.dma,f.rings,f.n));
  for(unsigned i=0;i<70;++i){auto frame=probe(size.length,size.tagged,seq++);DMABuffer b;
   const auto id=traced?snapshot(f,b,frame):0;
   if(!traced)assert(f.pool.snapshot(b,frame.size(),copyJumbo,frame.data())==DMAStatus::Ok);
   const unsigned desc=f.n.producer;
   const auto status=submitNetwork(f.io,f.dma,f.rings,f.n,b,reinterpret_cast<void*>(1),id);
   assert(status==NetworkStatus::Ok);if(traced)f.trace.finish(id,uint32_t(status),true);
   assert((f.n.tx[4*desc]&0x3fff)==size.length);assert((f.n.tx[4*desc]&(Own|First|Last))==(Own|First|Last));
   completeOne(f,desc);assert(!f.pool.leased());
  }
 }
 assert(f.n.txCompleted==630&&f.n.txSubmitted==630&&f.sink.freed==630);
 if(traced){assert(f.trace.count()==64&&f.trace.suppressed()==426);
  assert(f.trace.record(0)->length==1518&&f.trace.record(0)->icmpSequence==140);}
 Result r{f.io.events,f.dma.log,f.n.txSubmitted,f.n.txCompleted,f.sink.freed};return r;
}
void equivalence(){for(bool direct:{false,true}){
 auto a=traffic(false,direct),b=traffic(true,direct);assert(a.dma==b.dma&&a.mmio.size()==b.mmio.size());
 for(unsigned i=0;i<a.mmio.size();++i)assert(a.mmio[i].kind==b.mmio[i].kind&&a.mmio[i].address==b.mmio[i].address&&a.mmio[i].value==b.mmio[i].value);
 assert(a.submitted==b.submitted&&a.completed==b.completed&&a.freed==b.freed);
}}
bool failedCopy(void *,void *,size_t){return false;}
void jtxFailures(){
 {JTXFixture f;f.n.mtu=4000;auto frame=probe(4014,false,1);DMABuffer b;
  assert(f.pool.snapshot(b,frame.size(),failedCopy,nullptr)==DMAStatus::InvalidState);assert(!f.pool.leased());
  JTXCopyEvidence copy;copy.attempted=true;
  auto id=f.trace.begin(frame.data(),true,4014,4000,1,1,2,uint32_t(DMAStatus::InvalidState),copy,nullptr,nullptr,0,0);
  f.trace.finish(id,uint32_t(NetworkStatus::Mapping),false);assert(f.trace.record(0)->copyFlags==3&&f.trace.record(0)->terminal==4);}
 for(unsigned mode=0;mode<4;++mode){JTXFixture f;f.n.mtu=4000;auto frame=probe(4014,false,2);DMABuffer b;
  auto id=snapshot(f,b,frame);if(mode==0)f.dma.failSync=true;if(mode==1)f.io.failWriteOffset=0x38;
  if(mode==3)f.dma.failRingSync=true;
  auto status=submitNetwork(f.io,f.dma,f.rings,f.n,b,reinterpret_cast<void*>(1),id);
  f.trace.finish(id,uint32_t(status),!b.opaque);
  if(mode==0){assert(status==NetworkStatus::Sync&&b.opaque);f.dma.failSync=false;f.dma.discard(b);}
  if(mode==1){assert(status==NetworkStatus::IO&&!b.opaque);assert(f.trace.record(0)->flags&JTXKickFailed);}
  if(mode==2){f.dma.failOff=true;const auto freed=f.sink.freed;assert(!stopNetwork(f.io,f.dma,f.rings,f.n,f.sink));
   assert(f.trace.record(0)->flags&JTXRetained);assert(!f.trace.record(0)->terminal&&f.sink.freed==freed&&f.pool.leased()==1);
   f.dma.failOff=false;assert(stopNetwork(f.io,f.dma,f.rings,f.n,f.sink));assert(f.trace.record(0)->terminal==2);}
  if(mode==3){assert(status==NetworkStatus::IO&&!b.opaque);const auto flags=f.trace.record(0)->flags;
   assert((flags&JTXRingSyncAttempted)&&!(flags&JTXRingSynced)&&!(flags&JTXKickAttempted));}
 }
 {JTXFixture f;f.n.mtu=4000;auto small=probe(1514,false,0);
  for(unsigned i=0;i<64;++i){DMABuffer b;assert(f.pool.snapshot(b,small.size(),copyJumbo,small.data())==DMAStatus::Ok);
   assert(submitNetwork(f.io,f.dma,f.rings,f.n,b,reinterpret_cast<void*>(1))==NetworkStatus::Ok);}
  auto frame=probe(4014,false,7);DMABuffer b;auto id=snapshot(f,b,frame);
  assert(submitNetwork(f.io,f.dma,f.rings,f.n,b,reinterpret_cast<void*>(1),id)==NetworkStatus::Full);
  f.trace.finish(id,uint32_t(NetworkStatus::Full),false,true);assert(f.trace.record(0)->terminal==3);
  for(unsigned i=0;i<64;++i)f.n.tx[i*4]&=~Own;
  assert(serviceNetwork(f.io,f.dma,f.rings,f.n,f.sink,4));
  assert(submitNetwork(f.io,f.dma,f.rings,f.n,b,reinterpret_cast<void*>(1),id)==NetworkStatus::Ok);completeOne(f,0);
  assert(f.trace.record(0)->flags&JTXCompleted);}
}
void rawAndWire(const char *output){
 JTXFixture f;f.n.mtu=4000;JTXPHY phy;phy.observedNS=99;phy.valid=0x11f;phy.raw=0x13;
 phy.bmcr=0x1040;phy.bmsrFirst=phy.bmsrSecond=0x24;phy.anar=481;phy.anlpar=0x41e1;phy.anPending=1;phy.mtuState=1;
 f.trace.observePHY(phy);
 for(unsigned seq=0;seq<10;++seq){auto frame=probe(4014,false,seq);DMABuffer b;auto id=snapshot(f,b,frame);const auto desc=f.n.producer;
  auto status=submitNetwork(f.io,f.dma,f.rings,f.n,b,reinterpret_cast<void*>(1),id);f.trace.finish(id,uint32_t(status),true);
  completeOne(f,desc,seq>=1&&seq<=5?0x00800000U:0);
  const auto r=f.trace.record(seq);assert(r->icmpSequence==seq&&r->icmpID==0xcafe&&r->destination[5]==0x55);
  assert(r->copyFlags==15&&r->iovaEnd-r->iova+1==4014&&r->slot<65);
  assert(r->phyPublish.anPending&&r->phyPublish.observedNS==99);
  assert(bool(r->completeOpts1&0x00800000U)==(seq>=1&&seq<=5));assert(f.n.tx[4*desc]==0);
 }
 unsigned bytes=0;const auto data=f.trace.encode(bytes);assert(bytes==128+10*320&&data[0]=='J');
 if(output){auto file=std::fopen(output,"wb");assert(file);assert(std::fwrite(data,1,bytes,file)==bytes);assert(std::fclose(file)==0);}
}
int main(int argc,char **argv){static_assert(sizeof(JTXTrace)<65536,"Bounded diagnostic object");
 equivalence();jtxFailures();rawAndWire(argc>1?argv[1]:nullptr);
 puts("PASS JTX1: exact MMIO/DMA trace equivalence ON/OFF in two DMA modes, 9 frame profiles, wrap/freeze, copy/sync/kick failures, RingFull retry, BME retention, raw completions and bounded LE export");
}
