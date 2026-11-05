// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <stdint.h>
namespace realtek_next {
// Existing controller serialization only. No hardware access, polling, queue
// calls, addresses of kernel objects, or allocation in this observation store.
class MAC46VTDObservation {
public:
 static constexpr unsigned Schema=1,Words=40,Capacity=8,HeaderWords=8;
 static constexpr unsigned DumpWords=HeaderWords+(Capacity+1)*Words;
 enum Event:unsigned {TimerStall=1,Watchdog=2,NetworkError=3,IRQError=4,TSOError=5};
 enum Field:unsigned {Sequence,TimeNS,Epoch,Kind,Valid,Attempted,Returned,Enabled,Status,Running,
  Used,Producer,Consumer,RingCount,PoolCount,PoolLeased,PoolStride,PoolBytes,Submitted,Completed,
  IRQActions,IRQPasses,IRQBudget,StalledSeconds,SyncStatus,HeadOpts1,HeadOpts2,Owned,
  PCICommand,MACCommand,IMR,ISR,TXPoll,PHY,TXRingLow,TXRingHigh,PoolIOVA,HeadIOVA,HeadInPool,Reserved};
 enum Mask:uint64_t {Software=1,Descriptor=2,PCI=4,MAC=8,InterruptMask=16,
  InterruptStatus=32,Poll=64,PHYStatus=128,RingLow=256,RingHigh=512,PoolRange=1024};
 struct Sample {uint64_t w[Words]{};};
private:
 bool exact=false,validPreference=true,wanted=true,on=false,wdStored=false;
 unsigned stored=0,next=0;uint64_t total=0,overwritten=0,exported=UINT64_MAX;
 Sample samples[Capacity],lastWatchdog;uint64_t encoded[DumpWords]{};
public:
 static bool eligible(unsigned mac,unsigned cfg,unsigned xid,bool initialized,bool ready,
                      bool mapper,bool resources,unsigned ring,unsigned pool,unsigned stride){
  return mac==46&&cfg==0&&xid==0x541&&initialized&&ready&&mapper&&resources&&ring==64&&pool==65&&stride>=2048;
 }
 // Existing schema records device DMA addresses. For native64 these are
 // physical/bus addresses, not mapper IOVA; the controller publishes that mode.
 static bool eligibleNative64(unsigned mac,unsigned cfg,unsigned xid,bool initialized,bool ready,
                      bool mapper,bool native64,bool resources,unsigned ring,unsigned pool,unsigned stride){
  return mac==46&&cfg==0&&xid==0x541&&initialized&&ready&&!mapper&&native64&&resources&&ring==64&&pool==65&&stride>=2048;
 }
 void configure(bool identity,bool preferenceValid,bool requested){
  exact=identity;validPreference=preferenceValid;wanted=requested;on=exact&&validPreference&&wanted;
 }
 bool identityEligible()const{return exact;}bool enabled()const{return on;}
 uint64_t count()const{return total;}unsigned countStored()const{return stored;}
 template<class N>static Sample software(uint64_t ns,uint64_t epoch,unsigned event,bool enabled,
     const N &n,unsigned pool,unsigned leased,unsigned stride,uint64_t poolIOVA,
     uint64_t actions,uint64_t passes,uint64_t budget,unsigned stalled){
  Sample s;s.w[TimeNS]=ns;s.w[Epoch]=epoch;s.w[Kind]=event;s.w[Valid]=Software;
  s.w[Enabled]=enabled;s.w[Status]=unsigned(n.status);s.w[Running]=n.running;
  s.w[Used]=n.used;s.w[Producer]=n.producer;s.w[Consumer]=n.consumer;s.w[RingCount]=n.txCount;
  s.w[PoolCount]=pool;s.w[PoolLeased]=leased;s.w[PoolStride]=stride;
  s.w[PoolBytes]=uint64_t(pool)*stride;s.w[Submitted]=n.txSubmitted;s.w[Completed]=n.txCompleted;
  s.w[IRQActions]=actions;s.w[IRQPasses]=passes;s.w[IRQBudget]=budget;s.w[StalledSeconds]=stalled;
  s.w[SyncStatus]=UINT64_MAX;s.w[PoolIOVA]=poolIOVA;return s;
 }
 static void read(Sample &s,Mask mask,Field field,bool attempted,bool returned,uint64_t value,uint64_t sentinel){
  if(!attempted)return;
  s.w[Attempted]|=mask;if(returned){s.w[Returned]|=mask;s.w[field]=value;
   if(value!=sentinel)s.w[Valid]|=mask;}
 }
 static void descriptor(Sample &s,bool attempted,bool returned,unsigned status,
   uint32_t opts1,uint32_t opts2,uint64_t address,unsigned owned){
  if(!attempted)return;
  s.w[Attempted]|=Descriptor;s.w[SyncStatus]=status;
  if(!returned||status!=0)return;
  s.w[Returned]|=Descriptor;s.w[Valid]|=Descriptor;
  s.w[HeadOpts1]=opts1;s.w[HeadOpts2]=opts2;s.w[HeadIOVA]=address;s.w[Owned]=owned;
  const uint64_t base=s.w[PoolIOVA],bytes=s.w[PoolBytes],length=opts1&0x3fffU;
  if(base&&bytes&&base<=UINT64_MAX-bytes){s.w[Valid]|=PoolRange;
   s.w[HeadInPool]=address>=base&&address-base<bytes&&length<=bytes-(address-base);}
 }
 void record(Sample s,bool watchdog=false){
  if(!on||total==UINT64_MAX)return;
  s.w[Sequence]=++total;samples[next]=s;next=(next+1)%Capacity;
  if(stored<Capacity)++stored;else ++overwritten;
  if(watchdog){lastWatchdog=s;wdStored=true;}
 }
 const uint8_t *encode(unsigned &bytes){
  for(auto &w:encoded)w=0;
  encoded[0]=Schema;encoded[1]=Words;encoded[2]=Capacity;encoded[3]=total;
  encoded[4]=stored;encoded[5]=overwritten;encoded[6]=wdStored;
  for(unsigned i=0;i<stored;++i){const unsigned slot=stored==Capacity?(next+i)%Capacity:i;
   for(unsigned w=0;w<Words;++w)encoded[HeaderWords+i*Words+w]=samples[slot].w[w];}
  if(wdStored)for(unsigned w=0;w<Words;++w)encoded[HeaderWords+Capacity*Words+w]=lastWatchdog.w[w];
  bytes=sizeof(encoded);return reinterpret_cast<const uint8_t*>(encoded);
 }
 bool needsExport()const{return exported!=total;}void didExport(){exported=total;}
 template<class Sink>void write(Sink &s)const{
  s.setProperty("MAC46VTD1Schema",Schema,32);s.setProperty("MAC46VTD1Eligible",exact);
  s.setProperty("MAC46VTD1PreferenceValid",validPreference);s.setProperty("MAC46VTD1Requested",wanted);
  s.setProperty("MAC46VTD1Enabled",on);s.setProperty("MAC46VTD1RowWords",Words,32);
  s.setProperty("MAC46VTD1Capacity",Capacity,32);s.setProperty("MAC46VTD1Total",total,64);
  s.setProperty("MAC46VTD1Stored",stored,32);s.setProperty("MAC46VTD1Overwritten",overwritten,64);
  s.setProperty("MAC46VTD1WatchdogStored",wdStored);
  s.setProperty("MAC46VTD1WatchdogSequence",wdStored?lastWatchdog.w[Sequence]:0,64);
  s.setProperty("MAC46VTD1WatchdogTimeNS",wdStored?lastWatchdog.w[TimeNS]:0,64);
  s.setProperty("MAC46VTD1ExtraMMIOReads",uint64_t(0),32);s.setProperty("MAC46VTD1ExtraDMASync",uint64_t(0),32);
 }
};
}
