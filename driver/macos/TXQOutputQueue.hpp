// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <IOKit/network/IOBasicOutputQueue.h>
#include <IOKit/network/IONetworkController.h>
#include <libkern/c++/OSData.h>
#include <kern/clock.h>
#include "../src/TXQueueObservation.hpp"
#include "../src/TXQ2Observation.hpp"
#include "../src/TXQ3DrainObservation.hpp"
#include <sys/kpi_mbuf.h>
// The queue owns the observer. It never stores/retains a controller pointer.
// Apple owns the callout lifetime, including cancellation in super::free().
class RVTTXQOutputQueue:public IOBasicOutputQueue {
 OSDeclareDefaultStructors(RVTTXQOutputQueue)
protected:
 void output(IOMbufQueue *queue,UInt32 *state) override{
  realtek_next::TXQScope elapsed(&observation,realtek_next::TXQObservation::Consumer);
  IOBasicOutputQueue::output(queue,state);
 }
 void free() override{IOBasicOutputQueue::free();}
public:
 realtek_next::TXQObservation observation;
 realtek_next::TXQ2Observation dropObservation;
 realtek_next::TXQ3DrainObservation drainObservation;
 static uint32_t rawDrop(void *p){return static_cast<RVTTXQOutputQueue*>(p)->getDropCount();}
 realtek_next::TXQ2Observation::Token dropBegin(){
  if(!dropObservation.enabled())return {};
  return dropObservation.begin(clockTicks(),getDropCount(),getState(),getSize(),getCapacity());
 }
 UInt32 enqueue(mbuf_t packet,void *param) override{
  if(!dropObservation.enabled())return IOBasicOutputQueue::enqueue(packet,param);
  const auto token=dropBegin();uint64_t count=0,shape=0;mbuf_t part=packet;
  // Inspect only while caller still owns the input. No pointer/payload export.
  for(;part&&count<64;part=mbuf_nextpkt(part),++count){
   uint8_t header[18]{};const size_t length=(mbuf_flags(part)&MBUF_PKTHDR)?mbuf_pkthdr_len(part):0;
   const size_t capture=length<18?length:18;
   if(capture>=14&&!mbuf_copydata(part,0,capture,header)){
    unsigned type=(unsigned(header[12])<<8)|header[13];if(type==0x8100&&capture==18)type=(unsigned(header[16])<<8)|header[17];
    shape|=type==0x800?1:type==0x86dd?2:4;
   }else shape|=4;
   mbuf_tso_request_flags_t flags=0;uint32_t mss=0;
   if((mbuf_flags(part)&MBUF_PKTHDR)&&!mbuf_get_tso_requested(part,&flags,&mss)){if(flags==MBUF_TSO_IPV4)shape|=8;else if(flags==MBUF_TSO_IPV6)shape|=16;}
  }
  if(part)shape|=64;
  const UInt32 result=IOBasicOutputQueue::enqueue(packet,param);
  // Apple may have freed the input. No access to packet/part after delegation.
  const auto end=clockTicks();const auto drops=getDropCount(),state=getState(),primary=getSize();
  dropObservation.enqueueEnd(token,end,drops,state,primary,count,shape,result);
  if(token.live&&drainObservation.enabled())drainObservation.enqueueMetrics(count,bool(shape&64),token.primary,primary,token.capacity,drops!=token.drops);return result;
 }
 bool service(IOOptionBits options=0) override{
  const auto token=dropBegin();
  const auto drain=drainObservation.serviceWanted()?drainObservation.serviceBegin(clockTicks()):realtek_next::TXQ3DrainObservation::Token{};
  const bool result=IOBasicOutputQueue::service(options);dropObservation.serviceEnd(token,result);
  if(drain.live)drainObservation.serviceEnd(drain,clockTicks(),result);return result;
 }
 UInt32 flushObserved(unsigned reason){
  const auto token=dropBegin();const auto begin=observation.now();const UInt32 result=IOBasicOutputQueue::flush();
  observation.flush(reason,begin,observation.now(),result);
  if(token.live)dropObservation.flushEnd(token,clockTicks(),getDropCount(),getState(),getSize(),result,reason);return result;
 }
 void publishDropObservation(IOService *owner){
  if(!dropObservation.target())return;
  owner->setProperty("MAC34TXQ2Schema",1,32);owner->setProperty("MAC34TXQ2Policy",dropObservation.enabled());
  owner->setProperty("MAC34TXQ2SnapshotStored",false);
  if(!dropObservation.enabled())return;
  auto snapshot=new realtek_next::TXQ2Snapshot;if(!snapshot)return;
  if(dropObservation.snapshot(*snapshot,clockTicks(),rawDrop,this)){
   OSData *data=OSData::withBytes(snapshot,sizeof(*snapshot));
   if(data){const bool stored=owner->setProperty("MAC34TXQ2Snapshot",data);owner->setProperty("MAC34TXQ2SnapshotStored",stored);data->release();}
   owner->setProperty("MAC34TXQ2SnapshotBytes",sizeof(*snapshot),32);
   owner->setProperty("MAC34TXQ2EventWords",32,32);
   owner->setProperty("MAC34TXQ2ResidualValid",bool(snapshot->residualValid));
   uint64_t ns=0;absolutetime_to_nanoseconds(1000000000ULL,&ns);owner->setProperty("MAC34TXQ2NanosecondsPerBillionTicks",ns,64);
  }
  delete snapshot;
 }

 static uint64_t clockTicks(){uint64_t t=0;clock_get_uptime(&t);return t;}
 static RVTTXQOutputQueue *create(IONetworkController *target){
  auto q=new RVTTXQOutputQueue;
  if(q&&!q->init(target,target->getOutputHandler(),256,1)){q->release();q=nullptr;}
  if(q){q->observation.clock=clockTicks;uint64_t ticks[7]{};const uint64_t ns[7]={1000,10000,100000,1000000,10000000,100000000,1000000000};
   for(unsigned i=0;i<7;++i)nanoseconds_to_absolutetime(ns[i],&ticks[i]);q->observation.setThresholds(ticks);q->dropObservation.setThresholds(ticks);q->drainObservation.setThresholds(ticks);q->drainObservation.clock=clockTicks;}
  return q;
 }
 UInt32 flush() override{return flushObserved(0);}
 UInt32 flushFromDriver(unsigned reason){return flushObserved(reason);}
 // Publication only at the existing timer cadence, outside the controller lock.
 // No queue lock is held when snapshotting the diagnostic ring or publishing.
 void publishDrainObservation(IOService *owner){
  if(!drainObservation.target())return;
  owner->setProperty("MAC34TXQ3EnqueueMetricsAvailable",dropObservation.enabled());
  owner->setProperty("MAC34TXQ3Schema",1,32);owner->setProperty("MAC34TXQ3Policy",drainObservation.enabled());owner->setProperty("MAC34TXQ3SnapshotStored",false);
  if(!drainObservation.enabled())return;
  auto snapshot=new realtek_next::TXQ3DrainSnapshot;if(!snapshot)return;
  if(drainObservation.snapshot(*snapshot,clockTicks())){
   OSData *data=OSData::withBytes(snapshot,sizeof(*snapshot));if(data){owner->setProperty("MAC34TXQ3SnapshotStored",owner->setProperty("MAC34TXQ3Snapshot",data));data->release();}
   owner->setProperty("MAC34TXQ3SnapshotBytes",sizeof(*snapshot),32);
   owner->setProperty("MAC34TXQ3EpisodeWords",sizeof(realtek_next::TXQ3DrainEpisode)/8,32);
   uint64_t ns=0;absolutetime_to_nanoseconds(1000000000ULL,&ns);owner->setProperty("MAC34TXQ3NanosecondsPerBillionTicks",ns,64);
  }
  delete snapshot;
 }
 void publishObservation(IOService *owner){
  publishDrainObservation(owner);
  publishDropObservation(owner);
  owner->setProperty("TXQOBS1Schema",1,32);owner->setProperty("TXQOBS1Policy",observation.enabled());
  owner->setProperty("TXQOBS1Coverage","delegated output/flush; enqueue reject and complete dwell unavailable; getSize primary-only");
  if(!observation.enabled())return;
  const auto begin=observation.now();const auto drops=getDropCount(),state=getState(),primary=getSize();const auto end=observation.now();
  observation.observeQueue(begin,end,drops,state,primary);
  auto snapshot=new realtek_next::TXQSnapshot;
  if(!snapshot)return;
  if(observation.snapshot(*snapshot)){
   OSData *data=OSData::withBytes(snapshot,sizeof(*snapshot));if(data){owner->setProperty("TXQOBS1Snapshot",data);data->release();}
   owner->setProperty("TXQOBS1SnapshotBytes",sizeof(*snapshot),32);
   owner->setProperty("TXQOBS1EventWords",23,32);
   uint64_t ns=0;absolutetime_to_nanoseconds(1000000000ULL,&ns);owner->setProperty("TXQOBS1NanosecondsPerBillionTicks",ns,64);
  }
  delete snapshot;
 }
};
OSDefineMetaClassAndStructors(RVTTXQOutputQueue,IOBasicOutputQueue)
