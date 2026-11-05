// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <stdint.h>
#include <stddef.h>
namespace realtek_next {
struct TXQ3DrainHistogram {uint64_t count=0,sum=0,maximum=0,buckets[8]{};};
struct TXQ3DrainEpisode {
 uint64_t id=0,epoch=0,family=0,validMask=0,flags=0,histMask=0,segments=0;
 uint64_t initialRemaining=0,initialOutstanding=0,initialUsed=0,remaining=0,outstanding=0,used=0;
 uint64_t retryCount=0,retrySite=0,retryFirst=0,retryLast=0;
 uint64_t decisionAllowed=0,decisionUsed=0,serviceResult=0,cancelReason=0,lossVersion=0;
 uint64_t tailRemaining=0,tailOutstanding=0,tailUsed=0,lastChildRemaining=0,lastChildOutstanding=0,lastChildUsed=0,clearUsed=0,reclaimUsed=0;
 uint64_t times[12]{};
};
struct TXQ3DrainSnapshot {
 uint64_t schema=1,episodeWords=42,at=0,enabled=0,sampleEvery=256;
 uint64_t parents=0,selected=0,stored=0,suppressed=0,stageSuppressed=0,controlSuppressed=0,enqueueSuppressed=0,snapshotSuppressed=0;
 uint64_t overwritten=0,invalidated=0,stale=0,epochMismatch=0,clockRegressed=0,serviceBegins=0,serviceEnds=0,callbacks=0;
 uint64_t enqueueCalls=0,enqueuePresented=0,enqueueTruncated=0,enqueueDropIntervals=0,enqueueMaxObserved=0,primaryMax=0,primaryAboveCapacity=0,lossVersion=0;
 uint64_t chainBins[6]{},thresholdTicks[7]{};
 TXQ3DrainHistogram latency[7]{};TXQ3DrainEpisode episodes[64]{};
};
static_assert(sizeof(TXQ3DrainEpisode)==42*8,"TXQ3 episode ABI");
static_assert(sizeof(TXQ3DrainHistogram)==11*8,"TXQ3 histogram ABI");
static_assert(sizeof(TXQ3DrainSnapshot)==2807*8,"TXQ3 snapshot ABI");
// Diagnostic only. No pointers retained/exported, allocations, waits, queue APIs,
// functional locks or functional state changes. All writers try the gate once.
class TXQ3DrainObservation {
 friend struct TXQ3DrainTestAccess;
 uint64_t on=0,targetMode=0,gate=0,finished=0,pendingService=0,pendingCallback=0;
 TXQ3DrainSnapshot data;
 static uint64_t read(const uint64_t&v){return __atomic_load_n(&v,__ATOMIC_SEQ_CST);}
 static void store(uint64_t&v,uint64_t n){__atomic_store_n(&v,n,__ATOMIC_SEQ_CST);}
 static uint64_t add(uint64_t&v,uint64_t n=1){return __atomic_fetch_add(&v,n,__ATOMIC_SEQ_CST)+n;}
 bool lock(){uint64_t zero=0;return __atomic_compare_exchange_n(&gate,&zero,1,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED);}
 void unlock(){__atomic_store_n(&gate,0,__ATOMIC_RELEASE);}
 void miss(unsigned kind){add(data.suppressed);if(kind==1)add(data.stageSuppressed);else if(kind==2)add(data.controlSuppressed);else if(kind==3)add(data.enqueueSuppressed);else add(data.snapshotSuppressed);if(kind==1||kind==2)add(data.lossVersion);}
 TXQ3DrainEpisode *find(uint64_t id){if(!id)return nullptr;auto &e=data.episodes[((id-1)/256)%64];if(e.id!=id){add(data.stale);return nullptr;}return &e;}
 void clearPending(uint64_t id){if(read(finished)==id)store(finished,0);if(read(pendingService)==id)store(pendingService,0);if(read(pendingCallback)==id)store(pendingCallback,0);}
 bool complete(const TXQ3DrainEpisode&e)const{return (e.validMask&(1ULL<<CallbackAt))&&((e.validMask&(1ULL<<ServiceEndAt))||((e.flags&DecisionDeferred)!=0));}
 void markLoss(TXQ3DrainEpisode&e){if(e.id&&e.lossVersion!=read(data.lossVersion))e.flags|=PotentialSuppression;}
 void cancelLocked(TXQ3DrainEpisode&e,uint64_t now,unsigned reason){
  if(!(e.flags&Invalidated)){e.flags|=Invalidated;++data.invalidated;e.cancelReason=reason;e.times[CancelledAt]=now;e.validMask|=1ULL<<CancelledAt;}clearPending(e.id);
 }
 void stamp(TXQ3DrainEpisode&e,unsigned index,uint64_t now){if(!(e.validMask&(1ULL<<index))){e.times[index]=now;e.validMask|=1ULL<<index;}markLoss(e);histograms(e);}
 void histograms(TXQ3DrainEpisode&e){
  if(!(e.validMask&(1ULL<<AcceptedAt))||(e.flags&Invalidated))return;
  const unsigned starts[7]={BeginAt,TailAt,LastChildAt,ClearAt,CleanupAt,DecisionAt,ServiceBeginAt};
  const unsigned ends[7]={TailAt,LastChildAt,ClearAt,CleanupAt,DecisionAt,ServiceBeginAt,CallbackAt};
  for(unsigned j=0;j<7;++j){const auto bits=(1ULL<<starts[j])|(1ULL<<ends[j]);if((e.histMask&(1ULL<<j))||(e.validMask&bits)!=bits)continue;
   e.histMask|=1ULL<<j;
   if(j==ServiceCallback&&(e.flags&CallbackBeforeService))continue;
   if(e.times[ends[j]]<e.times[starts[j]]){e.flags|=ClockRegression;++data.clockRegressed;continue;}
   const auto elapsed=e.times[ends[j]]-e.times[starts[j]];auto &h=data.latency[j];++h.count;h.sum+=elapsed;if(elapsed>h.maximum)h.maximum=elapsed;
   unsigned bin=0;while(bin<7&&elapsed>=data.thresholdTicks[bin])++bin;++h.buckets[bin];
  }
 }
public:
 enum Stage:unsigned {Accepted=0,AllSubmitted=1,LastChildEntry=2,StateCleared=3,CleanupDone=4,ReclaimDone=5};
 enum Time:unsigned {BeginAt=0,AcceptedAt=1,TailAt=2,LastChildAt=3,ClearAt=4,CleanupAt=5,ReclaimAt=6,DecisionAt=7,ServiceBeginAt=8,ServiceEndAt=9,CallbackAt=10,CancelledAt=11};
 enum Latency:unsigned {StartTail=0,TailLast=1,LastClear=2,ClearCleanup=3,CleanupDecision=4,DecisionService=5,ServiceCallback=6};
 enum Flag:uint64_t {Invalidated=1,EpochMismatch=2,PotentialSuppression=4,CallbackBeforeService=8,PendingReplaced=16,ClockRegression=32,DecisionDeferred=64};
 struct Token {uint64_t id=0,epoch=0;bool live=false;};
 uint64_t (*clock)()=nullptr;
 bool enabled()const{return read(on)!=0;}
 bool target()const{return read(targetMode)!=0;}
 uint64_t now()const{return enabled()&&clock?clock():0;}
 bool serviceWanted()const{return enabled()&&read(pendingService)!=0;}
 bool callbackWanted()const{return enabled()&&read(pendingCallback)!=0;}
 void setThresholds(const uint64_t *ticks){for(unsigned i=0;i<7;++i)data.thresholdTicks[i]=ticks[i];}
 void configure(bool exact,bool wanted){
  store(targetMode,exact);const bool next=exact&&wanted;if(next){store(on,1);return;}
  store(on,0);if(!lock()){miss(2);store(finished,0);store(pendingService,0);store(pendingCallback,0);return;}
  for(auto &e:data.episodes)if(e.id&&!complete(e))cancelLocked(e,0,1);
  store(finished,0);store(pendingService,0);store(pendingCallback,0);unlock();
 }
 uint64_t begin(uint64_t epoch,unsigned family,uint64_t now,uint64_t segments,uint64_t remaining,uint64_t outstanding,uint64_t used){
  if(!enabled()||(family!=4&&family!=6))return 0;const auto id=add(data.parents);if((id-1)%256)return 0;add(data.selected);
  if(!lock()){miss(1);return 0;}auto &e=data.episodes[((id-1)/256)%64];if(e.id){clearPending(e.id);++data.overwritten;}
  e={};e.id=id;e.epoch=epoch;e.family=family;e.segments=segments;e.initialRemaining=e.remaining=remaining;e.initialOutstanding=e.outstanding=outstanding;e.initialUsed=e.used=used;
  e.times[BeginAt]=now;e.validMask=1ULL<<BeginAt;e.lossVersion=read(data.lossVersion);++data.stored;unlock();return id;
 }
 void stage(uint64_t id,Stage stage,uint64_t now,uint64_t remaining,uint64_t outstanding,uint64_t used){
  if(!id||!enabled()||unsigned(stage)>unsigned(ReclaimDone))return;if(!lock()){miss(1);return;}auto e=find(id);if(!e||e->flags&Invalidated){unlock();return;}
  const unsigned index=stage==Accepted?AcceptedAt:stage==AllSubmitted?TailAt:stage==LastChildEntry?LastChildAt:stage==StateCleared?ClearAt:stage==CleanupDone?CleanupAt:ReclaimAt;
  const bool fresh=!(e->validMask&(1ULL<<index));
  if(fresh&&stage==AllSubmitted){e->tailRemaining=remaining;e->tailOutstanding=outstanding;e->tailUsed=used;}
  if(fresh&&stage==LastChildEntry){e->lastChildRemaining=remaining;e->lastChildOutstanding=outstanding;e->lastChildUsed=used;}
  if(fresh&&stage==StateCleared)e->clearUsed=used;
  if(fresh&&stage==ReclaimDone)e->reclaimUsed=used;
  e->remaining=remaining;e->outstanding=outstanding;e->used=used;stamp(*e,index,now);
  if(stage==CleanupDone&&fresh){
   const auto old=read(finished);if(old&&old!=id){auto previous=find(old);if(previous)previous->flags|=PendingReplaced;}
   const auto callback=read(pendingCallback);if(callback&&callback!=id){auto previous=find(callback);if(previous)previous->flags|=PendingReplaced;}
   store(finished,id);store(pendingCallback,id);
  }
  unlock();
 }
 void retry(uint64_t id,unsigned site,uint64_t now){
  if(!id||!enabled())return;if(!lock()){miss(1);return;}auto e=find(id);if(e&&!(e->flags&Invalidated)){if(!e->retryCount)e->retryFirst=now;++e->retryCount;e->retryLast=now;e->retrySite=site;markLoss(*e);}unlock();
 }
 void reclaimDone(uint64_t epoch,uint64_t now,uint64_t used){
  if(!enabled()||!read(finished))return;if(!lock()){miss(2);return;}auto e=find(read(finished));
  if(e&&!(e->flags&Invalidated)){if(e->epoch!=epoch){e->flags|=EpochMismatch;++data.epochMismatch;cancelLocked(*e,now,2);}else{if(!(e->validMask&(1ULL<<ReclaimAt)))e->reclaimUsed=used;e->used=used;stamp(*e,ReclaimAt,now);}}unlock();
 }
 void decision(uint64_t epoch,bool allowed,uint64_t now,uint64_t used){
  if(!enabled()||!read(finished))return;if(!lock()){miss(2);store(finished,0);return;}auto e=find(read(finished));
  if(e&&!(e->flags&Invalidated)){
   if(e->epoch!=epoch){e->flags|=EpochMismatch;++data.epochMismatch;cancelLocked(*e,now,2);}
   else{e->decisionAllowed=allowed;e->decisionUsed=used;stamp(*e,DecisionAt,now);if(allowed){const auto old=read(pendingService);if(old&&old!=e->id){auto previous=find(old);if(previous)previous->flags|=PendingReplaced;}store(pendingService,e->id);}else e->flags|=DecisionDeferred;store(finished,0);}
  }unlock();
 }
 Token serviceBegin(uint64_t now){
  Token token;if(!serviceWanted())return token;if(!lock()){miss(2);store(pendingService,0);return token;}const auto id=read(pendingService);auto e=find(id);
  if(e&&!(e->flags&Invalidated)){token.id=id;token.epoch=e->epoch;token.live=true;stamp(*e,ServiceBeginAt,now);++data.serviceBegins;store(pendingService,0);}unlock();return token;
 }
 void serviceEnd(const Token&token,uint64_t now,bool result){
  if(!token.live||!enabled())return;if(!lock()){miss(2);return;}auto e=find(token.id);
  if(e&&e->epoch==token.epoch&&!(e->flags&Invalidated)&&!(e->validMask&(1ULL<<ServiceEndAt))){e->serviceResult=result;stamp(*e,ServiceEndAt,now);++data.serviceEnds;}unlock();
 }
 void callbackBegin(uint64_t now){
  if(!callbackWanted())return;if(!lock()){miss(2);store(pendingCallback,0);return;}const auto id=read(pendingCallback);auto e=find(id);
  if(e&&!(e->flags&Invalidated)){if(!(e->validMask&(1ULL<<ServiceBeginAt)))e->flags|=CallbackBeforeService;stamp(*e,CallbackAt,now);++data.callbacks;store(pendingCallback,0);}unlock();
 }
 void cancel(uint64_t id,uint64_t now,unsigned reason){if(!id||!enabled())return;if(!lock()){miss(1);return;}auto e=find(id);if(e)cancelLocked(*e,now,reason);unlock();}
 void invalidate(uint64_t epoch,uint64_t now,unsigned reason){
  if(!enabled())return;if(!lock()){miss(2);store(finished,0);store(pendingService,0);store(pendingCallback,0);return;}
  for(auto &e:data.episodes)if(e.id&&e.epoch==epoch&&!complete(e))cancelLocked(e,now,reason);
  store(finished,0);store(pendingService,0);store(pendingCallback,0);unlock();
 }
 void enqueueMetrics(uint64_t count,bool truncated,uint64_t primaryBefore,uint64_t primaryAfter,uint64_t capacity,bool dropChanged){
  if(!enabled())return;add(data.enqueueCalls);add(data.enqueuePresented,count);if(truncated)add(data.enqueueTruncated);if(dropChanged)add(data.enqueueDropIntervals);
  if(!lock()){miss(3);return;}unsigned bin=truncated||count>=64?5:count==0?0:count==1?1:count<=4?2:count<=16?3:4;++data.chainBins[bin];
  if(count>data.enqueueMaxObserved)data.enqueueMaxObserved=count;const auto high=primaryBefore>primaryAfter?primaryBefore:primaryAfter;if(high>data.primaryMax)data.primaryMax=high;if(high>capacity)++data.primaryAboveCapacity;unlock();
 }
 bool snapshot(TXQ3DrainSnapshot&out,uint64_t now){
  if(!enabled())return false;if(!lock()){miss(4);return false;}out.schema=1;out.episodeWords=42;out.at=now;out.enabled=enabled();out.sampleEvery=256;
#define TXQ3_READ(x) out.x=read(data.x)
  TXQ3_READ(parents);TXQ3_READ(selected);TXQ3_READ(stored);TXQ3_READ(suppressed);TXQ3_READ(stageSuppressed);TXQ3_READ(controlSuppressed);TXQ3_READ(enqueueSuppressed);TXQ3_READ(snapshotSuppressed);TXQ3_READ(overwritten);TXQ3_READ(invalidated);TXQ3_READ(stale);TXQ3_READ(epochMismatch);TXQ3_READ(clockRegressed);TXQ3_READ(serviceBegins);TXQ3_READ(serviceEnds);TXQ3_READ(callbacks);TXQ3_READ(enqueueCalls);TXQ3_READ(enqueuePresented);TXQ3_READ(enqueueTruncated);TXQ3_READ(enqueueDropIntervals);TXQ3_READ(enqueueMaxObserved);TXQ3_READ(primaryMax);TXQ3_READ(primaryAboveCapacity);TXQ3_READ(lossVersion);
#undef TXQ3_READ
  for(unsigned i=0;i<6;++i)out.chainBins[i]=data.chainBins[i];for(unsigned i=0;i<7;++i){out.thresholdTicks[i]=data.thresholdTicks[i];out.latency[i]=data.latency[i];}
  for(unsigned i=0;i<64;++i){out.episodes[i]=data.episodes[i];markLoss(out.episodes[i]);}unlock();return true;
 }
};
}
