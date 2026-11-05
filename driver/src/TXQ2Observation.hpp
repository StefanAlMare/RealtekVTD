// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <stdint.h>
#include <stddef.h>
namespace realtek_next {
struct TXQ2Context {
 uint64_t epoch=0,at=0,kind=0,producer=0,consumer=0,used=0,capacity=0,completed=0;
 uint64_t parent4=0,remaining4=0,outstanding4=0,parent6=0,remaining6=0,outstanding6=0;
 uint64_t site=0,family=0,reason=0,retryAt=0,serviceAllowed=0,networkStatus=0;
};
struct TXQ2Event {
 uint64_t sequence=0,begin=0,end=0,kind=0,a=0,b=0,c=0,d=0,e=0,f=0,g=0,h=0;
 TXQ2Context context;
};
struct TXQ2Snapshot {
 uint64_t schema=1,eventWords=32,at=0,version=0,active=0,enqueues=0,presented=0,truncated=0,enqueueDropIntervals=0;
 uint64_t flushCalls=0,flushPackets=0,serviceCalls=0,serviceTrue=0,callbacks=0,accepted=0,retry=0,dropped=0;
 uint64_t suppressed=0,contextSuppressed=0,totalEvents=0,overwritten=0,baselineDrop=0,rawDrop=0;
 uint64_t quiescent=0,gap=0,regressed=0,residualValid=0,nonObservedFlushResidual=0,epoch=0;
 uint64_t retrySites[18][3]{},retryReasons[16]{},enqueueElapsed[8]{};
 TXQ2Context context;TXQ2Event firstDrop,ring[64];
};
static_assert(sizeof(TXQ2Context)==20*8,"TXQ2 context ABI");
static_assert(sizeof(TXQ2Event)==32*8,"TXQ2 event ABI");
static_assert(sizeof(TXQ2Snapshot)==2207*8,"TXQ2 snapshot ABI");
// Diagnostic only: no mbuf/controller pointer, allocation, waits or functional lock.
// Writers try once. Atomic totals survive event/context suppression. Queue APIs
// remain delegated to Apple. A residual is valid only at a stable quiescent cut.
class TXQ2Observation {
 uint64_t on=0,targetMode=0,initialized=0,sequence=0,active=0,version=0,gate=0,coverageGap=0,regression=0;
 TXQ2Snapshot data;
 uint64_t thresholds[7]{};
 static uint64_t read(const uint64_t &v){return __atomic_load_n(&v,__ATOMIC_SEQ_CST);}
 static void add(uint64_t &v,uint64_t n=1){__atomic_fetch_add(&v,n,__ATOMIC_SEQ_CST);}
 static void store(uint64_t&v,uint64_t n){__atomic_store_n(&v,n,__ATOMIC_SEQ_CST);}
 bool lock(){uint64_t zero=0;return __atomic_compare_exchange_n(&gate,&zero,1,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED);}
 void unlock(){__atomic_store_n(&gate,0,__ATOMIC_RELEASE);}
 void enter(){add(active);add(version);}
 void leave(){add(version);__atomic_fetch_sub(&active,1ULL,__ATOMIC_SEQ_CST);}
 void event(TXQ2Event e,bool first){
  if(!lock()){add(data.suppressed);return;}
  e.sequence=++sequence;e.context=data.context;
  if(first&&!data.firstDrop.sequence)data.firstDrop=e;
  data.ring[(e.sequence-1)%64]=e;data.totalEvents=e.sequence;data.overwritten=e.sequence>64?e.sequence-64:0;unlock();
 }
public:
 struct Token {bool live=false;uint64_t begin=0;uint32_t drops=0,state=0,primary=0,capacity=0;};
 bool enabled()const{return read(on)!=0;}
 bool target()const{return read(targetMode)!=0;}
 void configure(bool exact,bool wanted,uint32_t rawDrop){
  store(targetMode,exact);
  const bool next=exact&&wanted;
  if(!next&&read(initialized))store(coverageGap,1);
  if(next&&!read(initialized)){data.baselineDrop=rawDrop;store(initialized,1);}
  store(on,next);
 }
 void setThresholds(const uint64_t *v){for(unsigned i=0;i<7;++i)thresholds[i]=v[i];}
 Token begin(uint64_t now,uint32_t drops,uint32_t state,uint32_t primary,uint32_t capacity){
  Token t;if(!enabled())return t;enter();t.live=true;t.begin=now;t.drops=drops;t.state=state;t.primary=primary;t.capacity=capacity;return t;
 }
 void enqueueEnd(const Token&t,uint64_t now,uint32_t drops,uint32_t state,uint32_t primary,uint64_t packets,uint64_t shape,uint32_t result){
  if(!t.live)return;
  add(data.enqueues);add(data.presented,packets);if(shape&64)add(data.truncated);
  unsigned bin=0;while(bin<7&&now-t.begin>=thresholds[bin])++bin;add(data.enqueueElapsed[bin]);
  if(drops<t.drops)store(regression,1);
  if(drops!=t.drops){add(data.enqueueDropIntervals);TXQ2Event e;e.begin=t.begin;e.end=now;e.kind=1;e.a=t.drops;e.b=drops;e.c=packets;e.d=shape;e.e=uint64_t(t.state)|(uint64_t(state)<<32);e.f=uint64_t(t.primary)|(uint64_t(primary)<<32);e.g=t.capacity;e.h=result;event(e,true);}
  leave();
 }
 void flushEnd(const Token&t,uint64_t now,uint32_t drops,uint32_t state,uint32_t primary,uint32_t freed,unsigned reason){
  if(!t.live)return;add(data.flushCalls);add(data.flushPackets,freed);if(drops<t.drops)store(regression,1);
  TXQ2Event e;e.begin=t.begin;e.end=now;e.kind=2;e.a=freed;e.b=reason;e.c=t.drops;e.d=drops;e.e=uint64_t(t.state)|(uint64_t(state)<<32);e.f=uint64_t(t.primary)|(uint64_t(primary)<<32);e.g=t.capacity;event(e,drops!=t.drops);leave();
 }
 void serviceEnd(const Token&t,bool result){if(!t.live)return;add(data.serviceCalls);if(result)add(data.serviceTrue);leave();}
 void callback(uint32_t result){if(!enabled())return;enter();add(data.callbacks);switch(result&255){case 2:add(data.retry);break;case 1:add(data.dropped);break;default:add(data.accepted);break;}leave();}
 void context(TXQ2Context c,unsigned site=0,unsigned family=0,unsigned reason=0){
  if(!enabled())return;
  if(site){add(data.retrySites[site<18?site:0][family==4?1:family==6?2:0]);add(data.retryReasons[reason<16?reason:0]);}
  if(!lock()){add(data.contextSuppressed);return;}
  if(site){c.site=site;c.family=family;c.reason=reason;c.retryAt=c.at;}
  else if(c.epoch==data.context.epoch){c.site=data.context.site;c.family=data.context.family;c.reason=data.context.reason;c.retryAt=data.context.retryAt;}
  else{c.site=0;c.family=0;c.reason=0;c.retryAt=0;} // New epoch: old retry context is unavailable.
  data.context=c;unlock();
 }
 bool snapshot(TXQ2Snapshot &out,uint64_t now,uint32_t (*rawReader)(void*),void *reader){
  if(!enabled()||!lock())return false;
  const auto v=read(version),a=read(active);out.schema=1;out.eventWords=32;out.at=now;out.version=v;out.active=a;
  out.nonObservedFlushResidual=0;
  // Copy counters atomically; do not copy the writer's atomic storage wholesale.
#define TXQ2_READ(x) out.x=read(data.x)
  TXQ2_READ(enqueues);TXQ2_READ(presented);TXQ2_READ(truncated);TXQ2_READ(enqueueDropIntervals);TXQ2_READ(flushCalls);TXQ2_READ(flushPackets);TXQ2_READ(serviceCalls);TXQ2_READ(serviceTrue);TXQ2_READ(callbacks);TXQ2_READ(accepted);TXQ2_READ(retry);TXQ2_READ(dropped);TXQ2_READ(suppressed);TXQ2_READ(contextSuppressed);
#undef TXQ2_READ
  for(unsigned i=0;i<18;++i)for(unsigned j=0;j<3;++j)out.retrySites[i][j]=read(data.retrySites[i][j]);
  for(unsigned i=0;i<16;++i)out.retryReasons[i]=read(data.retryReasons[i]);for(unsigned i=0;i<8;++i)out.enqueueElapsed[i]=read(data.enqueueElapsed[i]);
  out.context=data.context;out.epoch=data.context.epoch;out.firstDrop=data.firstDrop;out.totalEvents=data.totalEvents;out.overwritten=data.overwritten;for(unsigned i=0;i<64;++i)out.ring[i]=data.ring[i];
  out.baselineDrop=data.baselineDrop;out.rawDrop=rawReader(reader);out.gap=read(coverageGap);out.regressed=read(regression);
  out.quiescent=a==0&&read(active)==0&&v==read(version);
  // Never subtract concurrent counters and label them exact rejects. This is a
  // NON-OBSERVED-FLUSH residual under the delegated-source coverage assumption.
  out.residualValid=out.quiescent&&!out.gap&&!out.regressed&&out.rawDrop>=out.baselineDrop&&out.rawDrop-out.baselineDrop>=out.flushPackets;
  if(out.residualValid)out.nonObservedFlushResidual=out.rawDrop-out.baselineDrop-out.flushPackets;
  unlock();return true;
 }
};
}
