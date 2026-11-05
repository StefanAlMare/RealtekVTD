// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "NetworkProgram.hpp"
namespace realtek_next {
// Caller holds the existing controller lock, except two atomic queue-service counters.
// Rolling last-32 event log; lifetime sequence and overwrite count. No clock, payload, exported pointer, allocation or MMIO.
struct TXRingDiagnostic {
 static constexpr unsigned Capacity=32,Words=36;
 enum Field:unsigned {Sequence,Epoch,Reason,Attempts,RingFull,Producer,Consumer,Used,Free,
 Submitted,Completed,IRQActions,TXIRQPasses,ReclaimPasses,ReclaimedDescriptors,
 QueueServiceCalls,QueueServiceTrue,OWNIndex,OWNWord,RequestedDescriptors,
 FirstIRQSeen,FirstIRQCauses,FirstIRQSuccess,AfterUsed,AfterProducer,AfterConsumer,
 AfterSubmitted,AfterCompleted,ReclaimDelta,RetryAttempts,RetryOutcome,RetryStatus,
 RetryGeneration,RetrySubmitted,RetryCompleted,RingCount};
 enum Outcome:unsigned {Pending=0,Consumed=1,StalledAgain=2,Dropped=3,Cancelled=4};
 uint64_t samples[Capacity][Words]{};
 unsigned count=0,pending=0;uint32_t awaitingIRQ=0;uint64_t total=0,overwritten=0;void *pendingPacket=nullptr;
 uint64_t suppressed=0,poolFull=0,capacityFull=0,ownFull=0;
 uint64_t irqPasses=0,txIRQPasses=0,reclaimedDescriptors=0,reclaimedPackets=0,noReclaim=0;
 uint64_t queueCalls=0,queueTrue=0,flushCalls=0,flushPackets=0;
 static uint64_t read(const uint64_t &v){return __atomic_load_n(&v,__ATOMIC_RELAXED);}
 void queueService(bool resumed){
  __atomic_fetch_add(&queueCalls,uint64_t(1),__ATOMIC_RELAXED);
  if(resumed)__atomic_fetch_add(&queueTrue,uint64_t(1),__ATOMIC_RELAXED);
 }
 void flush(unsigned packets){
  __atomic_fetch_add(&flushCalls,uint64_t(1),__ATOMIC_RELAXED);
  __atomic_fetch_add(&flushPackets,uint64_t(packets),__ATOMIC_RELAXED);
 }
 void result(void *packet,unsigned epoch,Outcome outcome,unsigned status,const NetworkResult &n){
  if(!pending||pendingPacket!=packet)return;
  auto &r=samples[pending-1];
  if(r[Epoch]!=epoch){cancel();return;}
  ++r[RetryAttempts];r[RetryOutcome]=outcome;r[RetryStatus]=status;
  r[RetryGeneration]=epoch;r[RetrySubmitted]=n.txSubmitted;r[RetryCompleted]=n.txCompleted;
  pending=0;pendingPacket=nullptr;
 }
 void cancel(){if(pending)samples[pending-1][RetryOutcome]=Cancelled;pending=0;pendingPacket=nullptr;}
 void full(void *packet,unsigned epoch,unsigned reason,uint64_t attempts,uint64_t fullCount,const NetworkResult &n){
  if(reason==1)++poolFull;else if(reason==2)++capacityFull;else if(reason==3)++ownFull;
  const unsigned slot=unsigned(total%Capacity);
  if(count<Capacity)++count;else ++overwritten;
  auto &r=samples[slot];for(auto &v:r)v=0;
  r[Sequence]=++total;r[Epoch]=epoch;r[Reason]=reason;
  r[Attempts]=attempts;r[RingFull]=fullCount;r[Producer]=n.producer;r[Consumer]=n.consumer;
  r[Used]=n.used;r[Free]=n.txCount-n.used;r[RingCount]=n.txCount;r[Submitted]=n.txSubmitted;r[Completed]=n.txCompleted;
  r[IRQActions]=n.irqActions;r[TXIRQPasses]=txIRQPasses;r[ReclaimPasses]=irqPasses;
  r[ReclaimedDescriptors]=reclaimedDescriptors;r[QueueServiceCalls]=read(queueCalls);r[QueueServiceTrue]=read(queueTrue);
  r[OWNIndex]=reason==3?n.txFullIndex:UINT64_MAX;r[OWNWord]=reason==3?n.txFullWord:0;
  r[RequestedDescriptors]=reason==1?0:n.txFullRequested;
  awaitingIRQ|=uint32_t(1)<<slot;pending=slot+1;pendingPacket=packet;
 }
 void irq(unsigned epoch,unsigned causes,bool ok,unsigned usedBefore,uint64_t completedBefore,const NetworkResult &n){
  ++irqPasses;if(causes&12)++txIRQPasses;
  const unsigned freed=usedBefore>=n.used?usedBefore-n.used:0;
  reclaimedDescriptors+=freed;reclaimedPackets+=n.txCompleted-completedBefore;
  if(!freed)++noReclaim;
  while(awaitingIRQ){const unsigned slot=unsigned(__builtin_ctz(awaitingIRQ));awaitingIRQ&=~(uint32_t(1)<<slot);auto &r=samples[slot];
   if(r[Epoch]!=epoch){r[FirstIRQSeen]=2;continue;}
   r[FirstIRQSeen]=1;r[FirstIRQCauses]=causes;r[FirstIRQSuccess]=ok;
   r[AfterUsed]=n.used;r[AfterProducer]=n.producer;r[AfterConsumer]=n.consumer;
   r[AfterSubmitted]=n.txSubmitted;r[AfterCompleted]=n.txCompleted;r[ReclaimDelta]=freed;
  }
 }
};
static_assert(TXRingDiagnostic::RingCount+1==TXRingDiagnostic::Words,"TX schema width");
}
