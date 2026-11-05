// SPDX-License-Identifier: GPL-2.0-only
#define REALTEK_JUMBO_FIXTURES_ONLY
#include "jumbo.cpp"
#include "../src/MTUTransaction.hpp"
struct MTUIO: TraceBackend {
 bool failEnable=false;
 bool write8(uint16_t off,uint8_t v)override{
  if(failEnable&&off==0x37&&v==12)return false;
  return TraceBackend::write8(off,v);
 }
};
struct Target {
 MTUIO io;JumboDMA dma;TXPool pool;Stage2AResult rings;NetworkResult n;Sink sink;
 unsigned stops=0,starts=0,applies=0,safeStops=0;
 unsigned failStartAt=0,failApplyAt=0;bool persistentPauseFailure=false;
 Target(uint16_t pause=0xc00,bool direct=false){
  dma.noIOMMU=direct;dma.pool=&pool;rings=runStage2A(io,dma);
  auto resources=prepareNetworkResources(dma,rings,n,pool,true);assert(resources.jumboReady);
  io.phy[0xa408]=uint16_t(0x21|pause);io.phy[0xa400]=0x1040;io.phy[0xa402]=0x24;
  assert(start(1500));starts=0;
 }
 ~Target(){dma.failOff=false;assert(releaseNetwork(io,dma,rings,n,sink));
  teardownStage2A(io,dma,rings);assert(pool.shutdown(dma));assert(dma.releases==dma.allocations);}
 bool readPause(uint16_t &bits){RegisterAccess r(io);PauseProgram program(r);return program.read(bits);}
 bool applyPause(uint16_t bits,bool force,bool &restarted,bool &seen){
  assert(!dma.bme);++applies;
  if(applies==failApplyAt||persistentPauseFailure)io.failAt=io.calls+1;
  RegisterAccess r(io);PauseProgram program(r);bool ok=program.apply(bits,force,restarted,seen);io.failAt=0;return ok;
 }
 bool stop(){++stops;return stopNetwork(io,dma,rings,n,sink);}
 bool start(unsigned mtu){++starts;n.mtu=mtu;io.failEnable=starts==failStartAt;
  bool ok=startNetwork(io,dma,rings,n);io.failEnable=false;return ok;}
 void safeStop(){++safeStops;stop();}
};
void programFailures(){
 TraceBackend good;good.phy[0xa408]=0x8de1;good.phy[0xa400]=0x1040;good.phy[0xa402]=0x24;
 RegisterAccess r(good);PauseProgram p(r);bool restarted=false,seen=false;
 assert(p.apply(0,true,restarted,seen)&&restarted&&seen&&good.phy[0xa408]==0x81e1);
 for(size_t at=1;at<=good.calls;++at){TraceBackend io;io.phy[0xa408]=0x8de1;io.phy[0xa400]=0x1040;io.phy[0xa402]=0x24;
  io.failAt=at;RegisterAccess access(io);PauseProgram program(access);assert(!program.apply(0,true,restarted,seen));}
 for(unsigned other=1;other<65536;other+=32){
  TraceBackend io;io.phy[0xa408]=other;io.phy[0xa400]=0x1040;io.phy[0xa402]=0x24;
  RegisterAccess access(io);PauseProgram program(access);
  assert(program.apply(0x800,true,restarted,seen));assert(io.phy[0xa408]==((other&~0xc00U)|0x800));
 }
 for(unsigned bad:{0xffffU,0x8000U,0x4000U,0x0800U,0x0400U,0U}){
  TraceBackend io;io.phy[0xa408]=0xc21;io.phy[0xa400]=bad;RegisterAccess access(io);PauseProgram program(access);
  assert(!program.apply(0,true,restarted,seen));assert(io.phy[0xa408]==0xc21);
 }
}
void cyclesAndWake(){
 for(bool direct:{false,true})for(unsigned mask:{0U,0x400U,0x800U,0xc00U}){
  Target t(uint16_t(mask),direct);MTUTransaction transaction;transaction.policyApplied=true;
  unsigned allocations=t.dma.allocations;
  for(unsigned cycle=0;cycle<20;++cycle){
   for(unsigned mtu:{4000U,9000U,1500U}){
    assert(transaction.change(t,mtu));assert(transaction.mtu==mtu&&t.n.mtu==mtu&&t.n.running);
    assert(transaction.savedPause==mask&&transaction.savedValid&&transaction.policyApplied);
    assert((t.io.phy[0xa408]&0xc00)==(mtu==1500?mask:0));
    assert((t.io.phy[0xa408]&~0xc00U)==0x21);assert(!t.n.unsafeRetained);
   }
  }
  assert(t.dma.allocations==allocations&&transaction.sequence==60);
  // Sleep/disabled configuration: no PHY write until reapply, then recover saved policy after reset.
  assert(t.stop());transaction.offline(9000);t.n.mtu=9000;
  t.io.phy[0xa408]=0xc21;assert(transaction.reapply(t));assert((t.io.phy[0xa408]&0xc00)==0);
  assert(transaction.savedPause==mask);assert(t.start(9000));assert(t.stop());
  transaction.offline(1500);t.io.phy[0xa408]=0x21;assert(transaction.reapply(t));
  assert((t.io.phy[0xa408]&0xc00)==mask&&transaction.savedPause==mask);assert(t.start(1500));
 }
}
void rollbackAndSafeStop(){
 for(unsigned fail=0;fail<4;++fail){Target t;MTUTransaction transaction;transaction.policyApplied=true;
  if(fail>=2)assert(transaction.change(t,9000));
  const auto oldMTU=transaction.mtu;const auto oldPause=t.io.phy[0xa408]&0xc00;
  if(fail%2)t.failStartAt=t.starts+1;else t.failApplyAt=t.applies+1;
  assert(!transaction.change(t,oldMTU==1500?9000:1500));
  assert(transaction.mtu==oldMTU&&t.n.mtu==oldMTU&&t.n.running&&t.dma.bme);
  assert((t.io.phy[0xa408]&0xc00)==oldPause&&transaction.state!=MTUState::SAFE_STOP);
  assert(transaction.failures==1&&transaction.policyApplied);
 }
 {Target t;MTUTransaction transaction;transaction.policyApplied=true;t.persistentPauseFailure=true;
  unsigned releases=t.dma.releases;assert(!transaction.change(t,9000));
  assert(transaction.state==MTUState::SAFE_STOP&&!t.n.running&&!t.dma.bme&&t.safeStops==1);
  assert(t.dma.releases==releases);}
 {Target t;MTUTransaction transaction;transaction.policyApplied=true;
  auto frame=ethernetFrame(1518,true);DMABuffer lease;
  assert(t.pool.snapshot(lease,1518,copyJumbo,frame.data())==DMAStatus::Ok);
  assert(submitNetwork(t.io,t.dma,t.rings,t.n,lease,reinterpret_cast<void*>(1))==NetworkStatus::Ok);
  t.dma.failOff=true;unsigned releases=t.dma.releases;
  assert(!transaction.change(t,9000));assert(transaction.state==MTUState::SAFE_STOP&&t.n.unsafeRetained);
  assert(t.dma.bme&&t.dma.releases==releases&&t.pool.leased()==1&&t.sink.freed==0);}
}
void epochGuard(){
 MTUTransaction transaction;transaction.policyApplied=true;transaction.awaitingAutoneg=true;
 PHYDiagnostic d;d.valid=0x11f;d.bmcr=0x1040;d.bmsrFirst=d.bmsrSecond=0x24;
 d.anar=d.anlpar=0xc01;d.anlpar|=0x4000;d.raw=0x73;
 auto p=resolvePause(d);assert(p.mediumFlow);transaction.observe(d,p);assert(!p.valid&&!p.mediumFlow&&transaction.awaitingAutoneg);
 d.bmsrFirst&=~0x20; p=resolvePause(d);transaction.observe(d,p);assert(p.valid&&p.mediumFlow&&!transaction.awaitingAutoneg);
 transaction.mtu=9000;p=resolvePause(d);transaction.observe(d,p);assert(!p.valid&&!p.mediumFlow);
 transaction.offline(1500);p=resolvePause(d);transaction.observe(d,p);assert(!p.valid&&!p.mediumFlow);
}
int main(){programFailures();cyclesAndWake();rollbackAndSafeStop();epochGuard();
 puts("PASS PAUSE-TXN1: 4 exact masks x 20 cycles x 2 DMA modes, suspend/reset reapply, per-access failures, bidirectional rollback, post-BME enable failure, unsafe retention, no stale AN reporting");}
