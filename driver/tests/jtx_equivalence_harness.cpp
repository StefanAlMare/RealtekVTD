// SPDX-License-Identifier: GPL-2.0-only
// Compile the same workload against the exact EXP1 fixtures/core and JTX1.
#define REALTEK_JUMBO_FIXTURES_ONLY
#include EXP_FIXTURE
#include <string>
struct AuditDMA: JumboDMA {
 std::vector<std::string> log;
 void entry(const char *s,const DMABuffer &b){log.push_back(std::string(s)+":"+std::to_string(b.requestedLength)+":"+std::to_string(b.segmentCount?b.segments[0].address:0));}
 DMAStatus syncForDevice(DMABuffer &b)override{entry("device",b);return JumboDMA::syncForDevice(b);}
 DMAStatus syncForCPU(DMABuffer &b)override{entry("cpu",b);return DMA::syncForCPU(b);}
 void memoryBarrier()override{log.push_back("barrier");}
 DMAStatus setBusMaster(bool on)override{log.push_back(on?"bme-on":"bme-off");return DMA::setBusMaster(on);}
 void unmap(DMABuffer &b)override{entry("unmap",b);JumboDMA::unmap(b);}
 void complete(DMABuffer &b)override{entry("complete",b);JumboDMA::complete(b);}
 void release(DMABuffer &b)override{entry("release",b);JumboDMA::release(b);}
};
int main(){for(bool direct:{false,true}){
 TraceBackend io;AuditDMA dma;TXPool pool;dma.pool=&pool;dma.noIOMMU=direct;
 auto rings=runStage2A(io,dma);NetworkResult n;Sink sink;
#ifdef JTX_OBSERVE
 JTXTrace trace;n.trace=&trace;
#endif
 assert(prepareNetworkResources(dma,rings,n,pool,true).jumboReady);
 for(unsigned mtu:{1500U,4000U,9000U,1500U}){
  assert(stopNetwork(io,dma,rings,n,sink));n.mtu=mtu;assert(startNetwork(io,dma,rings,n));
  for(unsigned seq=0;seq<70;++seq){bool vlan=seq&1;auto frame=ethernetFrame(mtu+(vlan?18:14),vlan);DMABuffer b;
   assert(pool.snapshot(b,frame.size(),copyJumbo,frame.data())==DMAStatus::Ok);uint64_t id=0;
#ifdef JTX_OBSERVE
   JTXCopyEvidence copy;copy.attempted=copy.copied=true;
   id=trace.begin(frame.data(),true,unsigned(frame.size()),mtu,1,1,2,0,copy,&b,
    static_cast<const uint8_t*>(dma.bytes(b)),pool.iova(),pool.slotStride());
#endif
   const unsigned desc=n.producer;
#ifdef JTX_OBSERVE
   auto status=submitNetwork(io,dma,rings,n,b,reinterpret_cast<void*>(1),id);trace.finish(id,uint32_t(status),!b.opaque);
#else
   (void)id;auto status=submitNetwork(io,dma,rings,n,b,reinterpret_cast<void*>(1));
#endif
   assert(status==NetworkStatus::Ok);std::printf("DESC %u %08x %08x %08x %08x\n",desc,n.tx[4*desc],n.tx[4*desc+1],n.tx[4*desc+2],n.tx[4*desc+3]);
   n.tx[4*desc]&=~Own;assert(serviceNetwork(io,dma,rings,n,sink,5));
  }
 }
 assert(releaseNetwork(io,dma,rings,n,sink));teardownStage2A(io,dma,rings);assert(pool.shutdown(dma));
 for(const auto &e:io.events)std::printf("IO %s %u %u\n",e.kind.c_str(),e.address,e.value);
 for(const auto &e:dma.log)std::printf("DMA %s\n",e.c_str());
 std::printf("COUNTERS %llu %llu %u %u %u\n",static_cast<unsigned long long>(n.txSubmitted),static_cast<unsigned long long>(n.txCompleted),sink.freed,dma.allocations,dma.releases);
}}
