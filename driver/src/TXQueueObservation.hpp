// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <stdint.h>
#include <stddef.h>
namespace realtek_next {
// Diagnostic only. No mbuf, controller, queue or DMA pointers in exported data.
// One non-waiting CAS protects bounded copies; misses are explicit, never spun.
struct TXQEvent {
 uint64_t sequence=0,epoch=0,begin=0,end=0,event=0,path=0,site=0,reason=0,consumed=0,validMask=0;
 uint64_t used=0,free=0,producer=0,consumer=0,reclaimed=0,parent4=0,parent6=0;
 uint64_t drops=0,state=0,primarySize=0,observationBegin=0,observationEnd=0,result=0;
};
struct TXQHistogram {uint64_t count=0,sum=0,maximum=0,buckets[8]{};};
struct TXQSnapshot {
 uint64_t schema=1,enabled=0,total=0,published=0,overwritten=0,suppressed=0,histSuppressed=0,clockRegressed=0;
 uint64_t epoch=0,firstDropValid=0,lastDropsValid=0,lastDrops=0,dropCounterRegressed=0;
 uint64_t flushDriverCalls=0,flushDriverPackets=0,flushUnknownCalls=0,flushUnknownPackets=0;
 uint64_t retry[18][2]{},timingThresholdTicks[7]{},slowTicks=0;
 TXQHistogram hist[5]{};
 TXQEvent firstDrop{},ring[64]{};
};
class TXQObservation {
 friend struct TXQObservationTestAccess;
 unsigned gate=0,configured=0,on=0;uint64_t sequence=0,published=0,overwritten=0,suppressed=0,histSuppressed=0;
 uint64_t clockRegressed=0,epoch=0,retries[18][2]{},threshold[7]{},slow=0;
 uint64_t driverCalls=0,driverPackets=0,unknownCalls=0,unknownPackets=0;
 TXQHistogram hist[5]{};TXQEvent ring[64]{},firstDrop{};bool firstValid=false,lastValid=false;
 uint64_t lastDrops=0,dropRegressed=0,lastReclaimTicks=0,lastReclaimEpoch=0;
 bool enter(){unsigned zero=0;return __atomic_compare_exchange_n(&gate,&zero,1,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED);}
 void leave(){__atomic_store_n(&gate,0,__ATOMIC_RELEASE);}
 static void add(uint64_t &v,uint64_t n=1){__atomic_fetch_add(&v,n,__ATOMIC_RELAXED);}
 static uint64_t read(const uint64_t &v){return __atomic_load_n(&v,__ATOMIC_RELAXED);}
 void put(TXQEvent e,uint64_t seq){e.sequence=seq;auto &slot=ring[published%64];if(published>=64)++overwritten;
  slot.validMask=0;slot=e;++published;}
public:
 enum Event:unsigned {Retry=1,ParentEnd=2,Reclaim=3,Service=4,Slow=5,Flush=6,QueueObservation=7};
 enum Timing:unsigned {Callback=0,Prepare=1,Consumer=2,LockWait=3,Batch=4};
 enum Reason:unsigned {Unknown=0,ParentPending=1,Pool=2,Capacity=3,OWN=4,InGate=5};
 enum Valid:uint64_t {Interval=1,Epoch=2,Resources=4,Parents=8,Queue=16,ServiceResult=32,Consumption=64,FlushCount=128,ReclaimToService=256};
 uint64_t (*clock)()=nullptr;
 void configure(bool exact,bool booleanOrAbsent){if(configured)return;configured=1;__atomic_store_n(&on,unsigned(exact&&booleanOrAbsent),__ATOMIC_RELEASE);}
 bool enabled()const{return __atomic_load_n(&on,__ATOMIC_ACQUIRE)!=0;}
 void setThresholds(const uint64_t *t){for(unsigned i=0;i<7;++i)threshold[i]=t[i];slow=t[3];}
 uint64_t now()const{return enabled()&&clock?clock():0;}
 void setEpoch(uint64_t value){__atomic_store_n(&epoch,value,__ATOMIC_RELAXED);}
 uint64_t currentEpoch()const{return read(epoch);}
 static unsigned fullReason(unsigned value){return value==1?Pool:value==2?Capacity:value==3?OWN:Unknown;}
 void record(TXQEvent e){if(!enabled())return;__atomic_add_fetch(&sequence,uint64_t(1),__ATOMIC_RELAXED);
  if(!enter()){add(suppressed);return;}
  if(e.event==Reclaim){lastReclaimTicks=e.end;lastReclaimEpoch=e.epoch;}
  if(e.event==Service&&lastReclaimTicks&&lastReclaimEpoch==e.epoch&&e.begin>=lastReclaimTicks){e.observationBegin=lastReclaimTicks;e.observationEnd=e.begin;e.validMask|=ReclaimToService;}
  put(e,published+1);leave();}
 void retry(TXQEvent e){if(!enabled())return;if(e.site>=1&&e.site<=17)add(retries[e.site][e.path==6]);e.event=Retry;e.consumed=0;e.validMask|=Consumption;record(e);}
 void duration(unsigned kind,uint64_t begin,uint64_t end){if(!enabled()||kind>=5)return;
  if(end<begin){add(clockRegressed);return;}const auto elapsed=end-begin;
  if(!enter()){add(histSuppressed);return;}auto &h=hist[kind];++h.count;h.sum+=elapsed;if(elapsed>h.maximum)h.maximum=elapsed;
  unsigned bin=0;while(bin<7&&elapsed>threshold[bin])++bin;++h.buckets[bin];leave();
  if(elapsed>=slow){TXQEvent e;e.begin=begin;e.end=end;e.event=Slow;e.path=kind;e.epoch=currentEpoch();e.validMask=Interval|Epoch;record(e);}}
 void flush(unsigned caller,uint64_t begin,uint64_t end,unsigned packets){if(!enabled())return;
  if(caller){add(driverCalls);add(driverPackets,packets);}else{add(unknownCalls);add(unknownPackets,packets);}
  TXQEvent e;e.event=Flush;e.reason=caller;e.begin=begin;e.end=end;e.result=packets;e.epoch=currentEpoch();e.validMask=Interval|Epoch|FlushCount;record(e);}
 void observeQueue(uint64_t begin,uint64_t end,unsigned drops,unsigned state,unsigned primary){if(!enabled())return;
  __atomic_add_fetch(&sequence,uint64_t(1),__ATOMIC_RELAXED);if(!enter()){add(suppressed);return;}
  TXQEvent e;e.event=QueueObservation;e.begin=begin;e.end=end;e.observationBegin=begin;e.observationEnd=end;e.drops=drops;e.state=state;e.primarySize=primary;e.epoch=currentEpoch();e.validMask=Interval|Epoch|Queue;
  // Counts are observed at timer cadence; this is not the time of packet loss.
  if((!lastValid&&drops)||(lastValid&&drops>lastDrops)){if(!firstValid){firstDrop=e;firstDrop.sequence=published+1;firstValid=true;}}
  if(lastValid&&drops<lastDrops)++dropRegressed;lastDrops=drops;lastValid=true;put(e,published+1);leave();}
 bool snapshot(TXQSnapshot &out){if(!enter())return false;
  out.schema=1;out.enabled=enabled();out.total=read(sequence);out.published=published;out.overwritten=overwritten;out.suppressed=read(suppressed);out.histSuppressed=read(histSuppressed);out.clockRegressed=read(clockRegressed);out.epoch=currentEpoch();
  out.firstDropValid=firstValid;out.firstDrop=firstDrop;out.lastDropsValid=lastValid;out.lastDrops=lastDrops;out.dropCounterRegressed=dropRegressed;
  out.flushDriverCalls=read(driverCalls);out.flushDriverPackets=read(driverPackets);out.flushUnknownCalls=read(unknownCalls);out.flushUnknownPackets=read(unknownPackets);
  for(unsigned i=0;i<18;++i)for(unsigned j=0;j<2;++j)out.retry[i][j]=read(retries[i][j]);
  for(unsigned i=0;i<7;++i)out.timingThresholdTicks[i]=threshold[i];out.slowTicks=slow;
  for(unsigned i=0;i<5;++i)out.hist[i]=hist[i];for(unsigned i=0;i<64;++i)out.ring[i]=ring[i];leave();return true;}
};
class TXQScope {
 TXQObservation *o;unsigned kind;uint64_t begin=0;
public:TXQScope(TXQObservation *observer,unsigned k):o(observer&&observer->enabled()?observer:nullptr),kind(k){if(o)begin=o->now();}
 ~TXQScope(){finish();}void finish(){if(o){auto end=o->now();o->duration(kind,begin,end);o=nullptr;}}
};
template<class F>auto txqPrepare(TXQObservation *o,F fn)->decltype(fn()){TXQScope elapsed(o,TXQObservation::Prepare);return fn();}
static_assert(sizeof(TXQEvent)==23*8,"exported event ABI");
static_assert(sizeof(TXQHistogram)==11*8,"exported histogram ABI");
static_assert(sizeof(TXQSnapshot)==(17+36+7+1+55+23*65)*8,"exported snapshot ABI");
}
