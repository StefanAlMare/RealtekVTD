// SPDX-License-Identifier: GPL-2.0-only
#include <IOKit/network/IOEthernetController.h>
#include <IOKit/network/IOEthernetInterface.h>
#include <IOKit/network/IOBasicOutputQueue.h>
#include "TXQOutputQueue.hpp"
#include <IOKit/network/IONetworkStats.h>
#include <IOKit/IOFilterInterruptEventSource.h>
#include "MAC34Offload.hpp"
#include "../src/MAC34TXQueue.hpp"
#include "../src/MAC34TXCompletion.hpp"
#include "../src/MAC46VTDObservation.hpp"
#include "../src/MAC46Features.hpp"
#include "../src/MAC46EEE.hpp"
#include "../src/MAC46N64TXPrecheck.hpp"
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSString.h>
#include <libkern/c++/OSIterator.h>
#include "MacOSBackend.hpp"
#include "StartupDiagnostic.hpp"
#include "../src/ProbeProgram.hpp"
#include "../src/NetworkProgram.hpp"
#include "../src/FramePolicy.hpp"
#include "../src/MTUTransaction.hpp"
#include "../src/GU25FlowControl.hpp"
#include "../src/TXChecksumAssist.hpp"
#include "../src/MAC44TXChecksum.hpp"
#include "../src/MAC44TSO4.hpp"
#include "../src/MAC44TSO6.hpp"
#include "../src/TSO4Assist.hpp"
#include <net/if.h>
#include "../src/FC1BusMasterGuard.hpp"
#include "../src/TXAudit.hpp"
#include "../src/TXRingDiagnostic.hpp"
#include "../src/TXRingPolicy.hpp"
#include "../src/OutputQueuePolicy.hpp"
#include "../src/IRQAudit.hpp"
#include "../src/InterruptSelection.hpp"
#include "../src/PowerLifecycle.hpp"
#include "../src/PauseObservation.hpp"
#include "../src/MulticastFilter.hpp"
#include "../src/RXHandoffDiagnostics.hpp"
#include "../src/RXNoWaitPolicy.hpp"
#include <IOKit/IOCommandGate.h>
#include <IOKit/pwr_mgt/IOPM.h>
#include <IOKit/pwr_mgt/IOPMpowerState.h>
#include <kern/clock.h>
#include <net/kpi_interface.h>
#include <sys/errno.h>
using namespace realtek_next;
static uint64_t jtxClock(void *){uint64_t ticks=0,ns=0;clock_get_uptime(&ticks);absolutetime_to_nanoseconds(ticks,&ns);return ns;}
class RealtekVTDNextEthernet:public IOEthernetController,public PacketSink {
 OSDeclareDefaultStructors(RealtekVTDNextEthernet)
 IOPCIDevice *device=nullptr;MacOSBackend *backend=nullptr;
 IOWorkLoop *loop=nullptr;IOLock *lock=nullptr;
 IOFilterInterruptEventSource *irq=nullptr;IOTimerEventSource *timer=nullptr;
 bool irqAdded=false,timerAdded=false,opened=false,enabled=false,quarantined=false;
 // One lifetime pin while strict GU25 or exact MAC46 mapper DMA resources exist. A refusal keeps it
 // through kernel shutdown; safe terminal cleanup drops it as its final action.
 bool stopSafetyOwnerPinned=false;
 void finishStopSafetyOwner();
 bool promiscuous=false;unsigned generation=0,stalledSeconds=0;
 MulticastFilter multicast;unsigned filterMACVersion=0;bool gu24Profile=false;unsigned configuredTXCount=kTXDescriptorCount;unsigned configuredOutputQueueCapacity=256;
 StartupDiagnostic startup;unsigned phyAttemptSequence=0;
 void recordStartup(IOService *,const char *,const char *,uint32_t status=0);
 void observeProviderOpen(IOService *,ProviderOpenObservation &);
 void observeOpenOwners(IOService *,OpenOwnerSnapshot &);
 bool jumboEligible=false;NetworkResources resources;MTUTransaction mtuTransaction;
 GU25FlowControl flowControl;
 TXChecksumPolicy txChecksumPolicy;TXChecksumStats txChecksumStats;TXChecksumAssistReceipt txChecksumAssist;
 MAC46VTDObservation mac46VTD;
 MAC46Features mac46Features;MAC46EEEPolicy mac46EEE;
 MAC46N64TXPrecheck mac46N64Precheck;bool mac46N64PrecheckEligible()const;
 void refreshMAC46(unsigned);bool configureMAC46Checksum();bool configureMAC46TSO(unsigned,bool);bool applyMAC46EEE(unsigned);
 MAC34TXCompletion mac34TXQ4;bool txq4Requested=true;void retireTSO(unsigned);
 bool txq4OwnershipRetained=false;bool retainTXQ4Ownership(bool terminal=false);
 MAC34TXQueue mac34TXQ1;
 MAC34OffloadState mac34;void refreshMAC34(unsigned);bool configureMAC34Checksum();bool configureMAC34TSO(unsigned,bool);
 MAC44TXChecksumState mac44Checksum;void refreshMAC44Checksum();void refreshMAC44FlowControl();
 MAC44TSO6State mac44TSO6;void refreshMAC44TSO6();bool configureMAC44TSO6(unsigned);
 MAC44TSO4State mac44TSO4;void refreshMAC44TSO4();bool configureMAC44TSO4(unsigned);
 MAC44RXChecksumState mac44RX;void refreshMAC44RXChecksum();bool configureMAC44RXAssist();
 bool configureChecksumAssist();
 TXQ3DrainObservation *txq3=nullptr;bool txq3Requested=true;uint64_t txq3Parent4=0,txq3Parent6=0;void noteTXQ3(unsigned,unsigned);
 TXQ2Observation *txq2=nullptr;bool txq2Requested=true;void noteTXQ2(unsigned,unsigned=0,unsigned=0,unsigned=0);
 TXQObservation *txq=nullptr;unsigned txqPump4=TXQObservation::Unknown,txqPump6=TXQObservation::Unknown;
 void noteRetry(unsigned site,unsigned family,unsigned reason);void noteTXQ(unsigned event,unsigned family=0);bool txqService();
 TXChecksumPolicy txPrecheckPolicy;uint64_t txPrecheck4EarlyFull=0;
 TXChecksumPolicy single1Policy;uint64_t single1Submitted4=0,single1Submitted6=0;
 UInt32 outputPacketImpl(mbuf_t,void*);
 UInt32 submitSingleTSO(mbuf_t,DMABuffer &,unsigned,size_t,bool);
 TXChecksumPolicy csum6Policy;uint64_t tx6Requests=0,tx6Hardware=0,tx6Software=0,tx6Errors=0,rx6Published=0,rx6PublishErrors=0;
 TXChecksumPolicy rxChecksumPolicy;uint64_t rxChecksumPublished=0,rxChecksumPublishErrors=0,rxChecksumCleared=0;
 TXChecksumPolicy tso4Policy;TSO4Stats tso4Stats;TSO4AssistReceipt tso4Assist;TSO4SoftwareState tso4Software;
 bool configureTSO4(unsigned mtu);UInt32 outputTSO4(mbuf_t,uint32_t,unsigned);
 bool pumpTSO4();void finishTSO4();
 TXChecksumPolicy tso6Policy;TSO6Stats tso6Stats;TSO4AssistReceipt tso6Assist;TSO6SoftwareState tso6Software;
 bool configureTSO6(unsigned);bool configureTSOFamilies(unsigned);UInt32 outputTSO6(mbuf_t,uint32_t,unsigned);bool pumpTSO6();void finishTSO6();
 uint64_t mtuQueueFlushed=0,mtuCancelledCookies=0;
 JTXTrace jtxTrace;uint64_t jtxExportedRevision=UINT64_MAX;
 void publishJTXTrace();
 bool startPreparedMTU(unsigned);
 struct MTUTarget {
  RealtekVTDNextEthernet *self;
  bool fc1Quiescent(){
   // BME1: MSI registration can enable BME after start's network-safe-stop.
   // Called by activate under the controller lock, before PAUSE/startNetwork.
   struct GuardIO {
    MacOSBackend *backend;
    bool readPCI(uint16_t &v){return backend->readConfig16(4,v);}
    bool readMAC(uint8_t &v){return backend->read8(0x37,v);}
    bool disableBME(){return backend->setBusMaster(false)==DMAStatus::Ok;}
   } io{self->backend};
   // MAC44 uses the existing stopped lifecycle, not a new DMA recovery path.
   // Do not clear BME while software still owns pending DMA cookies/leases.
   // These are prerequisites, not proof that BME-off drains PCIe transactions.
   if((self->flowControl.mac44()||self->flowControl.mac34()||self->flowControl.mac46())&&self->flowControl.requested&&
      (self->network.running||self->network.used||self->backend->txPoolLeased()))return false;
   FC1BMEReceipt r;
   const bool ok=fc1EstablishQuiescence(io,self->enabled,self->network.unsafeRetained,r);
   // Optional diagnostics, published after the bounded operation; failure to
   // publish must never turn an unverified guard into an accepted one.
   self->setProperty("FC1BMEGuardSchema",1,32);
   self->setProperty("FC1BMEGuardOutcome",r.outcome);
   self->setProperty("FC1BMEGuardDriverEnabled",r.enabled);
   self->setProperty("FC1BMEGuardUnsafeRetained",r.unsafe);
   self->setProperty("FC1BMEBeforeRead",r.beforeRead);
   self->setProperty("FC1BMEPCIBeforeValid",r.pciBeforeValid);
   self->setProperty("FC1BMEMACBeforeValid",r.macBeforeValid);
   self->setProperty("FC1BMEPCIBefore",r.pciBefore,16);
   self->setProperty("FC1BMEMACBefore",r.macBefore,8);
   self->setProperty("FC1BMEDisableAttempted",r.disableAttempted);
   self->setProperty("FC1BMEDisableReturnedOK",r.disableReturnedOK);
   self->setProperty("FC1BMEAfterRead",r.afterRead);
   self->setProperty("FC1BMEPCIAfterValid",r.pciAfterValid);
   self->setProperty("FC1BMEMACAfterValid",r.macAfterValid);
   self->setProperty("FC1BMEPCIAfter",r.pciAfter,16);
   self->setProperty("FC1BMEMACAfter",r.macAfter,8);
   self->setProperty("FC1BMEQuiescenceVerified",r.verified);
   if(ok)self->rings.busMasterEnabled=false; // update only after actual verification
   return ok;
  }
  bool readPause(uint16_t &bits){RegisterAccess access(*self->backend,self->filterMACVersion);PauseProgram p(access);return p.read(bits);}
  bool applyPause(uint16_t bits,bool force,bool &restarted,bool &seen){
   // MTU change/rollback reaches this adapter after its existing checked stop.
   if((self->flowControl.mac44()||self->flowControl.mac34()||self->flowControl.mac46())&&self->flowControl.requested&&!fc1Quiescent())return false;
   RegisterAccess access(*self->backend,self->filterMACVersion);PauseProgram p(access);return p.apply(bits,force,restarted,seen);}
  bool stop(){unsigned pending=0;for(auto cookie:self->network.cookies)pending+=cookie!=nullptr;
   self->halt();const bool safe=!self->network.unsafeRetained;if(safe)self->mtuCancelledCookies+=pending;return safe;}
  bool start(unsigned mtu){return self->startPreparedMTU(mtu);}
  void safeStop(){self->halt();}
 };
 bool applyReceiveFilter();void publishReceiveFilter();
 void publishRXStatistics();
 RXHandoffDiagnostics rxHandoff;bool rxProbeRequested=true;
 TXChecksumPolicy rxFillPolicy;uint64_t rxFillAttempts=0,rxFillPacketsFilled=0,rxFillAllocationFailures=0,rxFillPreparationFailures=0;
 enum class RXPrepareStatus : uint8_t { AllocationFailed, PreparationFailed, NeedsCopy, Filled };
 bool rxFillActive() const;mbuf_t prepareRXPacket(const uint8_t *,unsigned,RXPrepareStatus &);
 RXNoWaitPolicy rxNoWaitPolicy;uint64_t rxNoWaitAttempts=0,rxNoWaitAllocated=0,rxNoWaitFailures=0;
 bool rxNoWaitActive() const;mbuf_t allocateRXPacket(unsigned);
 uint64_t rxProbeSequence=0,rxProbeDelivered=0,rxProbeInputCalls=0,rxProbeFlushReturned=0;
 ifnet_stats_param rxProbeBefore{},rxProbeAfter{};
 errno_t rxProbeBeforeStatus=ENXIO,rxProbeAfterStatus=ENXIO;
 uint64_t previousCompleted=0;uint8_t previousPHY=0xff;
 IONetworkInterface *interface=nullptr;IONetworkStats *stats=nullptr;
 IONetworkMedium *media[10]{};IOEthernetAddress address{};uint64_t pauseSamples=0;
 Stage2AResult rings;NetworkResult network;TXAudit audit;TXRingDiagnostic txRingDiag;IRQAudit irqAudit;PowerLifecycle power;bool powerRegistered=false;
 IOReturn activate();IOReturn applyPower(unsigned long state);
 static IOReturn powerAction(OSObject *owner,void *state,void *,void *,void *);
 IOReturn registerWithPolicyMaker(IOService *) override;
 void captureWatchdog(const char *reason="tx-watchdog");
 void publishPHYDiagnostic(const PHYDiagnostic &,const char *,const char *snapshot=nullptr);
 void publishPause(const PHYDiagnostic &,const PauseResolution &);void reportLinkInactive(const char *);
 bool initializeHardware();bool createMedia();void updateLink();void publish();void halt();
 static bool filter(OSObject *,IOFilterInterruptEventSource *);
 static void interrupt(OSObject *,IOInterruptEventSource *,int);
 static void tick(OSObject *,IOTimerEventSource *);
public:
 IOService *probe(IOService *,SInt32 *) override;
 bool createWorkLoop() override;
 IOWorkLoop *getWorkLoop() const override{return loop;}
 IOOutputQueue *createOutputQueue() override;
 IOReturn setPowerState(unsigned long,IOService *) override;
 bool start(IOService *) override;void stop(IOService *) override;void free() override;
 bool configureInterface(IONetworkInterface *) override;
 IOReturn enable(IONetworkInterface *) override;
 IOReturn disable(IONetworkInterface *) override;
 UInt32 outputPacket(mbuf_t,void *) override;
 IOReturn getChecksumSupport(UInt32 *mask,UInt32 family,bool output) override{
  if(!mask)return kIOReturnBadArgument;*mask=0;
  if(family!=kChecksumFamilyTCPIP)return kIOReturnUnsupported;
  if((output&&txChecksumPolicy.enabled())||(!output&&rxChecksumPolicy.enabled()))*mask=kChecksumIP|kChecksumTCP|kChecksumUDP;
  if(csum6Policy.enabled())*mask|=kChecksumTCPIPv6|kChecksumUDPIPv6;
  if(!output&&mac44RX.declared())*mask|=kChecksumIP|kChecksumTCP|kChecksumUDP|kChecksumTCPIPv6|kChecksumUDPIPv6;
  if(output&&mac44Checksum.enabled())*mask|=kChecksumIP|kChecksumTCP|kChecksumUDP|kChecksumTCPIPv6|kChecksumUDPIPv6;
  if(mac34.tx.enabled()&&output)*mask|=kChecksumIP|kChecksumTCP|kChecksumUDP|kChecksumTCPIPv6|kChecksumUDPIPv6;
  if(mac34.rx.enabled()&&!output)*mask|=kChecksumIP|kChecksumTCP|kChecksumUDP|kChecksumTCPIPv6|kChecksumUDPIPv6;
  if((output&&mac46Features.tx.enabled())||(!output&&mac46Features.rx.enabled()))*mask|=kChecksumIP|kChecksumTCP|kChecksumUDP|kChecksumTCPIPv6|kChecksumUDPIPv6;
  return kIOReturnSuccess;
 }
 UInt32 vlanFeatures() const{return (!gu24Profile||(filterMACVersion==42&&startup.xid==0x509&&startup.cfgMethod==25))?kIONetworkFeatureSoftwareVlan:0;}
 UInt32 getFeatures() const override{return vlanFeatures()|((tso4Policy.enabled()||mac44TSO4.enabled()||mac34.tso4.enabled()||mac46Features.tso4.enabled())?kIONetworkFeatureTSOIPv4:0)|((tso6Policy.enabled()||mac44TSO6.enabled()||mac34.tso6.enabled()||mac46Features.tso6.enabled())?kIONetworkFeatureTSOIPv6:0);}
 IOReturn getMaxPacketSize(UInt32 *size) const override{if(!size)return kIOReturnBadArgument;*size=(resources.jumboReady?kJumboMTU:1500)+18;return kIOReturnSuccess;}
 IOReturn setMaxPacketSize(UInt32) override;
 IOReturn getHardwareAddress(IOEthernetAddress *a) override{if(!a)return kIOReturnBadArgument;*a=address;return kIOReturnSuccess;}
 IOReturn selectMedium(const IONetworkMedium *) override;
 IOReturn setPromiscuousMode(bool) override;
 IOReturn setMulticastMode(bool) override;
 IOReturn setMulticastList(IOEthernetAddress *,UInt32) override;
 const OSString *newVendorString() const override{return OSString::withCString("Realtek");}
 const OSString *newModelString() const override{return OSString::withCString("RealtekVTDNext STAGE3B Ethernet");}
 bool receive(const uint8_t *,unsigned) override;
 bool receive(const uint8_t *,unsigned,const RXChecksumMetadata &) override;
 void releasePacket(void *p) override;
};
OSDefineMetaClassAndStructors(RealtekVTDNextEthernet,IOEthernetController)

bool RealtekVTDNextEthernet::retainTXQ4Ownership(bool terminal){
 // A valid core stop consumes every durable tail. If a malformed cookie leaves
 // a trusted original without its callback, retain it explicitly, never fake
 // its completion or free it speculatively. This latch is never auto-reset.
 if(!txq4OwnershipRetained&&(!mac34TXQ4.liveCount()||(!terminal&&network.unsafeRetained)))return false;
 txq4OwnershipRetained=true;network.unsafeRetained=true;network.status=NetworkStatus::UnsafeStop;
 if(!quarantined){quarantined=true;if(!terminal&&!stopSafetyOwnerPinned)retain();}
 // Terminal callers must return without destroying owner/backend; free() does
 // not resurrect a zero-reference object with retain().
 return true;
}
void RealtekVTDNextEthernet::retireTSO(unsigned family){
 // Copy/emitter retirement is distinct from original-mbuf completion. All
 // descriptor bytes/OWN/DMA mappings stay unchanged; only software cookies move.
 if(!mac34TXQ4.enabled()||mac34TXQ4.failedInvariant()||!enabled||!backend||
    !mac34.nativeReady||!network.mac34Native64Verified||network.macVersion!=34||
    backend->deviceMapperPresent()||!backend->native64NoIOMMUReady()||
    !network.running||network.mtu!=1500||network.status!=NetworkStatus::Ok||network.unsafeRetained)return;
 const bool six=family==6;if(!six&&family!=4)return;
 if(!(six?mac34.tso6.enabled():mac34.tso4.enabled()))return;
 const bool active=six?tso6Software.active():tso4Software.active();
 const bool owned=six?tso6Software.owned:tso4Software.owned;
 const bool remaining=six?tso6Software.remaining():tso4Software.remaining();
 const bool cancelled=six?tso6Software.cancelled:tso4Software.cancelled;
 const unsigned outstanding=six?tso6Software.outstanding:tso4Software.outstanding;
 if(!active||!owned||remaining||cancelled||!outstanding)return;
 void *original=six?tso6Software.original:tso4Software.original;
 void *cookie=six?static_cast<void*>(&tso6Software):static_cast<void*>(&tso4Software);
 const uint64_t diag=six?txq3Parent6:txq3Parent4;
 const auto result=mac34TXQ4.transfer(network,cookie,original,family,generation,outstanding,diag,
  [&](const DMABuffer &b){return backend->ownsTXSnapshot(b);});
 if(result!=MAC34TXCompletion::Success)return; // no preflight failure mutates ownership
 // Legacy TXQ3 schema assumes full parent drain before state clear. Mark the
 // sampled episode inapplicable due to handoff; never stamp a false completion.
 if(txq3&&diag)txq3->cancel(diag,txq3->now(),4);
 if(six){
  DMABuffer aggregate=tso6Software.aggregate;void *cpu=tso6Software.cpuAggregate;const size_t bytes=tso6Software.cpuBytes;
  tso6Software={};txq3Parent6=0;
  // All emitters completed synchronously before handoff. No child references
  // this source; each validated child is an independent private TXPool lease.
  if(cpu)IOFree(cpu,bytes);else{backend->unmap(aggregate);backend->complete(aggregate);backend->release(aggregate);}
 }else{tso4Software={};txq3Parent4=0;}
}
void RealtekVTDNextEthernet::noteTXQ3(unsigned family,unsigned kind){
 auto &id=family==6?txq3Parent6:txq3Parent4;
 if(kind==1001)id=0;
 if(!txq3||!txq3->enabled())return;
 const unsigned remaining=family==6?(tso6Software.remaining()?tso6Software.plan.segments()-tso6Software.next:0):(tso4Software.remaining()?tso4Software.plan.segments()-tso4Software.next:0);
 const unsigned outstanding=family==6?tso6Software.outstanding:tso4Software.outstanding;
 if(kind==1001){id=txq3->begin(generation,family,txq3->now(),family==6?tso6Software.plan.segments():tso4Software.plan.segments(),remaining,outstanding,network.used);return;}
 if(!id)return;
 if(kind==1002){txq3->cancel(id,txq3->now(),1);id=0;return;}
 txq3->stage(id,static_cast<TXQ3DrainObservation::Stage>(kind),txq3->now(),remaining,outstanding,network.used);
}
void RealtekVTDNextEthernet::noteRetry(unsigned site,unsigned family,unsigned reason){
 if(filterMACVersion==46&&mac46N64PrecheckEligible())mac46N64Precheck.retry(site,family,reason);
 if(txq3&&txq3->enabled()){
  const uint64_t id=family==6?txq3Parent6:family==4?txq3Parent4:txq3Parent4?txq3Parent4:txq3Parent6;
  if(id)txq3->retry(id,site,txq3->now());
 }
 noteTXQ2(1,site,family,reason);
 if(!txq||!txq->enabled())return;
 txq->setEpoch(generation);TXQEvent e;e.epoch=generation;e.begin=e.end=txq->now();e.site=site;e.path=family;e.reason=reason;
 e.used=network.used;e.free=network.used<=network.txCount?network.txCount-network.used:0;e.producer=network.producer;e.consumer=network.consumer;e.reclaimed=network.txCompleted;
 e.parent4=tso4Software.active();e.parent6=tso6Software.active();e.validMask=TXQObservation::Interval|TXQObservation::Epoch|TXQObservation::Resources|TXQObservation::Parents;txq->retry(e);
}
void RealtekVTDNextEthernet::noteTXQ(unsigned event,unsigned family){
 noteTXQ2(event==TXQObservation::ParentEnd?3:7);
 if(!txq||!txq->enabled())return;txq->setEpoch(generation);TXQEvent e;e.event=event;e.path=family;e.epoch=generation;e.begin=e.end=txq->now();
 e.used=network.used;e.free=network.used<=network.txCount?network.txCount-network.used:0;e.producer=network.producer;e.consumer=network.consumer;e.reclaimed=network.txCompleted;
 e.parent4=tso4Software.active();e.parent6=tso6Software.active();e.validMask=TXQObservation::Interval|TXQObservation::Epoch|TXQObservation::Resources|TXQObservation::Parents;txq->record(e);
}
void RealtekVTDNextEthernet::noteTXQ2(unsigned kind,unsigned site,unsigned family,unsigned reason){
 if(!txq2||!txq2->enabled())return;
 TXQ2Context c;c.epoch=generation;clock_get_uptime(&c.at);c.kind=kind;
 c.producer=network.producer;c.consumer=network.consumer;c.used=network.used;c.capacity=network.txCount;c.completed=network.txCompleted;
 c.parent4=tso4Software.active();c.remaining4=tso4Software.remaining()?tso4Software.plan.segments()-tso4Software.next:0;c.outstanding4=tso4Software.outstanding;
 c.parent6=tso6Software.active();c.remaining6=tso6Software.remaining()?tso6Software.plan.segments()-tso6Software.next:0;c.outstanding6=tso6Software.outstanding;
 c.serviceAllowed=kind==4;c.networkStatus=unsigned(network.status);txq2->context(c,site,family,reason);
}
bool RealtekVTDNextEthernet::txqService(){
 const auto begin=txq?txq->now():0;const bool result=getOutputQueue()->service(IOBasicOutputQueue::kServiceAsync);
 if(txq&&txq->enabled()){TXQEvent e;e.event=TXQObservation::Service;e.begin=begin;e.end=txq->now();e.epoch=txq->currentEpoch();e.result=result;e.validMask=TXQObservation::Interval|TXQObservation::Epoch|TXQObservation::ServiceResult;txq->record(e);}return result;
}

void RealtekVTDNextEthernet::recordStartup(IOService *provider,const char *stage,const char *outcome,uint32_t status){
 if(!startup.next())return;
 IOLog("RVT-GU25-P11 seq=%u stage=%s outcome=%s status=%u score-valid=%u score-before=%d score-after=%d tx-valid=%u TxConfig=%08x XID=%x CFG=%u\n",
  startup.sequence,stage,outcome,status,startup.scoreValid,startup.scoreBefore,startup.scoreAfter,
  startup.txConfigValid,startup.txConfig,startup.xid,startup.cfgMethod);
 // Publish a new dictionary, never mutate a previously published snapshot.
 // Every failure here is optional evidence loss, never a driver refusal.
 if(!provider)return;
 auto dict=OSDictionary::withCapacity(64);if(!dict)return;
 struct Sink {
  OSDictionary *dict;
  bool number(const char *key,uint64_t value){auto n=OSNumber::withNumber(value,64);if(!n)return false;
   const bool ok=dict->setObject(key,n);n->release();return ok;}
  bool text(const char *key,const char *value){auto s=OSString::withCString(value);if(!s)return false;
   const bool ok=dict->setObject(key,s);s->release();return ok;}
 }sink{dict};
 if(startup.write(sink,getRegistryEntryID(),stage,outcome,status))provider->setProperty("RVT-GU25-P11",dict);
 dict->release();
}
IOService *RealtekVTDNextEthernet::probe(IOService *provider,SInt32 *score){
 // Read only public software state before any probe diagnostic publication.
 observeProviderOpen(provider,startup.beforeProbe);++startup.probeCalls;
 if(provider){startup.providerRegistryID=provider->getRegistryEntryID();
  ownerCopyText(startup.providerClass,sizeof(startup.providerClass),provider->getMetaClass()->getClassName());
  ownerCopyText(startup.providerName,sizeof(startup.providerName),provider->getName());}
 // No MMIO/PCI access: preserve the inherited result and score exactly.
 startup.probeEntered=true;startup.scoreValid=score!=nullptr;
 if(score)startup.scoreBefore=startup.scoreAfter=*score;
 recordStartup(provider,"probe-enter","enter");
 auto result=IOEthernetController::probe(provider,score);
 observeProviderOpen(provider,startup.afterProbe);
 startup.probeReturned=true;startup.probeAccepted=result!=nullptr;startup.probeReturnedSelf=result==this;
 if(score)startup.scoreAfter=*score;
 recordStartup(provider,"probe-result",result?"pass":"fail");
 if(!result||result!=this)startup.sealed=true;
 return result;
}
bool RealtekVTDNextEthernet::createWorkLoop(){
 if(!loop)loop=IOWorkLoop::workLoop();
 startup.workLoopCalled=true;startup.workLoopOK=loop!=nullptr;
 recordStartup(getProvider(),"create-workloop",loop?"pass":"fail");return loop;
}
IOOutputQueue *RealtekVTDNextEthernet::createOutputQueue(){
 // Client/async consumer maps mbufs outside the workloop gate; only publication is locked.
 auto result=RVTTXQOutputQueue::create(this);
 if(result){txq=&result->observation;txq2=&result->dropObservation;txq3=&result->drainObservation;}
 startup.outputQueueCalled=true;startup.outputQueueOK=result!=nullptr;
 recordStartup(getProvider(),"create-output-queue",result?"pass":"fail");return result;
}
bool RealtekVTDNextEthernet::initializeHardware(){
 rxNoWaitPolicy.reset();rxFillPolicy.configure(false);
 mac46Features.tx.configure(false);mac46Features.rx.configure(false);mac46Features.tso4.configure(false);mac46Features.tso6.configure(false);
 network.mac46TXAuthorized=network.mac46TSO4Authorized=network.mac46TSO6Authorized=false;network.mac46Native64Verified=false;
 mac44Checksum.support.configure(false);mac44Checksum.hardwareInitialized=false;mac44Checksum.assist=TXChecksumAssistReceipt{};
 mac44RX.configure(false,false,false,false); // No stale RX declaration/receipt after a failed reinitialization.
 mac44TSO4.reset();network.mac44TSO4Authorized=false;network.mac44TSO4Limit=0;
 mac44TSO6.reset();network.mac44TSO6Authorized=false;network.mac44TSO6Limit=0;
 txPrecheckPolicy.configure(false);
 single1Policy.configure(false);
 tso4Policy.configure(false);tso6Policy.configure(false);
 txChecksumPolicy.configure(false);rxChecksumPolicy.configure(false);csum6Policy.configure(false);network.rxChecksumRequested=false;network.rxChecksum6Requested=false;network.rxChecksumProfile=RXChecksumProfile::GU25RevisionC;
 recordStartup(device,"txconfig-read","enter");
 uint32_t config=0;if(!backend->read32(0x40,config)){
  recordStartup(device,"txconfig-read","fail");return false;
 }
 auto id=identify(device->configRead16(UInt8(0)),device->configRead16(UInt8(2)),config,true);
 startup.txConfigValid=true;startup.txConfig=config;startup.xid=id.xid;
 startup.identityStatus=unsigned(id.status);startup.chip=unsigned(id.chip);
 startup.cfgMethod=cfgMethod(id.chip);
 recordStartup(device,"identify",id.status==ProbeStatus::Supported?"pass":"fail",unsigned(id.status));
 setProperty("StartupTxConfig",config,32);setProperty("StartupXID",id.xid,32);
 if(id.status!=ProbeStatus::Supported)return false;
 filterMACVersion=id.linuxMacVersion;
 // FC1 reads only its optional Boolean personality property. An absent property
 // requests negotiated AUTO on the exact GU25 identity; false preserves OFF.
 FC1Request flowRequest=FC1Request::Off;
 if(fc1Identity(id)){
  auto option=getProperty("GU25FlowControlEnabled");
  flowRequest=!option?FC1Request::Default:option==kOSBooleanTrue?FC1Request::On:
              option==kOSBooleanFalse?FC1Request::Off:FC1Request::Invalid;
 }
 flowControl.configure(id,flowRequest);
 gu24Profile=isGUProfile(id.chip); // Existing conservative GU policy, also for CFG25.
 network.macVersion=id.linuxMacVersion;configuredTXCount=txDescriptorCountForIdentity(id);
 configuredOutputQueueCapacity=outputQueueCapacityForIdentity(id);
 auto data=gu24Profile?nullptr:OSDynamicCast(OSData,getProperty(id.chip==Chip::RTL8411B?"Firmware8411B":id.chip==Chip::RTL8168EVL?"Firmware8168E3":"Firmware8168H"));
 recordStartup(device,"hardware-program","enter");
 auto result=runProbe(*backend,*backend,device->getFunctionNumber(),0x10ec,0x8168,
  data?static_cast<const uint8_t*>(data->getBytesNoCopy()):nullptr,data?data->getLength():0);
 startup.macConfig=result.macConfig;
 startup.phyConfig=result.phyConfig;
 startup.hardwareReturned=true;startup.hardwareStage=result.stage;startup.hardwareStatus=unsigned(result.status);
 startup.hardwareComplete=result.complete;startup.patchVerified=result.gu24MACPatchVerified||result.gu25MACPatchVerified;
 startup.gu25PatchVerified=result.gu25MACPatchVerified;startup.gu25PHYMCUApplied=result.gu25PHYMCUApplied;
 startup.programXID=result.identity.xid;startup.programIdentityStatus=unsigned(result.identity.status);
 startup.programChip=unsigned(result.identity.chip);startup.programCFGMethod=cfgMethod(result.identity.chip);
 recordStartup(device,result.stage,result.complete&&(gu24Profile?(result.gu24MACPatchVerified||result.gu25MACPatchVerified):result.firmwareApplied)?"pass":"fail",unsigned(result.status));
 jumboEligible=jumboIdentityValid(result.identity,result.autoneg.capabilities);
 if(result.complete&&result.gu25MACPatchVerified&&txChecksumIdentity(id)&&txChecksumIdentity(result.identity)){
  // SINGLE1 is restricted to this exact, successfully initialized GU25.
  auto rxNoWaitOption=getProperty("GU25RXNoWaitEnabled");
  rxNoWaitPolicy.configure(true,id.linuxMacVersion,cfgMethod(id.chip),id.xid,!rxNoWaitOption||rxNoWaitOption==kOSBooleanTrue);
  auto rxFillOption=getProperty("GU25RXFillEnabled");
  rxFillPolicy.configure(!rxFillOption||rxFillOption==kOSBooleanTrue);
  auto observerOption=getProperty("GU25TXQOBS1Enabled");
  if(txq){txq->configure(true,!observerOption||observerOption==kOSBooleanTrue);txq->setEpoch(generation);}
  auto precheckOption=getProperty("GU25TXPrecheckEnabled");
  txPrecheckPolicy.configure(!precheckOption||precheckOption==kOSBooleanTrue);
  auto singleOption=getProperty("GU25TSOSingleEnabled");
  single1Policy.configure(!singleOption||singleOption==kOSBooleanTrue);
  auto receiveOption=getProperty("GU25RXChecksumEnabled");
  auto csum6Option=getProperty("GU25CSUM6Enabled");csum6Policy.configure(!csum6Option||csum6Option==kOSBooleanTrue);network.rxChecksum6Requested=csum6Policy.enabled();
  rxChecksumPolicy.configure(!receiveOption||receiveOption==kOSBooleanTrue);network.rxChecksumRequested=rxChecksumPolicy.enabled();
  auto checksumOption=getProperty("GU25TXChecksumEnabled");
  txChecksumPolicy.configure(!checksumOption||checksumOption==kOSBooleanTrue);
 }
 setProperty("ChipProfile",id.chip==Chip::RTL8168GU_Method25?"RTL8168GU-CFG_METHOD_25-MAC_VER_42":gu24Profile?"RTL8168GU-CFG_METHOD_24":id.chip==Chip::RTL8411B?"RTL8411B-MAC_VER_44":id.chip==Chip::RTL8168EVL?"RTL8168EVL-MAC_VER_34":id.chip==Chip::RTL8168H?"RTL8168H-MAC_VER_46":"RTL8168M-MAC_VER_46");
 setProperty("CFGMethod",cfgMethod(id.chip),32);
 setProperty("ExternalPHYFirmwareRequired",!gu24Profile);
 if(id.chip==Chip::RTL8168GU_Method24){
  setProperty("GU24TxConfig",config,32);
  setProperty("GU24MACPatchVerified",result.gu24MACPatchVerified);
  setProperty("GU24NoIOMMURequired",false);setProperty("GU24HardwareValidated",false);
  setProperty("GU24PauseEnabled",false);setProperty("GU24VLANEnabled",false);
  setProperty("GU24ReferenceBoardMatch",device->configRead8(UInt8(0x08))==0x10&&
   device->configRead16(UInt8(0x2c))==0x1043&&device->configRead16(UInt8(0x2e))==0x200f);
 }
 if(id.chip==Chip::RTL8168GU_Method25){
  setProperty("GU25TxConfig",config,32);setProperty("GU25MACPatchVerified",result.gu25MACPatchVerified);
  setProperty("GU25PHYMCUApplied",result.gu25PHYMCUApplied);setProperty("GU25HardwareValidated",false);
  setProperty("GU25EEEEnabled",false);setProperty("GU25PauseEnabled",false);setProperty("GU25VLANEnabled",(getFeatures()&kIONetworkFeatureSoftwareVlan)!=0);
 }
 setProperty("PCIRevision",device->configRead8(UInt8(0x08)),8);
 setProperty("PCISubsystemVendor",device->configRead16(UInt8(0x2c)),16);
 setProperty("PCISubsystemDevice",device->configRead16(UInt8(0x2e)),16);
 publishPHYDiagnostic(result.phyInitDiag,"phy-init","PHYDiagAfterPHYInit");
 publishPHYDiagnostic(result.phyResetDiag,"soft-reset","PHYDiagAfterSoftReset");
 RegisterAccess diagAccess(*backend,filterMACVersion);
 publishPHYDiagnostic(capturePHYDiagnostic(diagAccess),"initialize-hardware","PHYDiagAfterInitializeHardware");
 setProperty("PHYAutonegConfigured",result.autoneg.configured);
 setProperty("PHYCapabilitiesBMSR",result.autoneg.capabilities.bmsr,16);
 setProperty("PHYCapabilitiesBMSRFirst",result.autoneg.capabilities.bmsrFirst,16);
 setProperty("PHYCapabilitiesESTATUS",result.autoneg.capabilities.estatus,16);
 setProperty("PHYCapabilitiesESTATUSRead",result.autoneg.capabilities.estatusRead);
 setProperty("PHYAutonegANARBefore",result.autoneg.anarBefore,16);
 setProperty("PHYAutonegANARAfter",result.autoneg.anarAfter,16);
 setProperty("PHYAutonegCTRL1000Before",result.autoneg.ctrl1000Before,16);
 setProperty("PHYAutonegCTRL1000After",result.autoneg.ctrl1000After,16);
 setProperty("PHYAutonegCTRL1000Accessed",result.autoneg.ctrl1000Accessed);
 setProperty("PHYAutonegBMCRBefore",result.autoneg.bmcrBefore,16);
 setProperty("PHYAutonegBMCRRestart",result.autoneg.bmcrRestart,16);
 setProperty("PHYAutonegStatus",unsigned(result.autoneg.status),32);
 setProperty("PHYAutonegStage",result.autoneg.stage);
 setProperty("PHYAutonegValidMask",result.autoneg.valid,32);
 setProperty("MACVersion",id.linuxMacVersion,32);setProperty("XID",id.xid,32);
 setProperty("Stage1Complete",result.complete);setProperty("FirmwareApplied",result.firmwareApplied);
 setProperty("ProbeStatus",unsigned(result.status),32);setProperty("ProbeStage",result.stage);
 // Publish current-attempt evidence after runProbe's existing cleanup, even after startup sealing.
 if(gu24Profile){
  setProperty("ProbePHYAttemptSequence",++phyAttemptSequence,32);
  setProperty("ProbeProgramTxConfigValid",result.txConfigValid);if(result.txConfigValid)setProperty("ProbeProgramTxConfig",result.txConfig,32);
  setProperty("ProbeProgramChip",unsigned(result.identity.chip),32);setProperty("ProbeProgramCFGMethod",cfgMethod(result.identity.chip),32);
  setProperty("ProbeProgramXID",result.identity.xid,32);
  struct PHYSink{RealtekVTDNextEthernet *owner;
   bool number(const char *key,uint64_t value){return owner->setProperty(key,value,64);}
   bool text(const char *key,const char *value){return owner->setProperty(key,value);}
  }sink{this};result.phyConfig.publish(sink);
 }

 setProperty("EEEAdvertisement",result.phy.eeeAdvertisement,32);
 refreshMAC44Checksum();refreshMAC44FlowControl();refreshMAC44RXChecksum();refreshMAC34(mtuTransaction.mtu);
 // Configure only on hardware initialization/DMA selection, not on checksum/TSO
 // refresh during an active MTU transaction (which must retain its applied receipt).
 if(filterMACVersion==34&&mac34.nativeReady)
  flowControl.configureMAC34(mac34.identity,mac34.initialized,mac34.nativeReady);
 refreshMAC46(mtuTransaction.mtu);
 if(filterMACVersion==46){flowControl.configureMAC46(mac46Features.identityEligible,mac46Features.hardwareInitialized,mac46Features.mapperReady,FC1Request::Default,mac46Features.native64Ready);Identity eeeIdentity{ProbeStatus(startup.identityStatus),Chip(startup.chip),uint16_t(startup.xid),uint8_t(filterMACVersion)};mac46EEE.configure(eeeIdentity,mac46Features.identityEligible&&mac46Features.hardwareInitialized,mac46Features.mapperReady,getProperty("FirmwareApplied")==kOSBooleanTrue,mac46Features.native64Ready);
  if(eeeIdentity.chip==Chip::RTL8168M){
   const bool initializedM=startup.hardwareComplete&&startup.hardwareStatus==0&&
    startup.programIdentityStatus==unsigned(ProbeStatus::Supported)&&startup.programChip==unsigned(Chip::RTL8168M)&&
    startup.programXID==0x6c0&&startup.programCFGMethod==0;
   mac46EEE.configureLinkM(eeeIdentity,initializedM,backend->dmaReady(),backend->deviceMapperPresent(),
    getProperty("FirmwareApplied")==kOSBooleanTrue,backend->native64NoIOMMUReady());
  }
 }
 return result.complete&&(gu24Profile?(result.gu24MACPatchVerified||result.gu25MACPatchVerified):result.firmwareApplied);
}
bool RealtekVTDNextEthernet::createMedia(){
 auto dict=OSDictionary::withCapacity(10);if(!dict)return false;
 const UInt32 types[10]={kIOMediumEthernetAuto,kIOMediumEthernet10BaseT,kIOMediumEthernet10BaseT|kIOMediumOptionFullDuplex,
  kIOMediumEthernet100BaseTX,kIOMediumEthernet100BaseTX|kIOMediumOptionFullDuplex,
  kIOMediumEthernet1000BaseT,kIOMediumEthernet1000BaseT|kIOMediumOptionFullDuplex,
  kIOMediumEthernet10BaseT|kIOMediumOptionFullDuplex|kIOMediumOptionFlowControl,
  kIOMediumEthernet100BaseTX|kIOMediumOptionFullDuplex|kIOMediumOptionFlowControl,
  kIOMediumEthernet1000BaseT|kIOMediumOptionFullDuplex|kIOMediumOptionFlowControl};
 const UInt64 speeds[10]={0,10000000,10000000,100000000,100000000,1000000000,1000000000,10000000,100000000,1000000000};
 bool ok=true;for(unsigned i=0;i<(gu24Profile&&!flowControl.eligible?7U:10U);++i){media[i]=IONetworkMedium::medium(types[i],speeds[i],0,i);
  if(!media[i]||!IONetworkMedium::addMedium(dict,media[i])){ok=false;break;}}
 // These extra media describe the active result; selectMedium stays auto-only.
 if(ok)ok=publishMediumDictionary(dict)&&setSelectedMedium(media[0]);dict->release();return ok;
}
void RealtekVTDNextEthernet::observeProviderOpen(IOService *provider,ProviderOpenObservation &observation){
 if(!provider)return;
 observation.selfOpen=provider->isOpen(this);
 observation.anyOpen=provider->isOpen(nullptr);
 observation.inactive=provider->isInactive();
 observation.available=true;
}
void RealtekVTDNextEthernet::observeOpenOwners(IOService *provider,OpenOwnerSnapshot &snapshot){
 snapshot.attempted=true;if(!provider)return;
 OSIterator *iterator=provider->getOpenClientIterator();if(!iterator)return;
 snapshot.iteratorAvailable=true;
 // The current framework iterator entry is locked/retained. Only copy bounded
 // identity evidence here; release the iterator before publishing or starting.
 for(unsigned i=0;i<=OpenOwnerSnapshot::limit;++i){
  OSObject *object=iterator->getNextObject();
  if(!object){snapshot.exhausted=true;break;}
  if(i==OpenOwnerSnapshot::limit){snapshot.truncated=true;break;}
  auto client=OSDynamicCast(IOService,object);if(!client){snapshot.nonServiceSeen=true;continue;}
  auto &owner=snapshot.owners[snapshot.count++];
  owner.registryID=client->getRegistryEntryID();owner.isSelf=client==this;owner.inactive=client->isInactive();
  owner.text(1,owner.className,sizeof(owner.className),client->getMetaClass()->getClassName());
  owner.text(2,owner.name,sizeof(owner.name),client->getName());
  auto property=[client,&owner](const char *key,unsigned bit,char *out,unsigned capacity){
   OSObject *value=client->copyProperty(key);auto string=OSDynamicCast(OSString,value);
   owner.text(bit,out,capacity,string?string->getCStringNoCopy():nullptr);
   if(value)value->release();
  };
  property("CFBundleIdentifier",4,owner.bundleID,sizeof(owner.bundleID));
  property("DriverVersion",8,owner.driverVersion,sizeof(owner.driverVersion));
  property("RealtekVTDNextBuild",16,owner.build,sizeof(owner.build));
 }
 snapshot.iteratorValid=iterator->isValid();iterator->release();
}
bool RealtekVTDNextEthernet::start(IOService *provider){
 // A failed start must balance a successful superclass start before detaching.
 // Keep the existing unsafe-DMA ownership protection alive across that balance.
 auto fail=[this,provider](const char *stage){
  if(startup.superStartReturned&&startup.superStartOK&&!startup.superStopCalled){
   startup.failureUnsafeDMA=rings.unsafeMappingRetained||network.unsafeRetained;
   if(startup.failureUnsafeDMA&&!quarantined){quarantined=true;if(!stopSafetyOwnerPinned)retain();if(backend&&network.stopSafety.latched)backend->retainStopSafety();startup.failureRetainAdded=true;}
   startup.failureQuarantined=quarantined;
   txq3=nullptr;txq2=nullptr;IOEthernetController::stop(provider);startup.superStopCalled=true;
  }
  recordStartup(provider,stage,"fail");startup.sealed=true;finishStopSafetyOwner();return false;
 };
 observeProviderOpen(provider,startup.beforeStart);++startup.startCalls;
 startup.startEntered=true;recordStartup(provider,"start-enter","enter");
 const bool superOK=IOEthernetController::start(provider);
 startup.superStartReturned=true;startup.superStartOK=superOK;
 recordStartup(provider,"super-start-result",superOK?"pass":"fail");
 if(!superOK)return fail("controller-super-start");
 if(!loop)return fail("workloop-unavailable");
 if(loop->inGate())return fail("start-gated");
 recordStartup(provider,"lock-allocation","enter");
 lock=IOLockAlloc();if(!lock)return fail("lock-allocation");
 auto txq1Option=getProperty("MAC34TXQ1Enabled");
 mac34TXQ1.configure(!txq1Option||txq1Option==kOSBooleanTrue);
 auto txq4Option=getProperty("MAC34TXQ4Enabled");txq4Requested=!txq4Option||txq4Option==kOSBooleanTrue;
 auto txq3Option=getProperty("MAC34TXQ3Enabled");txq3Requested=!txq3Option||txq3Option==kOSBooleanTrue;
 auto txq2Option=getProperty("MAC34TXQ2Enabled");txq2Requested=!txq2Option||txq2Option==kOSBooleanTrue;
 device=OSDynamicCast(IOPCIDevice,provider);if(!device)return fail("provider-type");device->retain();
 recordStartup(provider,"provider-open","enter");
 const bool openOK=device->open(this);
 startup.providerOpenReturned=true;startup.providerOpenOK=openOK;
 if(!openOK)return fail("provider-open");opened=true;
 backend=new MacOSBackend;if(!backend)return fail("backend-allocation");
 recordStartup(provider,"PCI-D0-MMIO-attach","enter");
 if(!backend->attach(device))return fail("PCI-D0-MMIO-attach");
 if(!initializeHardware())return fail("hardware-profile-init");
 recordStartup(provider,"DMA-mode-selection","enter");
 const bool dmaOK=backend->initializeDMA();
 startup.dmaReturned=true;startup.mapperPresent=backend->deviceMapperPresent();startup.dmaMode=backend->dmaModeName();
 recordStartup(provider,"DMA-mode-result",dmaOK?"pass":"fail");
 if(!dmaOK)return fail("DMA-mode-selection");
 refreshMAC44Checksum();refreshMAC44FlowControl();refreshMAC44RXChecksum();refreshMAC34(mtuTransaction.mtu);
 // Configure only on hardware initialization/DMA selection, not on checksum/TSO
 // refresh during an active MTU transaction (which must retain its applied receipt).
 if(filterMACVersion==34&&mac34.nativeReady)
  flowControl.configureMAC34(mac34.identity,mac34.initialized,mac34.nativeReady);
 refreshMAC46(mtuTransaction.mtu);
 if(filterMACVersion==46){flowControl.configureMAC46(mac46Features.identityEligible,mac46Features.hardwareInitialized,mac46Features.mapperReady,FC1Request::Default,mac46Features.native64Ready);Identity eeeIdentity{ProbeStatus(startup.identityStatus),Chip(startup.chip),uint16_t(startup.xid),uint8_t(filterMACVersion)};mac46EEE.configure(eeeIdentity,mac46Features.identityEligible&&mac46Features.hardwareInitialized,mac46Features.mapperReady,getProperty("FirmwareApplied")==kOSBooleanTrue,mac46Features.native64Ready);
  if(eeeIdentity.chip==Chip::RTL8168M){
   const bool initializedM=startup.hardwareComplete&&startup.hardwareStatus==0&&
    startup.programIdentityStatus==unsigned(ProbeStatus::Supported)&&startup.programChip==unsigned(Chip::RTL8168M)&&
    startup.programXID==0x6c0&&startup.programCFGMethod==0;
   mac46EEE.configureLinkM(eeeIdentity,initializedM,backend->dmaReady(),backend->deviceMapperPresent(),
    getProperty("FirmwareApplied")==kOSBooleanTrue,backend->native64NoIOMMUReady());
  }
 }
 setProperty("Stage3DTXPrivateSnapshot",true);setProperty("DriverVersion","0.21.0-GU25-P14-IDX257-SCAN1-JTXFAST1-FC1-BME1-VLAN1-JUMBO1-TXCSUM1-TSO4-OPT1-PHYMCU1-ECN1-RXCSUM1-RXCSUM2-CSUM6-TSO6-SINGLE1-TXQOBS1-EMITCOPY1-RXNOWAIT1-TXPRECHECK1-RXFILL1-STOPSAFE1-MAC44-TXCSUM1-MAC44-FC1-MAC44-RXCSUM1-MAC44-TSO4-MAC44-TSO6-MAC44-NOIOMMU1-MAC44-VLANTSO1-MAC44-VLANTSO2-MAC44-VLANTSO3-MAC34-NOIOMMU1-MAC34-NOIOMMU2-FC1-MAC34-TXQ1-MAC34-TXQ2-DIAG-MAC34-TXQ3-DIAG-MAC34-TXQ4-MAC46-APPLEVTD1-DIAG-MAC46-FULL-APPLEVTD-ON-MAC46-FULL-NOIOMMU-MAC46-LPI1-MAC46-LPI2-MAC46-N64-TXPRECHECK-B");setProperty("DeviceMapperPresent",backend->deviceMapperPresent());setProperty("DMAMode",backend->dmaModeName());
 auto parent=device->getProperty("iommu-parent");
 setProperty("ProviderIOMMUParentType",parent?parent->getMetaClass()->getClassName():"absent");
 setProperty("ProviderIOPCIUseDeviceMapperIsTrue",device->getProperty("IOPCIUseDeviceMapper")==kOSBooleanTrue);
 // DMA preparation remains in the ungated start context, as in P1.
 recordStartup(provider,"DMA-rings","enter");
 rings=runStage2A(*backend,*backend,configuredTXCount);
 startup.ringsReturned=true;startup.ringsStage=rings.stage;startup.ringsStatus=unsigned(rings.status);
 startup.ringsComplete=rings.complete;startup.unsafeRetained=rings.unsafeMappingRetained;
 recordStartup(provider,rings.stage,rings.complete?"pass":"fail",unsigned(rings.status));
 if(!rings.complete)return fail(rings.unsafeMappingRetained?"rings-unsafe-BME-retained":"rings-prepare");
 recordStartup(provider,"network-resources","enter");
 resources=backend->initializeNetworkResources(rings,network,jumboEligible);
 startup.resourcesReturned=true;startup.resourcesReady=resources.ready;
 jtxTrace.setClock(jtxClock);network.trace=&jtxTrace;
 if(!resources.ready)return fail("network-resources");
 refreshMAC34(mtuTransaction.mtu);refreshMAC46(mtuTransaction.mtu);
 // Acer AppleVTD bring-up: the mapper must be real, never inferred from intent.
 const bool initialized46=startup.hardwareComplete&&startup.hardwareStatus==0&&
  startup.programIdentityStatus==unsigned(ProbeStatus::Supported)&&startup.programChip==unsigned(Chip::RTL8168H)&&
  startup.programXID==0x541&&startup.programCFGMethod==0;
 const bool resources46=resources.ready&&rings.complete&&network.prepared&&network.txCount==64&&
  backend->txPoolCount()==65&&backend->txPoolStride()>=2048;
 const bool strict46=stopSafetyMAC46VTD(filterMACVersion,startup.cfgMethod,startup.xid,
  initialized46,backend->dmaReady(),backend->deviceMapperPresent(),resources46);
 const bool strict46Native=stopSafetyMAC46Native64(filterMACVersion,startup.cfgMethod,startup.xid,
  initialized46&&mac46Features.identityEligible&&mac46Features.hardwareInitialized,backend->dmaReady(),backend->deviceMapperPresent(),backend->native64NoIOMMUReady(),resources46);
 const auto observation46=getProperty("MAC46VTD1Enabled");
 mac46VTD.configure(MAC46VTDObservation::eligible(filterMACVersion,startup.cfgMethod,startup.xid,
  initialized46,backend->dmaReady(),backend->deviceMapperPresent(),resources46,network.txCount,
  backend->txPoolCount(),backend->txPoolStride())||MAC46VTDObservation::eligibleNative64(filterMACVersion,startup.cfgMethod,startup.xid,
  initialized46&&mac46Features.identityEligible&&mac46Features.hardwareInitialized,backend->dmaReady(),backend->deviceMapperPresent(),backend->native64NoIOMMUReady(),resources46,network.txCount,backend->txPoolCount(),backend->txPoolStride()),
  !observation46||observation46==kOSBooleanTrue||observation46==kOSBooleanFalse,
  !observation46||observation46==kOSBooleanTrue);
 setProperty("MAC46VTD1StrictStopPolicy",strict46);
 setProperty("MAC46NOIOMMU1StrictStopPolicy",strict46Native);
 // Strict stop is mandatory in this candidate and independent of observation ON/OFF.
 network.stopSafety.enabled=stopSafetyGU25(filterMACVersion,startup.cfgMethod,startup.xid,
  backend->dmaReady()&&!backend->deviceMapperPresent())||strict46||strict46Native;
 if(network.stopSafety.enabled&&!stopSafetyOwnerPinned){stopSafetyOwnerPinned=true;retain();}
 recordStartup(provider,"network-safe-stop","enter");
 if(!stopNetwork(*backend,*backend,rings,network,*this,backend))return fail("network-safe-stop");
 setProperty("JumboResourcesReady",resources.jumboReady);setProperty("JumboPreparationFallback",resources.jumboFallback);
 setProperty("JumboRXBufferBytes",network.bufferSize,32);
 recordStartup(provider,"MAC-address-read","enter");
 uint32_t low=0;uint16_t high=0;if(!backend->read32(0,low)||!backend->read16(4,high))return fail("MAC-address-read");
 for(unsigned i=0;i<4;++i)address.bytes[i]=uint8_t(low>>(8*i));
 address.bytes[4]=uint8_t(high);address.bytes[5]=uint8_t(high>>8);
 bool nonzero=false;for(auto b:address.bytes)nonzero|=b!=0;if(!nonzero||(address.bytes[0]&1))return fail("MAC-address-invalid");
 recordStartup(provider,"media-create","enter");
 if(!createMedia())return fail("media-create");
 recordStartup(provider,"IRQ-selection","enter");
 auto controllers=OSDynamicCast(OSArray,device->getProperty("IOInterruptControllers"));
 const auto selected=selectMessageInterrupt(device,controllers?controllers->getCount():0,
  kIOInterruptTypePCIMessaged,[](void *context,unsigned index,int &type){
   return static_cast<IOPCIDevice*>(context)->getInterruptType(int(index),&type)==kIOReturnSuccess;
  });
 startup.irqReturned=true;startup.irqAvailable=selected.found;startup.irqIndex=selected.index;
 setProperty("Stage3GMessageInterruptSelected",selected.found);
 setProperty("Stage3GInterruptAvailableCount",selected.count,32);
 setProperty("Stage3GInterruptValidIndices",selected.validIndices,32);
 if(!selected.found)return fail("IRQ-no-message-interrupt");
 setProperty("Stage3GInterruptIndex",selected.index,32);
 setProperty("Stage3BInterruptType",unsigned(selected.type),32);
 recordStartup(provider,"IRQ-event-sources","enter");
 irq=IOFilterInterruptEventSource::filterInterruptEventSource(this,interrupt,filter,device,int(selected.index));
 timer=IOTimerEventSource::timerEventSource(this,tick);
 // Preserve P1's allocation order and short-circuit event-source addition.
 if(!irq)return fail("IRQ-source-allocation");
 if(!(irqAdded=loop->addEventSource(irq)==kIOReturnSuccess))return fail("IRQ-source-add");
 if(!timer)return fail("timer-source-allocation");
 if(!(timerAdded=loop->addEventSource(timer)==kIOReturnSuccess))return fail("timer-source-add");
 irq->disable();reportLinkInactive("inactive");publish();
 recordStartup(provider,"interface-attach","enter");
 const bool attachOK=attachInterface(&interface,false);
 startup.attachReturned=true;startup.attached=attachOK;
 if(!attachOK)return fail("interface-attach");
 setProperty("Stage3BInterfaceAttached",true);
 // Seal before asynchronous publication can cause an enable/activate callback.
 // The snapshot proves attach and readiness, not completion of publication.
 recordStartup(provider,"start-ready","pass");startup.sealed=true;
 interface->registerService();registerService();
 IOLog("RVT-GU25-P11 start-return-success\n");return true;
}
bool RealtekVTDNextEthernet::configureInterface(IONetworkInterface *net){
 const bool superOK=IOEthernetController::configureInterface(net);
 startup.configureReturned=true;startup.configureSuperOK=superOK;
 if(!superOK){recordStartup(getProvider(),"interface-configure-super","fail");return false;}
 auto data=net->getNetworkData(kIONetworkStatsKey);
 stats=data?static_cast<IONetworkStats*>(const_cast<void*>(data->getBuffer())):nullptr;
 startup.statsReady=stats!=nullptr;recordStartup(getProvider(),"interface-configure-stats",stats?"pass":"fail");return stats;
}
IOReturn RealtekVTDNextEthernet::enable(IONetworkInterface *){
 IOLockLock(lock);power.request(true);const bool asleep=power.suspended;IOLockUnlock(lock);
 const IOReturn status=asleep?kIOReturnSuccess:activate();
 IOLockLock(lock);power.result(uint32_t(status));publish();IOLockUnlock(lock);return status;
}
void RealtekVTDNextEthernet::refreshMAC34(unsigned mtu){
 if(filterMACVersion!=34)return;
 Identity id{ProbeStatus(startup.identityStatus),Chip(startup.chip),uint16_t(startup.xid),uint8_t(filterMACVersion)};
 const bool init=startup.hardwareComplete&&startup.hardwareStatus==0&&startup.programIdentityStatus==unsigned(ProbeStatus::Supported)&&startup.programChip==unsigned(Chip::RTL8168EVL)&&startup.programXID==0x2c9&&startup.programCFGMethod==0;
 auto wanted=[&](const char*name){const auto p=getProperty(name);return !p||p==kOSBooleanTrue;};
 mac34.configure(id,init,backend&&backend->dmaReady(),backend&&backend->deviceMapperPresent(),backend&&backend->native64NoIOMMUReady(),network.prepared,resources.jumboReady,configuredTXCount,backend?backend->txPoolCount():0,backend?backend->txPoolStride():0,wanted("MAC34TXChecksumEnabled"),wanted("MAC34RXChecksumEnabled"),wanted("MAC34TSO4Enabled"),wanted("MAC34TSO6Enabled"),mtu);
 network.mac34Native64Verified=mac34.nativeReady;
 mac34TXQ4.configure(mac34.nativeReady,txq4Requested);
 if(txq3){txq3->configure(mac34.nativeReady,txq3Requested);if(!txq3->enabled())txq3Parent4=txq3Parent6=0;}
 if(txq2)txq2->configure(mac34.nativeReady,txq2Requested,static_cast<IOBasicOutputQueue*>(getOutputQueue())->getDropCount());network.mac34TXAuthorized=mac34.tx.enabled()&&mtu==1500;
 if(mac34.nativeReady){network.rxChecksumProfile=RXChecksumProfile::MAC34RevisionC;network.rxChecksumRequested=network.rxChecksum6Requested=mac34.rx.enabled();}
}
void RealtekVTDNextEthernet::refreshMAC46(unsigned mtu){
 if(filterMACVersion!=46)return;
 Identity id{ProbeStatus(startup.identityStatus),Chip(startup.chip),uint16_t(startup.xid),uint8_t(filterMACVersion)};
 const bool initialized=startup.hardwareComplete&&startup.hardwareStatus==0&&startup.programIdentityStatus==unsigned(ProbeStatus::Supported)&&startup.programChip==unsigned(Chip::RTL8168H)&&startup.programXID==0x541&&startup.programCFGMethod==0&&getProperty("FirmwareApplied")==kOSBooleanTrue;
 mac46Features.configure(id,device->configRead16(UInt8(0)),device->configRead16(UInt8(2)),device->configRead8(UInt8(8)),initialized,backend&&backend->dmaReady(),backend&&backend->deviceMapperPresent(),network.prepared&&resources.ready,resources.jumboReady,configuredTXCount,backend?backend->txPoolCount():0,backend?backend->txPoolStride():0,mtu,backend&&backend->native64NoIOMMUReady());
 network.mac46MapperVerified=mac46Features.mapperReady;
 network.mac46Native64Verified=mac46Features.identityEligible&&mac46Features.hardwareInitialized&&mac46Features.resourcesReady&&mac46Features.native64Ready;
 network.mac46TXAuthorized=mac46Features.tx.enabled()&&mtu==1500;
 if(mac46Features.rx.enabled()){network.rxChecksumProfile=RXChecksumProfile::MAC46RevisionC;network.rxChecksumRequested=network.rxChecksum6Requested=true;}
}
bool RealtekVTDNextEthernet::configureMAC46Checksum(){
 refreshMAC46(mtuTransaction.mtu);
 if(!mac46Features.declarationReady&&!mac46Features.managed)return true;
 struct IO {ifnet_t net;
  enum:uint32_t{txAssistMask=IFNET_CSUM_IP|IFNET_CSUM_TCP|IFNET_CSUM_UDP,ipv6AssistMask=IFNET_CSUM_TCPIPV6|IFNET_CSUM_UDPIPV6,txCapability=IFCAP_TXCSUM,checksumCaps=IFCAP_TXCSUM|IFCAP_RXCSUM};
  bool valid(){return net!=nullptr;}uint32_t offload(){return ifnet_offload(net);}uint32_t supported(){return ifnet_capabilities_supported(net);}uint32_t enabled(){return ifnet_capabilities_enabled(net);}
  int setOffload(uint32_t v){return ifnet_set_offload(net,v);}int setSupported(uint32_t v){return ifnet_set_capabilities_supported(net,v,IFCAP_VALID);}int setEnabled(uint32_t v){return ifnet_set_capabilities_enabled(net,v,IFCAP_VALID);}
 }io{interface?interface->getIfnet():nullptr};
 const bool ok=configureTXChecksumAssist(io,mac46Features.tx.enabled(),mac46Features.txAssist,mac46Features.rx.enabled(),IO::ipv6AssistMask,mac46Features.tx.enabled(),mac46Features.rx.enabled());
 if(ok)mac46Features.managed=mac46Features.tx.enabled()||mac46Features.rx.enabled();
 else{mac46Features.tx.configure(false);mac46Features.rx.configure(false);network.rxChecksumRequested=network.rxChecksum6Requested=false;network.mac46TXAuthorized=false;}
 return ok;
}
bool RealtekVTDNextEthernet::configureMAC46TSO(unsigned mtu,bool six){
 refreshMAC46(mtu);
 const bool want=(six?mac46Features.tso6.enabled():mac46Features.tso4.enabled())&&!network.unsafeRetained;
 bool &managed=six?mac46Features.tso6Managed:mac46Features.tso4Managed;
 if(six){network.mac46TSO6Authorized=false;network.mac46TSO6Limit=mac46Features.limit;}
 else{network.mac46TSO4Authorized=false;network.mac46TSO4Limit=mac46Features.limit;}
 if(!want&&!managed)return true;
 struct IOBase {ifnet_t net=nullptr;bool six=false;
  bool valid(){return net!=nullptr;}uint32_t offload(){return ifnet_offload(net);}uint32_t supported(){return ifnet_capabilities_supported(net);}uint32_t enabled(){return ifnet_capabilities_enabled(net);}
  int setOffload(uint32_t v){return ifnet_set_offload(net,v);}int setSupported(uint32_t v){return ifnet_set_capabilities_supported(net,v,IFCAP_VALID);}int setEnabled(uint32_t v){return ifnet_set_capabilities_enabled(net,v,IFCAP_VALID);}
  int setLimit(uint32_t v){return ifnet_set_tso_mtu(net,six?AF_INET6:AF_INET,v);}int getLimit(uint32_t &v){return ifnet_get_tso_mtu(net,six?AF_INET6:AF_INET,&v);}
 };
 struct IO4:IOBase{enum:uint32_t{tsoAssist=IFNET_TSO_IPV4,tsoCapability=IFCAP_TSO4};};
 struct IO6:IOBase{enum:uint32_t{tsoAssist=IFNET_TSO_IPV6,tsoCapability=IFCAP_TSO6};};
 auto &receipt=six?mac46Features.assist6:mac46Features.assist4;bool ok=false;
 if(six){IO6 io;io.net=interface?interface->getIfnet():nullptr;io.six=true;ok=configureTSO4Assist(io,want,receipt,mac46Features.limit);}
 else{IO4 io;io.net=interface?interface->getIfnet():nullptr;ok=configureTSO4Assist(io,want,receipt,mac46Features.limit);}
 if(ok){managed=want;if(six)network.mac46TSO6Authorized=want;else network.mac46TSO4Authorized=want;}
 else{(six?mac46Features.tso6:mac46Features.tso4).configure(false);}
 return ok;
}
bool RealtekVTDNextEthernet::applyMAC46EEE(unsigned mtu){
 if(!mac46EEE.eligible)return true;
 MTUTarget target{this};const bool quiescent=target.fc1Quiescent();
 RegisterAccess access(*backend,filterMACVersion);
 return mac46EEE.apply(access,quiescent,mtu);
}
bool RealtekVTDNextEthernet::configureMAC34Checksum(){
 refreshMAC34(mtuTransaction.mtu);
 if(!mac34.nativeReady&&!mac34.txManaged&&!mac34.rxManaged)return true;
 struct IO {ifnet_t net;
  enum:uint32_t{txAssistMask=IFNET_CSUM_IP|IFNET_CSUM_TCP|IFNET_CSUM_UDP,ipv6AssistMask=IFNET_CSUM_TCPIPV6|IFNET_CSUM_UDPIPV6,txCapability=IFCAP_TXCSUM,checksumCaps=IFCAP_TXCSUM|IFCAP_RXCSUM};
  bool valid(){return net!=nullptr;}uint32_t offload(){return ifnet_offload(net);}uint32_t supported(){return ifnet_capabilities_supported(net);}uint32_t enabled(){return ifnet_capabilities_enabled(net);}
  int setOffload(uint32_t v){return ifnet_set_offload(net,v);}int setSupported(uint32_t v){return ifnet_set_capabilities_supported(net,v,IFCAP_VALID);}int setEnabled(uint32_t v){return ifnet_set_capabilities_enabled(net,v,IFCAP_VALID);}
 }io{interface?interface->getIfnet():nullptr};
 const bool ok=configureTXChecksumAssist(io,mac34.tx.enabled(),mac34.txAssist,mac34.rx.enabled(),IO::ipv6AssistMask,mac34.tx.enabled(),mac34.rx.enabled());
 if(ok){mac34.txManaged=mac34.tx.enabled();mac34.rxManaged=mac34.rx.enabled();}return ok;
}
bool RealtekVTDNextEthernet::configureMAC34TSO(unsigned mtu,bool six){
 refreshMAC34(mtu);const bool want=(six?mac34.tso6.enabled():mac34.tso4.enabled())&&!network.unsafeRetained;
 bool &managed=six?mac34.tso6Managed:mac34.tso4Managed;
 if(!want&&!managed)return true;
 struct IOBase {ifnet_t net=nullptr;bool six=false;
  bool valid(){return net!=nullptr;}uint32_t offload(){return ifnet_offload(net);}uint32_t supported(){return ifnet_capabilities_supported(net);}uint32_t enabled(){return ifnet_capabilities_enabled(net);}
  int setOffload(uint32_t v){return ifnet_set_offload(net,v);}int setSupported(uint32_t v){return ifnet_set_capabilities_supported(net,v,IFCAP_VALID);}int setEnabled(uint32_t v){return ifnet_set_capabilities_enabled(net,v,IFCAP_VALID);}
  int setLimit(uint32_t v){return ifnet_set_tso_mtu(net,six?AF_INET6:AF_INET,v);}int getLimit(uint32_t &v){return ifnet_get_tso_mtu(net,six?AF_INET6:AF_INET,&v);}
 };
 struct IO4:IOBase{enum:uint32_t{tsoAssist=IFNET_TSO_IPV4,tsoCapability=IFCAP_TSO4};};
 struct IO6:IOBase{enum:uint32_t{tsoAssist=IFNET_TSO_IPV6,tsoCapability=IFCAP_TSO6};};
 auto &receipt=six?mac34.assist6:mac34.assist4;bool ok=false;
 if(six){IO6 io;io.net=interface?interface->getIfnet():nullptr;io.six=true;ok=configureTSO4Assist(io,want,receipt,9000);}
 else{IO4 io;io.net=interface?interface->getIfnet():nullptr;ok=configureTSO4Assist(io,want,receipt,9000);}
 if(ok)managed=want;else (six?mac34.tso6:mac34.tso4).configure(false);return ok;
}
void RealtekVTDNextEthernet::refreshMAC44Checksum(){
 // No MMIO or mapper allocation. Eligibility uses the existing checked identity,
 // PCI config identity and the backend's actual completed DMA selection.
 Identity id{ProbeStatus(startup.identityStatus),Chip(startup.chip),uint16_t(startup.xid),uint8_t(filterMACVersion)};
 network.mac44Native64ModeVerified=false;
 if(filterMACVersion!=44){mac44Checksum.configure(id,0,0,0,0,0,false,false,false,false);return;}
 mac44Checksum.configure(id,device->configRead16(UInt8(0)),device->configRead16(UInt8(2)),device->configRead8(UInt8(8)),
  device->configRead16(UInt8(0x2c)),device->configRead16(UInt8(0x2e)),getProperty("MAC44TXChecksumEnabled")==kOSBooleanTrue,
  startup.hardwareComplete&&startup.hardwareStatus==0&&startup.programIdentityStatus==unsigned(ProbeStatus::Supported)&&
   startup.programChip==unsigned(Chip::RTL8411B)&&startup.programXID==0x5c8&&startup.programCFGMethod==0,backend->dmaReady(),backend->deviceMapperPresent(),backend->native64NoIOMMUReady());
 network.mac44Native64ModeVerified=mac44Checksum.identityEligible&&mac44Checksum.hardwareInitialized&&mac44Checksum.native64Ready;
}
void RealtekVTDNextEthernet::refreshMAC44FlowControl(){
 if(filterMACVersion!=44)return; // GU25 and all other policies are untouched.
 const OSObject *option=getProperty("MAC44FlowControlEnabled");
 const auto request=!option?FC1Request::Default:option==kOSBooleanTrue?FC1Request::On:
                    option==kOSBooleanFalse?FC1Request::Off:FC1Request::Invalid;
 // refreshMAC44Checksum always computes identity/init/mapper eligibility even
 // when its independent TX checksum preference is OFF. No extra hardware I/O.
 flowControl.configureMAC44(mac44Checksum.identityEligible,mac44Checksum.hardwareInitialized,
                            mac44Checksum.dmaReady,request);
}
void RealtekVTDNextEthernet::refreshMAC44RXChecksum(){
 if(filterMACVersion!=44){mac44RX.configure(false,false,false,false);return;}
 mac44RX.configure(getProperty("MAC44RXChecksumEnabled")==kOSBooleanTrue,mac44Checksum.identityEligible,
                    mac44Checksum.hardwareInitialized,mac44Checksum.mapperReady,mac44Checksum.native64Ready);
 if(mac44RX.declared()){
  network.rxChecksumProfile=RXChecksumProfile::MAC44RevisionC;
  network.rxChecksumRequested=network.rxChecksum6Requested=true;
 }
}
bool RealtekVTDNextEthernet::configureMAC44RXAssist(){
 if(filterMACVersion!=44||(!mac44RX.declared()&&!mac44RX.ifnetManaged))return true;
 struct IO {
  ifnet_t net;enum:uint32_t {rxCapability=IFCAP_RXCSUM};
  bool valid(){return net!=nullptr;}uint32_t offload(){return ifnet_offload(net);}
  uint32_t supported(){return ifnet_capabilities_supported(net);}uint32_t enabled(){return ifnet_capabilities_enabled(net);}
  int setSupported(uint32_t v){return ifnet_set_capabilities_supported(net,v,IFCAP_RXCSUM);}
  int setEnabled(uint32_t v){return ifnet_set_capabilities_enabled(net,v,IFCAP_RXCSUM);}
 }io{interface?interface->getIfnet():nullptr};
 const bool want=mac44RX.declared();
 const bool ok=realtek_next::configureMAC44RXAssist(io,want,mac44RX.assist);
 if(ok){mac44RX.ifnetManaged=want;return true;}
 ++mac44RX.ifnetFailures;network.rxChecksumRequested=network.rxChecksum6Requested=false;
 // RX failure never disables the independent TX checksum policy/masks.
 // If rollback was proved, one bounded RX-OFF transaction may permit TX+SW RX.
 if(!mac44RX.assist.rollbackOK)return false;
 const bool disabled=realtek_next::configureMAC44RXAssist(io,false,mac44RX.disableFallback);
 if(disabled)mac44RX.ifnetManaged=false;return disabled;
}
bool RealtekVTDNextEthernet::configureChecksumAssist(){
 if(filterMACVersion==46)return configureMAC46Checksum();
 if(filterMACVersion==34)return configureMAC34Checksum();
 if(mac44Checksum.enabled()){
  struct IO {
   ifnet_t net;
   enum:uint32_t {txAssistMask=IFNET_CSUM_IP|IFNET_CSUM_TCP|IFNET_CSUM_UDP,ipv6AssistMask=IFNET_CSUM_TCPIPV6|IFNET_CSUM_UDPIPV6,txCapability=IFCAP_TXCSUM,checksumCaps=IFCAP_TXCSUM};
   bool valid(){return net!=nullptr;}uint32_t offload(){return ifnet_offload(net);}
   uint32_t supported(){return ifnet_capabilities_supported(net);}uint32_t enabled(){return ifnet_capabilities_enabled(net);}
   int setOffload(uint32_t v){return ifnet_set_offload(net,v);}
   int setSupported(uint32_t v){return ifnet_set_capabilities_supported(net,v,IFCAP_VALID);}
   int setEnabled(uint32_t v){return ifnet_set_capabilities_enabled(net,v,IFCAP_VALID);}
  }io{interface?interface->getIfnet():nullptr};
  TXChecksumAssistReceipt receipt;const bool ok=configureTXChecksumAssist(io,true,receipt,false,IO::ipv6AssistMask,true,false);
  IOLockLock(lock);mac44Checksum.assist=receipt;if(!ok)mac44Checksum.support.configure(false);IOLockUnlock(lock);return ok;
 }

 if(filterMACVersion!=42||startup.xid!=0x509||startup.cfgMethod!=25)return true;
 // Called while the output consumer/engines are stopped, outside the controller
 // lock: the BSD KPIs may acquire their own interface locks.
 struct IO {
  ifnet_t net;
  enum:uint32_t {txAssistMask=IFNET_CSUM_IP|IFNET_CSUM_TCP|IFNET_CSUM_UDP,ipv6AssistMask=IFNET_CSUM_TCPIPV6|IFNET_CSUM_UDPIPV6,txCapability=IFCAP_TXCSUM,checksumCaps=IFCAP_TXCSUM|IFCAP_RXCSUM};
  bool valid(){return net!=nullptr;}uint32_t offload(){return ifnet_offload(net);}
  uint32_t supported(){return ifnet_capabilities_supported(net);}uint32_t enabled(){return ifnet_capabilities_enabled(net);}
  int setOffload(uint32_t flags){return ifnet_set_offload(net,flags);}
  int setSupported(uint32_t flags){return ifnet_set_capabilities_supported(net,flags,IFCAP_VALID);}
  int setEnabled(uint32_t flags){return ifnet_set_capabilities_enabled(net,flags,IFCAP_VALID);}
 }io{interface?interface->getIfnet():nullptr};
 TXChecksumAssistReceipt receipt;const bool ok=configureTXChecksumAssist(io,txChecksumPolicy.enabled(),receipt,rxChecksumPolicy.enabled(),IO::ipv6AssistMask,csum6Policy.enabled(),csum6Policy.enabled());
 IOLockLock(lock);txChecksumAssist=receipt;if(!ok){txChecksumPolicy.configure(false);rxChecksumPolicy.configure(false);csum6Policy.configure(false);network.rxChecksumRequested=false;network.rxChecksum6Requested=false;network.rxChecksumProfile=RXChecksumProfile::GU25RevisionC;}IOLockUnlock(lock);return ok;
}
IOReturn RealtekVTDNextEthernet::activate(){
 if(txq4OwnershipRetained)return kIOReturnNotReady;
 if(enabled)return kIOReturnSuccess;if(!backend||!network.prepared||network.unsafeRetained)return kIOReturnNotReady;
 // A full chip reinitialization prevents inheritance of the finite probe's DMA cursors on re-enable.
 // Ordinary enable follows the validated 0.16 path; PCI D0 handling is wake-only.
 if(!initializeHardware())return kIOReturnIOError;
 if(!configureChecksumAssist())return kIOReturnIOError;
 if(!configureMAC44RXAssist())return kIOReturnIOError;
 if(!configureTSOFamilies(mtuTransaction.mtu))return kIOReturnIOError;
 IOLockLock(lock);++generation;if(txq)txq->setEpoch(generation);
 MTUTarget target{this};
 bool ok=(mtuTransaction.mtu==1500||(jumboEligible&&resources.jumboReady))&&applyMAC46EEE(mtuTransaction.mtu)&&flowControl.reapply(mtuTransaction,target);
 network.mtu=mtuTransaction.mtu;
 if(ok)ok=startNetwork(*backend,*backend,rings,network,promiscuous);
 if(ok)ok=applyReceiveFilter();
 if(ok){enabled=true;previousPHY=0xff;stalledSeconds=0;previousCompleted=network.txCompleted;irq->enable();ok=backend->write16(0x3c,kNetworkInterruptMask);}
 if(!ok){IOLog("RealtekVTDNext STAGE3B enable failed: status=%u\n",unsigned(network.status));halt();IOLockUnlock(lock);return kIOReturnIOError;}
 setProperty("Stage3BTXWatchdog",false);updateLink();publish();
 if(mac46EEE.linkEligible&&!enabled){IOLockUnlock(lock);return kIOReturnIOError;}
 timer->setTimeoutMS(1000);IOLockUnlock(lock);
 getOutputQueue()->setCapacity(configuredOutputQueueCapacity);getOutputQueue()->start();txqService();
 return kIOReturnSuccess;
}
bool RealtekVTDNextEthernet::startPreparedMTU(unsigned mtu){
 if(txq4OwnershipRetained)return false;
 network.mtu=mtu;++generation;if(txq)txq->setEpoch(generation);
 bool ok=applyMAC46EEE(mtu)&&(!mac46EEE.eligible||!mac46EEE.last.restartRequired)&&startNetwork(*backend,*backend,rings,network,promiscuous);
 if(ok)ok=applyReceiveFilter();
 if(ok){enabled=true;previousPHY=0xff;stalledSeconds=0;previousCompleted=network.txCompleted;
  irq->enable();ok=backend->write16(0x3c,kNetworkInterruptMask);}
 return ok;
}
IOReturn RealtekVTDNextEthernet::setMaxPacketSize(UInt32 size){
 if(!lock||!backend||!network.prepared)return kIOReturnNotReady;
 unsigned requested=0;
 IOLockLock(lock);
 const bool argument=packetSizeToMTU(size,resources.jumboReady?kJumboMTU:1500,requested);
 if(!argument){IOLockUnlock(lock);return kIOReturnBadArgument;}
 if(network.unsafeRetained||mtuTransaction.state==MTUState::SAFE_STOP){IOLockUnlock(lock);return kIOReturnNotReady;}
 if(requested==mtuTransaction.mtu){IOLockUnlock(lock);return kIOReturnSuccess;}
 if(requested>1500&&!jumboEligible){IOLockUnlock(lock);return kIOReturnUnsupported;}
 if(!enabled||power.suspended){
  if(network.status!=NetworkStatus::Ok){IOLockUnlock(lock);return kIOReturnNotReady;}
  IOLockUnlock(lock);if(!configureTSOFamilies(requested))return kIOReturnIOError;
  IOLockLock(lock);mtuTransaction.offline(requested);network.mtu=requested;publish();IOLockUnlock(lock);return kIOReturnSuccess;
 }
 IOLockUnlock(lock);
 // SDK: stop() joins the ungated consumer. Never hold controller lock while joining it.
 // outputPacket never waits for this workloop gate, and never calls queue stop().
 auto queue=getOutputQueue();UInt32 flushed=0;if(queue){queue->stop();flushed=static_cast<RVTTXQOutputQueue*>(queue)->flushFromDriver(1);txRingDiag.flush(flushed);}
 if(!configureTSOFamilies(requested)){IOLockLock(lock);halt();IOLockUnlock(lock);return kIOReturnIOError;}
 IOLockLock(lock);mtuQueueFlushed+=flushed;
 if(!enabled||network.unsafeRetained){IOLockUnlock(lock);return kIOReturnNotReady;}
 MTUTarget target{this};const bool accepted=mtuTransaction.change(target,requested);
 const bool running=enabled&&!network.unsafeRetained&&mtuTransaction.state!=MTUState::SAFE_STOP;
 if(running){updateLink();if(enabled)timer->setTimeoutMS(1000);}
 const bool resume=enabled&&!network.unsafeRetained&&mtuTransaction.state!=MTUState::SAFE_STOP;
 const unsigned finalMTU=network.mtu;publish();IOLockUnlock(lock);
 if(!configureTSOFamilies(finalMTU)){IOLockLock(lock);halt();IOLockUnlock(lock);return kIOReturnIOError;}
 if(resume&&queue){queue->start();txqService();}
 return accepted&&resume?kIOReturnSuccess:kIOReturnIOError;
}
IOReturn RealtekVTDNextEthernet::registerWithPolicyMaker(IOService *policyMaker){
 // IONetworkController already performs PMinit/joinPMtree and calls this hook before attach.
 static IOPMPowerState states[2]={
  {1,0,0,0,0,0,0,0,0,0,0,0},
  {1,kIOPMDeviceUsable|kIOPMPowerOn,kIOPMPowerOn,kIOPMPowerOn,0,0,0,0,0,0,0,0}
 };
 IOReturn status=policyMaker->registerPowerDriver(this,states,2);
 if(status==kIOReturnSuccess)status=policyMaker->changePowerStateTo(1);
 powerRegistered=status==kIOReturnSuccess;setProperty("Stage4APowerRegistered",powerRegistered);
 return status;
}
IOReturn RealtekVTDNextEthernet::setPowerState(unsigned long state,IOService *){
 if(state>1||!lock||!backend||!getCommandGate())return kIOPMAckImplied;
 getCommandGate()->runAction(powerAction,reinterpret_cast<void*>(uintptr_t(state)));
 return kIOPMAckImplied;
}
IOReturn RealtekVTDNextEthernet::powerAction(OSObject *owner,void *state,void *,void *,void *){
 return static_cast<RealtekVTDNextEthernet*>(owner)->applyPower(uintptr_t(state));
}
IOReturn RealtekVTDNextEthernet::applyPower(unsigned long state){
 if(!state){
  // Quiesce the ungated output consumer before taking the same lock as outputPacket/IRQ.
  if(getOutputQueue()){getOutputQueue()->stop();const UInt32 flushed=static_cast<RVTTXQOutputQueue*>(getOutputQueue())->flushFromDriver(2);txRingDiag.flush(flushed);}
  IOLockLock(lock);
  if(power.suspend())halt();
  const bool safe=!network.unsafeRetained;
  power.result(safe?0:uint32_t(kIOReturnIOError));publish();IOLockUnlock(lock);
  return safe?kIOReturnSuccess:kIOReturnIOError;
 }
 IOLockLock(lock);const bool waking=power.resume();const bool wanted=power.shouldActivate();IOLockUnlock(lock);
 if(!waking)return kIOReturnSuccess;
 // Parent PCI power is restored before this child transition. Reverify D0 and BME-off.
 IOReturn status=kIOReturnSuccess;
 if(!backend->ensureD0())status=kIOReturnIOError;
 else if(wanted)status=activate();
 IOLockLock(lock);power.result(uint32_t(status));publish();IOLockUnlock(lock);
 return status;
}
void RealtekVTDNextEthernet::halt(){
 mac34TXQ4.markCancelled(); // records/originals remain live until core cleanup, or retained on unsafe stop
 if(txq3&&txq3->enabled())txq3->invalidate(generation,txq3->now(),1);txq3Parent4=txq3Parent6=0;
 if(filterMACVersion==34){mac34.tso4.configure(false);mac34.tso6.configure(false);network.mac34TXAuthorized=false;}
 if(filterMACVersion==44){mac44TSO4.hardwarePolicy.configure(false);network.mac44TSO4Authorized=false;mac44TSO6.hardwarePolicy.configure(false);network.mac44TSO6Authorized=false;}
 if(filterMACVersion==46){mac46Features.tso4.configure(false);mac46Features.tso6.configure(false);network.mac46TSO4Authorized=network.mac46TSO6Authorized=false;}
 if(tso4Software.active())tso4Software.cancelled=true;
 if(tso6Software.active())tso6Software.cancelled=true;
 txRingDiag.cancel();enabled=false;++generation;if(txq)txq->setEpoch(generation);if(irq)irq->disable();if(timer)timer->cancelTimeout();
 if(backend&&!stopNetwork(*backend,*backend,rings,network,*this,backend)&&!quarantined){
  // Keep the owner, provider and mappings alive if hardware DMA cannot be disabled.
  quarantined=true;if(!stopSafetyOwnerPinned)retain();if(network.stopSafety.latched)backend->retainStopSafety();IOLog("RealtekVTDNext: stop unconfirmed; retaining owner, provider and DMA resources\n");
 }
 retainTXQ4Ownership();
 if(!network.unsafeRetained){finishTSO4();finishTSO6();}
 multicast.invalidate();reportLinkInactive("inactive");publish();
}
IOReturn RealtekVTDNextEthernet::disable(IONetworkInterface *){
 IOLockLock(lock);power.request(false);IOLockUnlock(lock);
 if(getOutputQueue()){getOutputQueue()->stop();const UInt32 flushed=static_cast<RVTTXQOutputQueue*>(getOutputQueue())->flushFromDriver(2);txRingDiag.flush(flushed);}
 IOLockLock(lock);halt();IOLockUnlock(lock);return network.unsafeRetained?kIOReturnIOError:kIOReturnSuccess;
}
void RealtekVTDNextEthernet::refreshMAC44TSO4(){
 if(filterMACVersion!=44){mac44TSO4.reset();return;}
 mac44TSO4.configure(getProperty("MAC44TSO4Enabled")==kOSBooleanTrue,
  mac44Checksum.identityEligible,mac44Checksum.hardwareInitialized,mac44Checksum.mapperReady,
  mac44Checksum.enabled(),network.prepared,resources.jumboReady,configuredTXCount,
  backend?backend->txPoolCount():0,backend?backend->txPoolStride():0,mac44Checksum.native64Ready);
}
bool RealtekVTDNextEthernet::configureMAC44TSO4(unsigned mtu){
 refreshMAC44TSO4();const bool wanted=mac44TSO4.ready()&&mtu==1500&&!network.unsafeRetained;
 network.mac44TSO4Authorized=false;network.mac44TSO4Limit=mac44TSO4.limit;
 // OFF/invalid/negative identities preserve pre-feature calls; remove only a
 // capability installed by this policy on an earlier lifecycle transition.
 if(!wanted&&!mac44TSO4.managed)return true;
 struct IO {
  ifnet_t net;enum:uint32_t{tsoAssist=IFNET_TSO_IPV4,tsoCapability=IFCAP_TSO4};
  bool valid(){return net!=nullptr;}uint32_t offload(){return ifnet_offload(net);}uint32_t supported(){return ifnet_capabilities_supported(net);}uint32_t enabled(){return ifnet_capabilities_enabled(net);}
  int setOffload(uint32_t v){return ifnet_set_offload(net,v);}int setSupported(uint32_t v){return ifnet_set_capabilities_supported(net,v,IFCAP_VALID);}int setEnabled(uint32_t v){return ifnet_set_capabilities_enabled(net,v,IFCAP_VALID);}
  int setLimit(uint32_t n){return ifnet_set_tso_mtu(net,AF_INET,n);}int getLimit(uint32_t &n){return ifnet_get_tso_mtu(net,AF_INET,&n);}
 }io{interface?interface->getIfnet():nullptr};
 TSO4AssistReceipt receipt;const bool ok=configureTSO4Assist(io,wanted,receipt,mac44TSO4.limit);
 IOLockLock(lock);mac44TSO4.assist=receipt;mac44TSO4.hardwarePolicy.configure(ok&&wanted);
 if(ok)mac44TSO4.managed=wanted;network.mac44TSO4Authorized=ok&&wanted;IOLockUnlock(lock);return ok;
}
bool RealtekVTDNextEthernet::configureTSO4(unsigned mtu){
 if(filterMACVersion==34)return configureMAC34TSO(mtu,false);
 if(filterMACVersion==46)return configureMAC46TSO(mtu,false);
 if(filterMACVersion==44)return configureMAC44TSO4(mtu);
 const bool identity=filterMACVersion==42&&startup.xid==0x509&&startup.cfgMethod==25;
 if(!identity){tso4Policy.configure(false);return true;}
 auto option=getProperty("GU25TSO4Enabled");const bool requested=!option||option==kOSBooleanTrue;
 const bool wanted=requested&&txChecksumPolicy.enabled()&&resources.jumboReady&&backend&&backend->txPoolCount()==257&&backend->txPoolStride()==16384&&configuredTXCount==256&&mtu==1500&&!network.unsafeRetained;
 struct IO {
  ifnet_t net;enum:uint32_t{tsoAssist=IFNET_TSO_IPV4,tsoCapability=IFCAP_TSO4};
  bool valid(){return net!=nullptr;}uint32_t offload(){return ifnet_offload(net);}uint32_t supported(){return ifnet_capabilities_supported(net);}uint32_t enabled(){return ifnet_capabilities_enabled(net);}
  int setOffload(uint32_t v){return ifnet_set_offload(net,v);}int setSupported(uint32_t v){return ifnet_set_capabilities_supported(net,v,IFCAP_VALID);}int setEnabled(uint32_t v){return ifnet_set_capabilities_enabled(net,v,IFCAP_VALID);}
  int setLimit(uint32_t n){return ifnet_set_tso_mtu(net,AF_INET,n);}int getLimit(uint32_t&n){return ifnet_get_tso_mtu(net,AF_INET,&n);}
 }io{interface?interface->getIfnet():nullptr};
 TSO4AssistReceipt receipt;const bool ok=configureTSO4Assist(io,wanted,receipt);
 IOLockLock(lock);tso4Assist=receipt;tso4Policy.configure(ok&&wanted);IOLockUnlock(lock);return ok;
}
bool RealtekVTDNextEthernet::configureTSOFamilies(unsigned mtu){return configureTSO4(mtu)&&configureTSO6(mtu);}
void RealtekVTDNextEthernet::refreshMAC44TSO6(){
 if(filterMACVersion!=44){mac44TSO6.reset();return;}
 mac44TSO6.configure(getProperty("MAC44TSO6Enabled")==kOSBooleanTrue,
  mac44Checksum.identityEligible,mac44Checksum.hardwareInitialized,mac44Checksum.mapperReady,
  mac44Checksum.enabled(),network.prepared,resources.jumboReady,configuredTXCount,
  backend?backend->txPoolCount():0,backend?backend->txPoolStride():0,mac44Checksum.native64Ready);
}
bool RealtekVTDNextEthernet::configureMAC44TSO6(unsigned mtu){
 refreshMAC44TSO6();const bool wanted=mac44TSO6.ready()&&mtu==1500&&!network.unsafeRetained;
 network.mac44TSO6Authorized=false;network.mac44TSO6Limit=mac44TSO6.limit;mac44TSO6.disableFallback={};
 if(!wanted&&!mac44TSO6.managed)return true;
 struct IO {
  ifnet_t net;enum:uint32_t{tsoAssist=IFNET_TSO_IPV6,tsoCapability=IFCAP_TSO6};
  bool valid(){return net!=nullptr;}uint32_t offload(){return ifnet_offload(net);}uint32_t supported(){return ifnet_capabilities_supported(net);}uint32_t enabled(){return ifnet_capabilities_enabled(net);}
  int setOffload(uint32_t v){return ifnet_set_offload(net,v);}int setSupported(uint32_t v){return ifnet_set_capabilities_supported(net,v,IFCAP_VALID);}int setEnabled(uint32_t v){return ifnet_set_capabilities_enabled(net,v,IFCAP_VALID);}
  int setLimit(uint32_t n){return ifnet_set_tso_mtu(net,AF_INET6,n);}int getLimit(uint32_t &n){return ifnet_get_tso_mtu(net,AF_INET6,&n);}
 }io{interface?interface->getIfnet():nullptr};
 TSO4AssistReceipt receipt;const bool ok=configureTSO4Assist(io,wanted,receipt,mac44TSO6.limit);
 IOLockLock(lock);mac44TSO6.assist=receipt;mac44TSO6.hardwarePolicy.configure(ok&&wanted);
 if(ok)mac44TSO6.managed=wanted;network.mac44TSO6Authorized=ok&&wanted;IOLockUnlock(lock);
 if(ok)return true;
 ++mac44TSO6.ifnetFailures;
 // Optional family only: a proved rollback plus a bounded verified OFF
 // transaction preserves TSO4/checksum. A failed/unproved rollback is fatal.
 if(!receipt.rollbackOK)return false;
 const bool disabled=configureTSO4Assist(io,false,mac44TSO6.disableFallback,mac44TSO6.limit);
 if(disabled)mac44TSO6.managed=false;return disabled;
}
bool RealtekVTDNextEthernet::configureTSO6(unsigned mtu){
 if(filterMACVersion==34)return configureMAC34TSO(mtu,true);
 if(filterMACVersion==46)return configureMAC46TSO(mtu,true);
 if(filterMACVersion==44)return configureMAC44TSO6(mtu);
 const bool identity=filterMACVersion==42&&startup.xid==0x509&&startup.cfgMethod==25;
 if(!identity){tso6Policy.configure(false);return true;}
 auto option=getProperty("GU25TSO6Enabled");const bool requested=!option||option==kOSBooleanTrue;
 const bool wanted=requested&&csum6Policy.enabled()&&resources.jumboReady&&backend&&backend->txPoolCount()==257&&backend->txPoolStride()==16384&&configuredTXCount==256&&mtu==1500&&!network.unsafeRetained;
 struct IO {
  ifnet_t net;enum:uint32_t{tsoAssist=IFNET_TSO_IPV6,tsoCapability=IFCAP_TSO6};
  bool valid(){return net!=nullptr;}uint32_t offload(){return ifnet_offload(net);}uint32_t supported(){return ifnet_capabilities_supported(net);}uint32_t enabled(){return ifnet_capabilities_enabled(net);}
  int setOffload(uint32_t v){return ifnet_set_offload(net,v);}int setSupported(uint32_t v){return ifnet_set_capabilities_supported(net,v,IFCAP_VALID);}int setEnabled(uint32_t v){return ifnet_set_capabilities_enabled(net,v,IFCAP_VALID);}
  int setLimit(uint32_t n){return ifnet_set_tso_mtu(net,AF_INET6,n);}int getLimit(uint32_t&n){return ifnet_get_tso_mtu(net,AF_INET6,&n);}
 }io{interface?interface->getIfnet():nullptr};
 TSO4AssistReceipt receipt;
 // An already-OFF family needs no setters. Inspect all three flags, so a stale
 // ifnet capability can never survive this fast path. Enabled families always
 // execute the full limit/readback/rollback transaction before consumer start.
 if(!wanted&&io.valid()&&!(io.offload()&IFNET_TSO_IPV6)&&!(io.supported()&IFCAP_TSO6)&&!(io.enabled()&IFCAP_TSO6)){
  receipt.configured=true;receipt.afterAssist=io.offload();receipt.afterSupported=io.supported();receipt.afterEnabled=io.enabled();
  IOLockLock(lock);tso6Assist=receipt;tso6Policy.configure(false);IOLockUnlock(lock);return true;
 }
 const bool ok=configureTSO4Assist(io,wanted,receipt);
 IOLockLock(lock);tso6Assist=receipt;tso6Policy.configure(ok&&wanted);IOLockUnlock(lock);return ok;
}
void RealtekVTDNextEthernet::finishTSO6(){
 if(!tso6Software.active()||tso6Software.outstanding||network.unsafeRetained||(!tso6Software.cancelled&&tso6Software.remaining()))return;
 noteTXQ(TXQObservation::ParentEnd,6);
 void *original=tso6Software.original;const bool owned=tso6Software.owned,cancel=tso6Software.cancelled;DMABuffer aggregate=tso6Software.aggregate;
 void *cpu=tso6Software.cpuAggregate;const size_t cpuBytes=tso6Software.cpuBytes;
 if(cancel||!owned)noteTXQ3(6,1002);
 tso6Software={};noteTXQ3(6,TXQ3DrainObservation::StateCleared);if(cpu)IOFree(cpu,cpuBytes);else{backend->unmap(aggregate);backend->complete(aggregate);backend->release(aggregate);}
 if(owned){if(cancel)++tso6Stats.cancelled;else ++tso6Stats.softwareCompleted;freePacket(static_cast<mbuf_t>(original));}
 noteTXQ3(6,TXQ3DrainObservation::CleanupDone);txq3Parent6=0;
}
bool RealtekVTDNextEthernet::pumpTSO6(){
 TXQScope batchElapsed(txq,TXQObservation::Batch);txqPump6=TXQObservation::Unknown;
 for(unsigned budget=0;budget<kTSO6PumpBudget&&enabled&&tso6Software.remaining();++budget){
  if(network.status!=NetworkStatus::Ok)return true;if(network.used>=network.txCount){txqPump6=TXQObservation::Capacity;return false;}
  TSO6CopyContext context{&tso6Software.plan,tso6Software.next};DMABuffer segment;
  const auto mapped=txqPrepare(txq,[&](){return backend->snapshotTX(segment,tso6Software.plan.segmentBytes(tso6Software.next),copyTSO6Segment,&context);});
  if(mapped==DMAStatus::PoolFull){txqPump6=TXQObservation::Pool;return false;}
  if(mapped!=DMAStatus::Ok){++tso6Stats.requestErrors;tso6Stats.vlanFailure.note(mac44Checksum.identityEligible&&mac44Checksum.hardwareInitialized,VlanTSOFailure::PumpSnapshot,tso6Software.plan.frameBytes(),tso6Software.plan.mss(),network.mtu,generation,unsigned(mapped),0,VlanTSOFailure::Shape{},0,0,-1,tso6Software.owned,tso6Software.next,tso6Software.plan.segments());if(tso6Software.owned)++tso6Stats.partialFailures;return true;}
  const auto status=submitNetwork(*backend,*backend,rings,network,segment,&tso6Software);const bool consumed=!segment.opaque;
  if(consumed){tso6Software.owned=true;++tso6Software.next;++tso6Software.outstanding;++tso6Stats.softwareSegments;}
  else{backend->unmap(segment);backend->complete(segment);backend->release(segment);}
  if(status==NetworkStatus::Full&&!consumed){txqPump6=TXQObservation::fullReason(network.txFullReason);return false;}
  if(status!=NetworkStatus::Ok){++tso6Stats.requestErrors;tso6Stats.vlanFailure.note(mac44Checksum.identityEligible&&mac44Checksum.hardwareInitialized,VlanTSOFailure::PumpSubmit,tso6Software.plan.frameBytes(),tso6Software.plan.mss(),network.mtu,generation,unsigned(status),0,VlanTSOFailure::Shape{},0,0,-1,consumed,tso6Software.next,tso6Software.plan.segments());if(tso6Software.owned)++tso6Stats.partialFailures;return true;}
 }
 if(tso6Software.owned&&!tso6Software.remaining())noteTXQ3(6,TXQ3DrainObservation::AllSubmitted);
 return false;
}
// SINGLE1: already finalized single-segment bytes use the ordinary mbuf cookie.
// No software parent is created; the original remains owned until normal reclaim.
// Caller supplies one private pool lease. Never return Retry after OWN transfer.
UInt32 RealtekVTDNextEthernet::submitSingleTSO(mbuf_t packet,DMABuffer &segment,unsigned epoch,size_t originalBytes,bool ipv6){
 TXQScope initialLockWait(txq,TXQObservation::LockWait);IOLockLock(lock);initialLockWait.finish();
 const bool same=enabled&&generation==epoch;
 const bool pending=tso4Software.active()||tso6Software.active();
 const auto status=same&&!pending?submitNetwork(*backend,*backend,rings,network,segment,packet):NetworkStatus::NotReady;
 const bool consumed=!segment.opaque;
 const bool retry=!consumed&&same&&(pending||status==NetworkStatus::Full);
 if(consumed){if(ipv6)++single1Submitted6;else ++single1Submitted4;++audit.accepted;audit.acceptedBytes+=originalBytes;
  if(status!=NetworkStatus::Ok){if(ipv6)++tso6Stats.requestErrors;else ++tso4Stats.requestErrors;(ipv6?tso6Stats.vlanFailure:tso4Stats.vlanFailure).note(mac44Checksum.identityEligible&&mac44Checksum.hardwareInitialized,VlanTSOFailure::SinglePostOwn,originalBytes,0,network.mtu,epoch,unsigned(status),0,VlanTSOFailure::Shape{},0,0,-1,consumed);captureWatchdog("single1-submit");halt();}}
 else if(retry){noteRetry(1,ipv6?6:4,pending?TXQObservation::ParentPending:TXQObservation::fullReason(network.txFullReason));if(ipv6)++tso6Stats.retries;else ++tso4Stats.retries;}
 else if(same){if(ipv6)++tso6Stats.requestErrors;else ++tso4Stats.requestErrors;(ipv6?tso6Stats.vlanFailure:tso4Stats.vlanFailure).note(mac44Checksum.identityEligible&&mac44Checksum.hardwareInitialized,VlanTSOFailure::SingleSubmit,originalBytes,0,network.mtu,epoch,unsigned(status),0,VlanTSOFailure::Shape{},0,0,-1,consumed);}
 IOLockUnlock(lock);
 if(consumed)return kIOReturnOutputSuccess;
 backend->unmap(segment);backend->complete(segment);backend->release(segment);
 if(retry)return kIOReturnOutputStall;
 freePacket(packet);return kIOReturnOutputDropped;
}
UInt32 RealtekVTDNextEthernet::outputTSO6(mbuf_t packet,uint32_t flags,unsigned mss){
 TXQScope initialLockWait(txq,TXQObservation::LockWait);IOLockLock(lock);initialLockWait.finish();++tso6Stats.requests;
 if(tso4Software.active()||tso6Software.active()){++tso6Stats.retries;noteRetry(2,6,TXQObservation::ParentPending);IOLockUnlock(lock);return kIOReturnOutputStall;}
 const bool active=enabled;const unsigned epoch=generation,mtu=network.mtu;const bool mac44=mac44TSO6.ready(),mac46=mac46Features.tso6.enabled(),soft34=mac34.tso6.enabled(),hardware=!mac34.nativeReady&&(tso6Policy.enabled()||mac44TSO6.enabled()||mac46);IOLockUnlock(lock);
 if(!active){freePacket(packet);return kIOReturnOutputDropped;}
 mbuf_csum_request_flags_t checksums=0;uint32_t ignored=0;
 const size_t length=mbuf_pkthdr_len(packet);
 const bool metadata=flags==MBUF_TSO_IPV6&&mss&&mss<=65535&&mbuf_get_csum_requested(packet,&checksums,&ignored)==0&&!(checksums&~uint32_t(MBUF_CSUM_REQ_TCPIPV6));
 // Inspect only a bounded inline header before allocating the enlarged CPU copy.
 // Out-of-band VLAN metadata alone never grants this input-domain exception.
 uint8_t vlanHeader[18]{};const bool large=length>kTSO6MaxIP+64;
 const bool softwareMAC34=metadata&&soft34&&mtu==1500&&length>maxFrameBytes(kJumboMTU)&&length<=kMAC44VLAN6SoftwareIPMax+18&&mbuf_copydata(packet,0,sizeof(vlanHeader),vlanHeader)==0&&softwareTSOLink(vlanHeader,sizeof(vlanHeader),true);
 const bool software46=metadata&&mac46&&length>maxFrameBytes(resources.jumboReady?kJumboMTU:1500)&&((length<=kTSO6MaxIP+64U)||(length<=65535U+18U&&mbuf_copydata(packet,0,sizeof(vlanHeader),vlanHeader)==0&&mac46TSO6SoftwareInput(vlanHeader,sizeof(vlanHeader),length,mtu)));
 const bool softwareVLAN6=software46||softwareMAC34||(metadata&&large&&filterMACVersion==44&&mac44&&mtu==1500&&
  mac44TSO6.native64Ready&&!mac44TSO6.mapperReady&&backend&&backend->dmaReady()&&!backend->deviceMapperPresent()&&
  length<=kMAC44VLAN6SoftwareIPMax+18&&mbuf_copydata(packet,0,sizeof(vlanHeader),vlanHeader)==0&&
  tso6VLANSoftwareInput(vlanHeader,sizeof(vlanHeader),length,mtu));
 if(!metadata||length<14||(large&&!softwareVLAN6)){IOLockLock(lock);if(!metadata)++tso6Stats.metadataErrors;else {++tso6Stats.requestErrors;tso6Stats.vlanFailure.note(mac44Checksum.identityEligible&&mac44Checksum.hardwareInitialized,VlanTSOFailure::InputBounds,length,mss,mtu,epoch,0,unsigned(TSO4Error::Bounds),VlanTSOFailure::Shape{},flags,checksums);}IOLockUnlock(lock);freePacket(packet);return kIOReturnOutputDropped;}
 // RAII closes every pre-OWN exit. Only committed software state takes this
 // CPU pointer; it is never mapped or handed to a hardware descriptor.
 struct CPUInput {void *data=nullptr;size_t length=0;~CPUInput(){if(data)IOFree(data,length);}}cpu;
 DMABuffer mapped;JTXCopyEvidence copy;
 const auto mapping=txqPrepare(txq,[&](){
  if(!softwareVLAN6)return backend->mapPacket(mapped,packet,&copy);
  cpu.length=length;cpu.data=IOMalloc(length);
  if(!cpu.data)return DMAStatus::AllocationFailed;
  return mbuf_copydata(packet,0,length,cpu.data)==0?DMAStatus::Ok:DMAStatus::InvalidState;
 });
 auto discardInput=[&](){if(mapped.opaque){backend->unmap(mapped);backend->complete(mapped);backend->release(mapped);}};
 if(mapping!=DMAStatus::Ok){IOLockLock(lock);if(mapping==DMAStatus::PoolFull){++tso6Stats.retries;noteRetry(3,6,TXQObservation::Pool);}else {++tso6Stats.requestErrors;tso6Stats.vlanFailure.note(mac44Checksum.identityEligible&&mac44Checksum.hardwareInitialized,VlanTSOFailure::HardwareSnapshot,length,mss,mtu,epoch,unsigned(mapping),0,VlanTSOFailure::Shape{},flags,checksums);}IOLockUnlock(lock);if(mapping==DMAStatus::PoolFull)return kIOReturnOutputStall;freePacket(packet);return kIOReturnOutputDropped;}
 auto bytes=softwareVLAN6?static_cast<uint8_t*>(cpu.data):static_cast<uint8_t*>(backend->bytes(mapped));
 const size_t inputBytes=softwareVLAN6?length:size_t(mapped.requestedLength);
 const auto plan=TSO6Plan::parse(bytes,inputBytes,length,mss,mtu,mac46?TSO6InputProfile::MAC46Software:soft34?TSO6InputProfile::MAC34Software:softwareVLAN6?TSO6InputProfile::MAC44VLANSoftware:TSO6InputProfile::Conservative);
 if(!plan.valid()){const auto failureShape=VlanTSOFailure::inspect(bytes,inputBytes<192?inputBytes:192);discardInput();IOLockLock(lock);++tso6Stats.requestErrors;tso6Stats.vlanFailure.note(mac44Checksum.identityEligible&&mac44Checksum.hardwareInitialized,VlanTSOFailure::PlanParse,length,mss,mtu,epoch,0,unsigned(plan.error()),failureShape,flags,checksums);IOLockUnlock(lock);freePacket(packet);return kIOReturnOutputDropped;}
 if((mac46&&!mac46Features.accepts(plan,mtu))||(mac44&&!mac44TSO6.accepts(plan))||(mac34.nativeReady&&!mac34.accepts(plan,mtu))){discardInput();IOLockLock(lock);if(mac44)++mac44TSO6.aggregateRejected;++tso6Stats.requestErrors;tso6Stats.vlanFailure.note(mac44Checksum.identityEligible&&mac44Checksum.hardwareInitialized,VlanTSOFailure::AggregateLimit,length,mss,mtu,epoch,0,0,VlanTSOFailure::Shape{},flags,checksums);IOLockUnlock(lock);freePacket(packet);return kIOReturnOutputDropped;}
 const auto reason=plan.hardwareReason(mtu);const unsigned ecn=plan.ipv6ECNCodepoint();
 IOLockLock(lock);++tso6Stats.hardwareEvaluations;if(ecn<4)++tso6Stats.ecnEvaluations[ecn];if(!hardware)++tso6Stats.hardwarePolicyOff;else ++tso6Stats.hardwareReasons[unsigned(reason)];IOLockUnlock(lock);
 // Reuse the verified emitter, with a separate destination (no alias assumption).
 // The aggregate lease is no longer needed after the synchronous copy completes.
 if((single1Policy.enabled()||mac44||mac46||mac34.nativeReady)&&plan.segments()==1){
  TSO6CopyContext context{&plan,0};DMABuffer segment;
  const auto singleMapping=txqPrepare(txq,[&](){return backend->snapshotTX(segment,plan.segmentBytes(0),copyTSO6Segment,&context);});
  discardInput();
  if(singleMapping!=DMAStatus::Ok){IOLockLock(lock);if(singleMapping==DMAStatus::PoolFull){++tso6Stats.retries;noteRetry(4,6,TXQObservation::Pool);}else {++tso6Stats.requestErrors;tso6Stats.vlanFailure.note(mac44Checksum.identityEligible&&mac44Checksum.hardwareInitialized,VlanTSOFailure::SingleSnapshot,length,mss,mtu,epoch,unsigned(singleMapping),0,VlanTSOFailure::Shape{},flags,checksums);}IOLockUnlock(lock);if(singleMapping==DMAStatus::PoolFull)return kIOReturnOutputStall;freePacket(packet);return kIOReturnOutputDropped;}
  return submitSingleTSO(packet,segment,epoch,length,true);
 }
 if(mac44&&hardware&&reason==TSO6HardwareReason::Eligible&&plan.segments()>64){IOLockLock(lock);++mac44TSO6.segmentLimitFallback;IOLockUnlock(lock);}
 if(!softwareVLAN6&&hardware&&reason==TSO6HardwareReason::Eligible&&(!(mac44||mac46)||plan.segments()<=64)){
  const auto tso=TSO6HardwarePlan::bind(bytes,size_t(mapped.requestedLength),plan,mapped.segmentCount==1,mtu,mac46?TSO6HardwareProfile::MAC46DeviceMapper:mac44?TSO6HardwareProfile::MAC44DeviceMapper:TSO6HardwareProfile::GU25);
  if(tso.enabled()){
   IOLockLock(lock);const auto status=(enabled&&generation==epoch&&!tso4Software.active()&&!tso6Software.active())?submitNetwork(*backend,*backend,rings,network,mapped,packet,0,TXChecksumPlan{},TSO4HardwarePlan{},tso):NetworkStatus::NotReady;
   const bool consumed=!mapped.opaque;if(consumed){++tso6Stats.hardwareSubmitted;if(ecn<4)++tso6Stats.ecnHardwareSubmitted[ecn];if(plan.tcpHeaderBytes()>20)++tso6Stats.hardwareWithTCPOptions;if(plan.tcpPSH())++tso6Stats.hardwareWithPSH;++audit.accepted;audit.acceptedBytes+=length;if((mac44||mac46)&&status!=NetworkStatus::Ok){if(mac44)++mac44TSO6.postOwnErrors;++tso6Stats.requestErrors;tso6Stats.vlanFailure.note(mac44Checksum.identityEligible&&mac44Checksum.hardwareInitialized,VlanTSOFailure::HardwarePostOwn,length,mss,mtu,epoch,unsigned(status),0,VlanTSOFailure::Shape{},flags,checksums,-1,true);captureWatchdog("mac44-tso6-submit");halt();}}else if(status==NetworkStatus::Full){++tso6Stats.retries;noteRetry(5,6,TXQObservation::fullReason(network.txFullReason));}
   IOLockUnlock(lock);if(consumed)return kIOReturnOutputSuccess;
   discardInput();if(status==NetworkStatus::Full)return kIOReturnOutputStall;freePacket(packet);return kIOReturnOutputDropped;
  }
 }
 IOLockLock(lock);
 if(!enabled||generation!=epoch||tso4Software.active()||tso6Software.active()){
  const bool retry=enabled&&generation==epoch;discardInput();if(retry){++tso6Stats.retries;noteRetry(6,6,TXQObservation::ParentPending);}IOLockUnlock(lock);if(retry)return kIOReturnOutputStall;freePacket(packet);return kIOReturnOutputDropped;
 }
 tso6Software.original=packet;tso6Software.plan=plan;tso6Software.aggregate=mapped;mapped={};
 if(softwareVLAN6){tso6Software.cpuAggregate=cpu.data;tso6Software.cpuBytes=cpu.length;cpu.data=nullptr;}
 noteTXQ3(6,1001);
 const bool failed=pumpTSO6(),owned=tso6Software.owned;
 if(!owned){if(!failed)noteRetry(7,6,txqPump6);tso6Software.cancelled=true;finishTSO6();if(!failed)++tso6Stats.retries;IOLockUnlock(lock);if(failed){freePacket(packet);return kIOReturnOutputDropped;}return kIOReturnOutputStall;}
 ++tso6Stats.softwareAccepted;noteTXQ3(6,TXQ3DrainObservation::Accepted);noteTXQ2(2);if(plan.softwareVLANAggregate()){++mac44TSO6.vlanSoftwareAccepted;if(plan.ipBytes()>mac44TSO6.vlanSoftwareMaxIPBytes)mac44TSO6.vlanSoftwareMaxIPBytes=plan.ipBytes();}++audit.accepted;audit.acceptedBytes+=length;if(failed)halt();else retireTSO(6);IOLockUnlock(lock);return kIOReturnOutputSuccess;
}

void RealtekVTDNextEthernet::finishTSO4(){
 if(!tso4Software.active()||tso4Software.outstanding||(!tso4Software.cancelled&&tso4Software.remaining()))return;
 noteTXQ(TXQObservation::ParentEnd,4);
 void *original=tso4Software.original;const bool owned=tso4Software.owned,cancel=tso4Software.cancelled;
 if(cancel||!owned)noteTXQ3(4,1002);
 tso4Software={};noteTXQ3(4,TXQ3DrainObservation::StateCleared);if(owned){if(cancel)++tso4Stats.cancelled;else ++tso4Stats.softwareCompleted;freePacket(static_cast<mbuf_t>(original));}
 noteTXQ3(4,TXQ3DrainObservation::CleanupDone);txq3Parent4=0;
}
void RealtekVTDNextEthernet::releasePacket(void *packet){
 MAC34TXCompletion::TailRecord receipt;const auto completion=mac34TXQ4.take(packet,receipt);
 if(completion==MAC34TXCompletion::Child||completion==MAC34TXCompletion::Invalid)return;
 if(completion==MAC34TXCompletion::Tail){
  if(receipt.family==6){if(receipt.cancelled)++tso6Stats.cancelled;else ++tso6Stats.softwareCompleted;}
  else{if(receipt.cancelled)++tso4Stats.cancelled;else ++tso4Stats.softwareCompleted;}
  freePacket(static_cast<mbuf_t>(receipt.original));return;
 }
 if(packet==&tso6Software&&tso6Software.active()){if(tso6Software.outstanding==1&&!tso6Software.remaining()&&!tso6Software.cancelled)noteTXQ3(6,TXQ3DrainObservation::LastChildEntry);if(tso6Software.outstanding)--tso6Software.outstanding;finishTSO6();return;}
 if(packet==&tso4Software&&tso4Software.active()){
  if(tso4Software.outstanding==1&&!tso4Software.remaining()&&!tso4Software.cancelled)noteTXQ3(4,TXQ3DrainObservation::LastChildEntry);if(tso4Software.outstanding)--tso4Software.outstanding;finishTSO4();return;
 }
 freePacket(static_cast<mbuf_t>(packet));
}
bool RealtekVTDNextEthernet::pumpTSO4(){
 TXQScope batchElapsed(txq,TXQObservation::Batch);txqPump4=TXQObservation::Unknown;
 // Existing controller lock serializes output, reclaim, stop and bounded progress.
 // No new allocation: each segment uses one lease of the existing TXPool arena.
 for(unsigned budget=0;budget<kTSO4PumpBudget&&enabled&&tso4Software.remaining();++budget){
  if(network.status!=NetworkStatus::Ok)return true;
  if(network.used>=network.txCount){txqPump4=TXQObservation::Capacity;return false;}
  auto read=[](void*c,size_t offset,void*d,size_t n)->bool{return mbuf_copydata(static_cast<mbuf_t>(c),offset,n,d)==0;};
  TSO4CopyContext context{&tso4Software.plan,tso4Software.next,read,tso4Software.original};DMABuffer segment;
  const auto mapped=txqPrepare(txq,[&](){return backend->snapshotTX(segment,tso4Software.plan.segmentBytes(tso4Software.next),copyTSO4Segment,&context);});
  if(mapped==DMAStatus::PoolFull){txqPump4=TXQObservation::Pool;return false;}
  if(mapped!=DMAStatus::Ok){++tso4Stats.requestErrors;tso4Stats.vlanFailure.note(mac44Checksum.identityEligible&&mac44Checksum.hardwareInitialized,VlanTSOFailure::PumpSnapshot,tso4Software.plan.frameBytes(),tso4Software.plan.mss(),network.mtu,generation,unsigned(mapped),0,VlanTSOFailure::Shape{},0,0,-1,tso4Software.owned,tso4Software.next,tso4Software.plan.segments());if(tso4Software.owned)++tso4Stats.partialFailures;return true;}
  const auto status=submitNetwork(*backend,*backend,rings,network,segment,&tso4Software);
  const bool consumed=!segment.opaque;
  if(consumed){tso4Software.owned=true;++tso4Software.next;++tso4Software.outstanding;++tso4Stats.softwareSegments;}
  else{backend->unmap(segment);backend->complete(segment);backend->release(segment);}
  if(status==NetworkStatus::Full&&!consumed){txqPump4=TXQObservation::fullReason(network.txFullReason);return false;}
  if(status!=NetworkStatus::Ok){++tso4Stats.requestErrors;tso4Stats.vlanFailure.note(mac44Checksum.identityEligible&&mac44Checksum.hardwareInitialized,VlanTSOFailure::PumpSubmit,tso4Software.plan.frameBytes(),tso4Software.plan.mss(),network.mtu,generation,unsigned(status),0,VlanTSOFailure::Shape{},0,0,-1,consumed,tso4Software.next,tso4Software.plan.segments());if(tso4Software.owned)++tso4Stats.partialFailures;return true;}
 }
 if(tso4Software.owned&&!tso4Software.remaining())noteTXQ3(4,TXQ3DrainObservation::AllSubmitted);
 return false;
}
bool RealtekVTDNextEthernet::mac46N64PrecheckEligible()const{
 // identityEligible is established by mac46FeatureIdentity(): Supported H,
 // MAC46/XID541/CFG0 and PCI10ec:8168 revision15. No new PCI/MMIO read here.
 return filterMACVersion==46&&backend&&mac46Features.identityEligible&&mac46Features.hardwareInitialized&&
  mac46Features.native64Ready&&!mac46Features.mapperReady&&mac46Features.dmaReady&&
  mac46Features.resourcesReady&&mac46Features.resources&&mac46Features.tso4.enabled()&&
  backend->dmaReady()&&!backend->deviceMapperPresent()&&backend->native64NoIOMMUReady()&&
  network.prepared&&resources.ready&&network.macVersion==46&&network.mac46Native64Verified&&!network.mac46MapperVerified&&
  configuredTXCount==64&&network.txCount==64&&rings.txDescriptorCount==64&&network.tx&&
  rings.tx.prepared&&rings.tx.mapped&&rings.tx.segmentCount==1&&rings.tx.mappedLength==64U*kDescriptorSize&&
  network.producer<64&&network.consumer<64&&network.used<=64&&
  backend->txPoolCount()==65&&backend->txPoolStride()==mac46Features.stride&&
  (mac46Features.stride==kNetworkBufferSize||mac46Features.stride==kJumboTXStride)&&
  !network.unsafeRetained&&!rings.stopSafetyRetained&&!network.stopSafety.latched;
}
UInt32 RealtekVTDNextEthernet::outputTSO4(mbuf_t packet,uint32_t flags,unsigned mss){
 TXQScope initialLockWait(txq,TXQObservation::LockWait);IOLockLock(lock);initialLockWait.finish();++tso4Stats.requests;
 if(tso4Software.active()||tso6Software.active()){++tso4Stats.retries;noteRetry(8,4,TXQObservation::ParentPending);IOLockUnlock(lock);return kIOReturnOutputStall;}
 const bool active=enabled;const unsigned epoch=generation,mtu=network.mtu;const bool mac44=mac44TSO4.ready(),mac46=mac46Features.tso4.enabled(),soft34=mac34.tso4.enabled(),hardware=!mac34.nativeReady&&(tso4Policy.enabled()||mac44TSO4.enabled()||mac46);
 IOLockUnlock(lock);if(!active){freePacket(packet);return kIOReturnOutputDropped;}
 uint8_t header[kTSO4HeaderBytes]{};const size_t length=mbuf_pkthdr_len(packet),capture=length<sizeof(header)?length:sizeof(header);
 mbuf_csum_request_flags_t checksums=0;uint32_t ignored=0;
 const bool metadata=flags==MBUF_TSO_IPV4&&mss&&mss<=65535&&mbuf_get_csum_requested(packet,&checksums,&ignored)==0&&!(checksums&~uint32_t(MBUF_CSUM_REQ_IP|MBUF_CSUM_REQ_TCP));
 TSO4Plan plan;int headerCopyStatus=-1;
 if(metadata&&(headerCopyStatus=mbuf_copydata(packet,0,capture,header))==0)plan=TSO4Plan::parse(header,capture,length,mss,mtu,mac46?TSO4InputProfile::MAC46Software:soft34?TSO4InputProfile::MAC34Software:mac44?TSO4InputProfile::MAC44VLANSoftware:TSO4InputProfile::Conservative);
 if(!metadata||!plan.valid()){IOLockLock(lock);if(!metadata)++tso4Stats.metadataErrors;else {++tso4Stats.requestErrors;tso4Stats.vlanFailure.note(mac44Checksum.identityEligible&&mac44Checksum.hardwareInitialized,headerCopyStatus==0?VlanTSOFailure::PlanParse:VlanTSOFailure::HeaderCopy,length,mss,mtu,epoch,unsigned(headerCopyStatus),unsigned(plan.error()),headerCopyStatus==0?VlanTSOFailure::inspect(header,capture):VlanTSOFailure::Shape{},flags,checksums,headerCopyStatus);}IOLockUnlock(lock);freePacket(packet);return kIOReturnOutputDropped;}
 if((mac46&&!mac46Features.accepts(plan,mtu))||(mac44&&!mac44TSO4.accepts(plan))||(mac34.nativeReady&&!mac34.accepts(plan,mtu))){IOLockLock(lock);if(mac44)++mac44TSO4.aggregateRejected;++tso4Stats.requestErrors;tso4Stats.vlanFailure.note(mac44Checksum.identityEligible&&mac44Checksum.hardwareInitialized,VlanTSOFailure::AggregateLimit,length,mss,mtu,epoch,0,0,VlanTSOFailure::inspect(header,capture),flags,checksums,headerCopyStatus);IOLockUnlock(lock);freePacket(packet);return kIOReturnOutputDropped;}
 const auto hardwareReason=plan.hardwareReason(mtu);
 const bool hardwareCandidate=(!mac46||length<=maxFrameBytes(resources.jumboReady?kJumboMTU:1500))&&hardware&&hardwareReason==TSO4HardwareReason::Eligible&&(!(mac44||mac46)||plan.segments()<=kMAC44TSO4HardwareSegments);
 IOLockLock(lock);++tso4Stats.hardwareEvaluations;
 const unsigned ecnCodepoint=plan.ipv4ECNCodepoint();
 if(ecnCodepoint<4U)++tso4Stats.ecnEvaluations[ecnCodepoint];
 if(!hardware)++tso4Stats.hardwarePolicyOff;
 else ++tso4Stats.hardwareReasons[unsigned(hardwareReason)];
 if(mac44&&hardware&&hardwareReason==TSO4HardwareReason::Eligible&&plan.segments()>kMAC44TSO4HardwareSegments)++mac44TSO4.segmentLimitFallback;
 // MAC46 B: only the fully validated hardware candidate can avoid its copy.
 // The existing lock covers this advisory check; no descriptor is reserved.
 const bool observeMAC46Snapshot=hardwareCandidate&&plan.segments()>1&&mac46N64PrecheckEligible();
 if(observeMAC46Snapshot&&enabled&&generation==epoch&&network.running&&network.status==NetworkStatus::Ok&&
    !tso4Software.active()&&!tso6Software.active()&&network.used==network.txCount){
  mac46N64Precheck.targeted=true;++mac46N64Precheck.earlyFull;
  ++tso4Stats.retries;noteRetry(11,4,TXQObservation::Capacity);
  IOLockUnlock(lock);return kIOReturnOutputStall;
 }
 if(observeMAC46Snapshot)mac46N64Precheck.beginSnapshot();
 // TXPRECHECK1: the validated TSO4 hardware path needs one descriptor.
 // Stop before leasing/copying a private snapshot when capacity is already
 // exhausted. This is advisory: submitNetwork retains its final checks.
 // No OWN read, reclaim, service call, DMA operation or mbuf mutation here.
 // Reuse this existing lock; preserve software fallback and other identities.
 if(txPrecheckPolicy.enabled()&&hardware&&hardwareReason==TSO4HardwareReason::Eligible&&
    backend&&backend->dmaReady()&&!backend->deviceMapperPresent()&&
    enabled&&generation==epoch&&!tso4Software.active()&&!tso6Software.active()&&
    network.running&&network.status==NetworkStatus::Ok&&!network.unsafeRetained&&
    txDescriptorCountValid(network.txCount)&&network.txCount==rings.txDescriptorCount&&
    network.used==network.txCount){
  network.txFullReason=2;network.txFullRequested=1;
  ++txPrecheck4EarlyFull;++tso4Stats.retries;
  noteRetry(11,4,TXQObservation::Capacity);
  IOLockUnlock(lock);return kIOReturnOutputStall;
 }
 IOLockUnlock(lock);
 // A TSO-marked packet that emits exactly one frame needs no parent/child state.
 // The unchanged emitter supplies final IP/TCP checksums and canonical padding.
 if((single1Policy.enabled()||mac44||mac46||mac34.nativeReady)&&plan.segments()==1){
  auto read=[](void*c,size_t offset,void*d,size_t n)->bool{return mbuf_copydata(static_cast<mbuf_t>(c),offset,n,d)==0;};
  TSO4CopyContext context{&plan,0,read,packet};DMABuffer segment;
  const auto singleMapping=txqPrepare(txq,[&](){return backend->snapshotTX(segment,plan.segmentBytes(0),copyTSO4Segment,&context);});
  if(singleMapping!=DMAStatus::Ok){IOLockLock(lock);if(singleMapping==DMAStatus::PoolFull){++tso4Stats.retries;noteRetry(9,4,TXQObservation::Pool);}else {++tso4Stats.requestErrors;tso4Stats.vlanFailure.note(mac44Checksum.identityEligible&&mac44Checksum.hardwareInitialized,VlanTSOFailure::SingleSnapshot,length,mss,mtu,epoch,unsigned(singleMapping),0,VlanTSOFailure::Shape{},flags,checksums);}IOLockUnlock(lock);if(singleMapping==DMAStatus::PoolFull)return kIOReturnOutputStall;freePacket(packet);return kIOReturnOutputDropped;}
  return submitSingleTSO(packet,segment,epoch,length,false);
 }
 bool mac46SnapshotCopied=false;
 if(hardwareCandidate){
  DMABuffer mapped;JTXCopyEvidence copy;const auto mapping=txqPrepare(txq,[&](){return backend->mapPacket(mapped,packet,&copy);});
  mac46SnapshotCopied=copy.copied;
  if(mapping!=DMAStatus::Ok){IOLockLock(lock);if(observeMAC46Snapshot)mac46N64Precheck.snapshotResult(copy.copied,length,false);if(mapping==DMAStatus::PoolFull){++tso4Stats.retries;noteRetry(10,4,TXQObservation::Pool);}else {++tso4Stats.requestErrors;tso4Stats.vlanFailure.note(mac44Checksum.identityEligible&&mac44Checksum.hardwareInitialized,VlanTSOFailure::HardwareSnapshot,length,mss,mtu,epoch,unsigned(mapping),0,VlanTSOFailure::Shape{},flags,checksums);}IOLockUnlock(lock);if(mapping==DMAStatus::PoolFull)return kIOReturnOutputStall;freePacket(packet);return kIOReturnOutputDropped;}
  const auto tso=TSO4HardwarePlan::bind(static_cast<const uint8_t*>(backend->bytes(mapped)),size_t(mapped.requestedLength),plan,mapped.segmentCount==1,mtu,mac46?TSO4HardwareProfile::MAC46DeviceMapper:mac44?TSO4HardwareProfile::MAC44DeviceMapper:TSO4HardwareProfile::GU25);
  if(tso.enabled()){IOLockLock(lock);
  if(observeMAC46Snapshot)mac46N64Precheck.snapshotResult(copy.copied,length,true);
  const auto result=(enabled&&generation==epoch&&!tso4Software.active()&&!tso6Software.active())?submitNetwork(*backend,*backend,rings,network,mapped,packet,0,TXChecksumPlan{},tso):NetworkStatus::NotReady;
  const bool consumed=!mapped.opaque;if(consumed){++tso4Stats.hardwareSubmitted;
   if(ecnCodepoint<4U)++tso4Stats.ecnHardwareSubmitted[ecnCodepoint];
   if(plan.tcpHeaderBytes()>20)++tso4Stats.hardwareWithTCPOptions;
   if(plan.tcpPSH())++tso4Stats.hardwareWithPSH;
   ++audit.accepted;audit.acceptedBytes+=length;
   if((mac44||mac46)&&result!=NetworkStatus::Ok){if(mac44)++mac44TSO4.postOwnErrors;++tso4Stats.requestErrors;tso4Stats.vlanFailure.note(mac44Checksum.identityEligible&&mac44Checksum.hardwareInitialized,VlanTSOFailure::HardwarePostOwn,length,mss,mtu,epoch,unsigned(result),0,VlanTSOFailure::Shape{},flags,checksums,-1,true);captureWatchdog("mac44-tso4-submit");halt();}
  }else if(result==NetworkStatus::Full){if(observeMAC46Snapshot)mac46N64Precheck.finalFull(copy.copied,length);++tso4Stats.retries;noteRetry(11,4,TXQObservation::fullReason(network.txFullReason));}
  IOLockUnlock(lock);if(consumed)return kIOReturnOutputSuccess;
  backend->unmap(mapped);backend->complete(mapped);backend->release(mapped);if(result==NetworkStatus::Full)return kIOReturnOutputStall;freePacket(packet);return kIOReturnOutputDropped;}
  backend->unmap(mapped);backend->complete(mapped);backend->release(mapped);
 }
 IOLockLock(lock);
 if(observeMAC46Snapshot)mac46N64Precheck.snapshotResult(mac46SnapshotCopied,length,true);
 if(!enabled||generation!=epoch){IOLockUnlock(lock);freePacket(packet);return kIOReturnOutputDropped;}
 if(tso4Software.active()||tso6Software.active()){++tso4Stats.retries;noteRetry(12,4,TXQObservation::ParentPending);IOLockUnlock(lock);return kIOReturnOutputStall;}
 tso4Software.original=packet;tso4Software.plan=plan;noteTXQ3(4,1001);
 const bool failed=pumpTSO4(),owned=tso4Software.owned;
 if(!owned){if(!failed)noteRetry(13,4,txqPump4);noteTXQ3(4,1002);tso4Software={};if(!failed)++tso4Stats.retries;IOLockUnlock(lock);if(failed){freePacket(packet);return kIOReturnOutputDropped;}return kIOReturnOutputStall;}
 ++tso4Stats.softwareAccepted;noteTXQ3(4,TXQ3DrainObservation::Accepted);noteTXQ2(2);if(plan.softwareVLANAggregate()){++mac44TSO4.vlanSoftwareAccepted;if(plan.ipBytes()>mac44TSO4.vlanSoftwareMaxIPBytes)mac44TSO4.vlanSoftwareMaxIPBytes=plan.ipBytes();}++audit.accepted;audit.acceptedBytes+=length;
 if(failed)halt();else retireTSO(4);IOLockUnlock(lock);return kIOReturnOutputSuccess;
}

UInt32 RealtekVTDNextEthernet::outputPacket(mbuf_t packet,void *param){
 if(txq3&&txq3->callbackWanted())txq3->callbackBegin(txq3->now());
 const auto result=outputPacketImpl(packet,param);if(txq2)txq2->callback(result);return result;
}
UInt32 RealtekVTDNextEthernet::outputPacketImpl(mbuf_t packet,void *){
 TXQScope callbackElapsed(txq,TXQObservation::Callback);
 if(loop->inGate()){
  IOLockLock(lock);++audit.gated;noteRetry(14,0,TXQObservation::InGate);IOLockUnlock(lock);return kIOReturnOutputStall;
 }
 mbuf_tso_request_flags_t tsoFlags=0;uint32_t tsoMSS=0;
 if(mbuf_get_tso_requested(packet,&tsoFlags,&tsoMSS)!=0){IOLockLock(lock);++tso4Stats.metadataErrors;IOLockUnlock(lock);freePacket(packet);return kIOReturnOutputDropped;}
 if(tsoFlags==MBUF_TSO_IPV4)return outputTSO4(packet,uint32_t(tsoFlags),tsoMSS);
 if(tsoFlags==MBUF_TSO_IPV6)return outputTSO6(packet,uint32_t(tsoFlags),tsoMSS);
 if(tsoFlags){IOLockLock(lock);++tso4Stats.metadataErrors;++tso6Stats.metadataErrors;IOLockUnlock(lock);freePacket(packet);return kIOReturnOutputDropped;}
 IOLockLock(lock);if(tso4Software.active()||tso6Software.active()){if(tso4Software.active()){++tso4Stats.retries;++tso4Stats.regularPacketWaits;}else{++tso6Stats.retries;++tso6Stats.regularPacketWaits;}noteRetry(15,0,TXQObservation::ParentPending);IOLockUnlock(lock);return kIOReturnOutputStall;}
 ++audit.attempts;const bool active=enabled;const unsigned epoch=generation,packetMTU=network.mtu;const bool mac46Active=mac46Features.tx.enabled(),mac44Active=mac44Checksum.enabled(),mac34Target=mac34.nativeReady,mac34Active=mac34.tx.enabled(),checksumHardware=txChecksumPolicy.enabled()||((mac44Active||mac34Active||mac46Active)&&packetMTU==1500),checksum6Hardware=(csum6Policy.enabled()||mac44Active||mac34Active||mac46Active)&&packetMTU==1500;
 if(!active)audit.drop(TXDrop::Inactive);IOLockUnlock(lock);
 if(!active){freePacket(packet);return kIOReturnOutputDropped;}
 size_t length=mbuf_pkthdr_len(packet);
 uint8_t frameHeader[18]{};
 if(length<14||length>maxFrameBytes(packetMTU)||
    mbuf_copydata(packet,0,length<18?length:18,frameHeader)||!frameLengthValid(frameHeader,length,packetMTU)){IOLockLock(lock);
  JTXCopyEvidence copy;const auto id=jtxTrace.begin(frameHeader,false,unsigned(length),packetMTU,epoch,jtxTrace.now(),0,0xffffffffU,copy,nullptr,nullptr,0,0);
  jtxTrace.finish(id,uint32_t(NetworkStatus::InvalidPacket),false);
  audit.drop(TXDrop::Size);if(txRingDiag.pending)txRingDiag.result(packet,epoch,TXRingDiagnostic::Dropped,unsigned(NetworkStatus::InvalidPacket),network);if(length>audit.lengthMax)audit.lengthMax=length;IOLockUnlock(lock);freePacket(packet);return kIOReturnOutputDropped;}
 if(length<60&&!mac44Active&&!mac34Target&&!mac46Active){uint8_t zeros[60]{};
  if(mbuf_copyback(packet,length,60-length,zeros,MBUF_DONTWAIT)){IOLockLock(lock);audit.drop(TXDrop::Padding);if(txRingDiag.pending)txRingDiag.result(packet,epoch,TXRingDiagnostic::Dropped,unsigned(NetworkStatus::InvalidPacket),network);IOLockUnlock(lock);freePacket(packet);return kIOReturnOutputDropped;}
  mbuf_pkthdr_setlen(packet,60);
 }
 unsigned chainCount=0;for(mbuf_t part=packet;part;part=mbuf_next(part))if(mbuf_len(part))++chainCount;
 mbuf_csum_request_flags_t checksum=0;uint32_t checksumValue=0;
 // value is unsupported by the pinned public SDK. Never interpret it as an offset.
 if(mbuf_get_csum_requested(packet,&checksum,&checksumValue)!=0){
  IOLockLock(lock);++txChecksumStats.metadataErrors;if(mac44Active)++mac44Checksum.metadataErrors;audit.drop(TXDrop::Submit);IOLockUnlock(lock);
  freePacket(packet);return kIOReturnOutputDropped;
 }
 const uint32_t checksumKnown=MBUF_CSUM_REQ_IP|MBUF_CSUM_REQ_TCP|MBUF_CSUM_REQ_UDP|MBUF_CSUM_REQ_TCPIPV6|MBUF_CSUM_REQ_UDPIPV6;
 const uint32_t checksumRequest=((checksum&MBUF_CSUM_REQ_IP)?TXIP:0)|((checksum&MBUF_CSUM_REQ_TCP)?TXTCP:0)|
  ((checksum&MBUF_CSUM_REQ_UDP)?TXUDP:0)|((checksum&MBUF_CSUM_REQ_TCPIPV6)?TXTCP6:0)|
  ((checksum&MBUF_CSUM_REQ_UDPIPV6)?TXUDP6:0)|((checksum&~checksumKnown)?0x80000000U:0);
 TXChecksumPlan checksumPlan;
 DMABuffer mapped;uint64_t start=0,end=0,ns=0;
 JTXCopyEvidence copy;
 clock_get_uptime(&start);const auto mapStatus=txqPrepare(txq,[&](){
  if((!mac44Active&&!mac34Target&&!mac46Active)||length>=60)return backend->mapPacket(mapped,packet,&copy);
  struct Copy {mbuf_t packet;size_t length;JTXCopyEvidence *evidence;} context{packet,length,&copy};
  return backend->snapshotTX(mapped,60,[](void *opaque,void *destination,size_t bytes){
   auto &c=*static_cast<Copy*>(opaque);c.evidence->attempted=true;
   if(bytes!=60||c.length>bytes||mbuf_copydata(c.packet,0,c.length,destination)){c.evidence->copied=false;return false;}
   auto out=static_cast<uint8_t*>(destination);for(size_t i=c.length;i<bytes;++i)out[i]=0;
   c.evidence->copied=true;return true;
  },&context);
 });clock_get_uptime(&end);
 absolutetime_to_nanoseconds(end-start,&ns);
 if(mapStatus==DMAStatus::Ok&&checksumRequest)
  checksumPlan=prepareTXChecksum(static_cast<uint8_t*>(backend->bytes(mapped)),size_t(mapped.requestedLength),checksumRequest,checksumHardware&&mapped.segmentCount==1,checksum6Hardware&&mapped.segmentCount==1,mac34Target?34:mac46Active?46:mac44Active?44:42);
 IOLockLock(lock);audit.mapped(mapStatus,ns,unsigned(mapped.segmentCount),chainCount,length,uint32_t(checksum));
 const bool request6=(checksumRequest&(TXTCP6|TXUDP6))!=0;
 if(mac44Active){if(mapStatus==DMAStatus::Ok)mac44Checksum.observe(checksumRequest,checksumPlan);else if(mapStatus!=DMAStatus::PoolFull)++mac44Checksum.mappingErrors;}
 if(request6){++tx6Requests;if(checksumPlan.mode()==TXChecksumMode::Error)++tx6Errors;}
 if(checksumRequest)++txChecksumStats.requestAttempts;
 if(checksumPlan.mode()==TXChecksumMode::Software)++txChecksumStats.softwareCompletedAttempts;
 if(checksumPlan.mode()==TXChecksumMode::Error)++txChecksumStats.requestErrors;
 // JTXFAST1: begin() has no state change for any frame <=1514 bytes.
 // Avoid preparing its unused diagnostic arguments for those frames only.
 // The larger/VLAN-boundary/Jumbo path and all TX/DMA work stay unchanged.
 uint64_t id=0;
 if(length>1514U){
  uint64_t beginNS=0,mapDoneNS=0;absolutetime_to_nanoseconds(start,&beginNS);absolutetime_to_nanoseconds(end,&mapDoneNS);
  id=jtxTrace.begin(frameHeader,true,unsigned(length),packetMTU,epoch,beginNS,mapDoneNS,uint32_t(mapStatus),copy,
   mapStatus==DMAStatus::Ok?&mapped:nullptr,mapStatus==DMAStatus::Ok?static_cast<const uint8_t*>(backend->bytes(mapped)):nullptr,
   backend->txPoolIOVA(),backend->txPoolStride());
 }
 if(mapStatus==DMAStatus::PoolFull){noteRetry(16,0,TXQObservation::Pool);jtxTrace.finish(id,uint32_t(NetworkStatus::Full),false,true);++audit.full;
  if(txRingDiag.pending)txRingDiag.result(packet,epoch,TXRingDiagnostic::StalledAgain,unsigned(NetworkStatus::Full),network);
  txRingDiag.full(packet,epoch,1,audit.attempts,audit.full,network);
  IOLockUnlock(lock);return kIOReturnOutputStall;}
 if(mapStatus!=DMAStatus::Ok){jtxTrace.finish(id,uint32_t(NetworkStatus::Mapping),false);audit.drop(TXDrop::Mapping);if(txRingDiag.pending)txRingDiag.result(packet,epoch,TXRingDiagnostic::Dropped,unsigned(NetworkStatus::Mapping),network);IOLockUnlock(lock);freePacket(packet);return kIOReturnOutputDropped;}
 const unsigned mac44Slot=network.producer;
 const auto status=(enabled&&generation==epoch)?submitNetwork(*backend,*backend,rings,network,mapped,packet,id,checksumPlan):NetworkStatus::NotReady;
 const bool consumed=!mapped.opaque;
 jtxTrace.finish(id,uint32_t(status),consumed,status==NetworkStatus::Full);
 if(consumed){++audit.accepted;audit.acceptedBytes+=length;
  if(mac46Active&&checksumRequest){const unsigned family=request6?1:0;if(checksumPlan.mode()==TXChecksumMode::Software)++mac46Features.txSoftware[family];else if(checksumPlan.mode()!=TXChecksumMode::None)++mac46Features.txHardware[family];}
  if(mac44Active)mac44Checksum.submitted(mac44Slot,epoch,checksumRequest,checksumPlan);
  if(mac34Target&&checksumRequest){const unsigned family=request6?1:0;if(checksumPlan.mode()==TXChecksumMode::Software)++mac34.txSoftware[family];else if(checksumPlan.mode()!=TXChecksumMode::None)++mac34.txHardware[family];}
  if(checksumPlan.mode()==TXChecksumMode::HardwareIPv6)++tx6Hardware;
  if(request6&&checksumPlan.mode()==TXChecksumMode::Software)++tx6Software;
  if(checksumPlan.mode()==TXChecksumMode::HardwareIPv4)++txChecksumStats.hardwareScheduled;
  if(checksumPlan.mode()==TXChecksumMode::Software)++txChecksumStats.softwareSubmitted;
 }else if(status==NetworkStatus::Full)++audit.full;else audit.drop(TXDrop::Submit);
 if(txRingDiag.pending)txRingDiag.result(packet,epoch,consumed?TXRingDiagnostic::Consumed:status==NetworkStatus::Full?TXRingDiagnostic::StalledAgain:TXRingDiagnostic::Dropped,unsigned(status),network);
 if(status==NetworkStatus::Full&&!consumed)noteRetry(17,0,TXQObservation::fullReason(network.txFullReason));
 if(status==NetworkStatus::Full&&!consumed)txRingDiag.full(packet,epoch,network.txFullReason,audit.attempts,audit.full,network);
 IOLockUnlock(lock);
 if(consumed)return kIOReturnOutputSuccess;
 backend->unmap(mapped);backend->complete(mapped);backend->release(mapped);
 if(status==NetworkStatus::Full)return kIOReturnOutputStall;
 freePacket(packet);return kIOReturnOutputDropped;
}
bool RealtekVTDNextEthernet::rxNoWaitActive() const {
 return rxNoWaitPolicy.active(backend&&backend->dmaReady(),backend&&backend->deviceMapperPresent());
}
mbuf_t RealtekVTDNextEthernet::allocateRXPacket(unsigned length){
 // Called only by receive(), with the existing controller serialization.
 // OFF, other identities and DeviceMapper retain the original allocator.
 if(!rxNoWaitActive())return allocatePacket(length);
 ++rxNoWaitAttempts;
 if(!length||length>maxFrameBytes(kJumboMTU)){++rxNoWaitFailures;return nullptr;}
 mbuf_t packet=nullptr;
 const errno_t status=mbuf_allocpacket(MBUF_DONTWAIT,length,nullptr,&packet);
 if(status||!packet){if(packet)freePacket(packet);++rxNoWaitFailures;return nullptr;}
 // This mbuf is CPU-owned and never used as a DMA target. The controller has
 // no packet-buffer-alignment override. Do not assume a one-mbuf allocation.
 // Normalize every component to the exact frame length without growing it.
 size_t remaining=length;unsigned parts=0;
 for(mbuf_t part=packet;part;part=mbuf_next(part)){
  if(++parts>length+1U){freePacket(packet);++rxNoWaitFailures;return nullptr;}
  const size_t used=mbuf_len(part),space=mbuf_trailingspace(part);
  if(space>SIZE_MAX-used){freePacket(packet);++rxNoWaitFailures;return nullptr;}
  const size_t capacity=used+space,bytes=remaining<capacity?remaining:capacity;
  mbuf_setlen(part,bytes);remaining-=bytes;
 }
 if(remaining||!(mbuf_flags(packet)&MBUF_PKTHDR)||mbuf_nextpkt(packet)){
  freePacket(packet);++rxNoWaitFailures;return nullptr;
 }
 mbuf_pkthdr_setlen(packet,length);++rxNoWaitAllocated;return packet;
}
bool RealtekVTDNextEthernet::rxFillActive() const {
 return rxFillPolicy.enabled()&&rxNoWaitActive();
}
mbuf_t RealtekVTDNextEthernet::prepareRXPacket(const uint8_t *source,unsigned length,RXPrepareStatus &status){
 // OFF preserves the complete RXNOWAIT1 allocator/normalizer and copyback path.
 status=RXPrepareStatus::AllocationFailed;
 if(!rxFillActive()){
  auto packet=allocateRXPacket(length);if(packet)status=RXPrepareStatus::NeedsCopy;
  return packet;
 }
 ++rxFillAttempts;++rxNoWaitAttempts;status=RXPrepareStatus::PreparationFailed;
 const uintptr_t sourceAddress=reinterpret_cast<uintptr_t>(source);
 if(!source||!length||length>maxFrameBytes(kJumboMTU)||length>UINTPTR_MAX-sourceAddress){
  ++rxFillPreparationFailures;++rxNoWaitFailures;return nullptr;
 }
 mbuf_t packet=nullptr;
 const errno_t allocated=mbuf_allocpacket(MBUF_DONTWAIT,length,nullptr,&packet);
 if(allocated||!packet){
  if(packet)freePacket(packet);++rxFillAllocationFailures;++rxNoWaitFailures;
  status=RXPrepareStatus::AllocationFailed;return nullptr;
 }
 auto preparationFailed=[&]()->mbuf_t {
  freePacket(packet);++rxFillPreparationFailures;++rxNoWaitFailures;return nullptr;
 };
 if(!(mbuf_flags(packet)&MBUF_PKTHDR)||mbuf_nextpkt(packet))return preparationFailed();
 // Fresh, exclusively CPU-owned allocation. Public KPI capacity and cluster
 // reference checks authorize each bounded write; no DMA address is changed.
 size_t remaining=length,copied=0;unsigned parts=0;
 for(mbuf_t part=packet;part;part=mbuf_next(part)){
  if(++parts>length+1U)return preparationFailed();
  const size_t used=mbuf_len(part),space=mbuf_trailingspace(part);
  if(space>SIZE_MAX-used)return preparationFailed();
  const size_t capacity=used+space,bytes=remaining<capacity?remaining:capacity;
  if(copied>length||bytes>size_t(length)-copied)return preparationFailed();
  void *destination=nullptr;
  if(bytes){
   destination=mbuf_data(part);const uintptr_t destinationAddress=reinterpret_cast<uintptr_t>(destination);
   if(!destination||bytes>UINTPTR_MAX-destinationAddress||
      (destinationAddress<sourceAddress+length&&sourceAddress<destinationAddress+bytes)||mbuf_mclhasreference(part)!=0)return preparationFailed();
  }
  mbuf_setlen(part,bytes);
  if(bytes){memcpy(destination,source+copied,bytes);copied+=bytes;}
  remaining-=bytes;
 }
 if(remaining||copied!=length)return preparationFailed();
 mbuf_pkthdr_setlen(packet,length);++rxNoWaitAllocated;++rxFillPacketsFilled;
 status=RXPrepareStatus::Filled;return packet;
}
bool RealtekVTDNextEthernet::receive(const uint8_t *bytes,unsigned length){
 ++rxHandoff.calls;
 if(!interface){++rxHandoff.interfaceAbsent;return false;}
 RXPrepareStatus preparation=RXPrepareStatus::AllocationFailed;
 auto packet=prepareRXPacket(bytes,length,preparation);
 if(!packet){if(preparation==RXPrepareStatus::AllocationFailed)++rxHandoff.allocationsFailed;else ++rxHandoff.copiesFailed;return false;}
 if(preparation!=RXPrepareStatus::Filled&&mbuf_copyback(packet,0,length,bytes,MBUF_DONTWAIT)){++rxHandoff.copiesFailed;freePacket(packet);return false;}
 const bool header=(mbuf_flags(packet)&MBUF_PKTHDR)!=0;
 const bool next=mbuf_nextpkt(packet)!=nullptr,attached=interface->getIfnet()!=nullptr;
 const auto returned=interface->inputPacket(packet,length,IONetworkInterface::kInputOptionQueuePacket);
 // inputPacket owns packet after the call; never inspect it again.
 rxHandoff.input(length,returned,header,next,attached);return true;
}
bool RealtekVTDNextEthernet::receive(const uint8_t *bytes,unsigned length,const RXChecksumMetadata &metadata){
 ++rxHandoff.calls;
 if(!interface){++rxHandoff.interfaceAbsent;return false;}
 RXPrepareStatus preparation=RXPrepareStatus::AllocationFailed;
 auto packet=prepareRXPacket(bytes,length,preparation);
 if(!packet){if(preparation==RXPrepareStatus::AllocationFailed)++rxHandoff.allocationsFailed;else ++rxHandoff.copiesFailed;return false;}
 if(preparation!=RXPrepareStatus::Filled&&mbuf_copyback(packet,0,length,bytes,MBUF_DONTWAIT)){++rxHandoff.copiesFailed;freePacket(packet);return false;}
 // kChecksumTCP/UDP include the pseudo header (public IONetworkController ABI).
 static_assert(kChecksumIP==RXCheckedIP&&kChecksumTCP==RXCheckedTCP&&kChecksumUDP==RXCheckedUDP,"checksum mask ABI");
 static_assert(kChecksumTCPIPv6==0x20&&kChecksumUDPIPv6==0x40,"IPv6 Apple ABI differs from normalized 8/16");
 const bool mac44Publish=enabled&&mac44RX.publishable(network.rxChecksumActive)&&network.rxChecksumProfile==RXChecksumProfile::MAC44RevisionC;
 const bool mac34Publish=enabled&&mac34.rx.enabled()&&network.rxChecksumActive&&network.rxChecksumProfile==RXChecksumProfile::MAC34RevisionC;
 const bool mac46Publish=enabled&&mac46Features.rx.enabled()&&mac46Features.txAssist.configured&&network.rxChecksumActive&&network.rxChecksumProfile==RXChecksumProfile::MAC46RevisionC;
 const uint32_t mask=rxChecksumAppleMask(metadata,rxChecksumPolicy.enabled()||mac44Publish||mac34Publish||mac46Publish,csum6Policy.enabled()||mac44Publish||mac34Publish||mac46Publish);
 if(mask){
  if(setChecksumResult(packet,kChecksumFamilyTCPIP,mask,mask)){if(mac44Publish)mac44RX.notePublished(metadata);if(mac34Publish)mac34.received(metadata);if(mac46Publish)mac46Features.received(metadata);if(metadata.ipv6)++rx6Published;else ++rxChecksumPublished;}
  else {if(metadata.ipv6)++rx6PublishErrors;else ++rxChecksumPublishErrors;
   // Clear any partially encoded result before software fallback; refuse only
   // if the framework cannot establish an unannotated mbuf either.
   if(!setChecksumResult(packet,kChecksumFamilyTCPIP,0,0)){freePacket(packet);return false;}
   ++rxChecksumCleared;
  }
 }
 const bool header=(mbuf_flags(packet)&MBUF_PKTHDR)!=0;
 const bool next=mbuf_nextpkt(packet)!=nullptr,attached=interface->getIfnet()!=nullptr;
 const auto returned=interface->inputPacket(packet,length,IONetworkInterface::kInputOptionQueuePacket);
 // inputPacket owns packet after the call; never inspect it again.
 rxHandoff.input(length,returned,header,next,attached);return true;
}
bool RealtekVTDNextEthernet::filter(OSObject *owner,IOFilterInterruptEventSource *){
 auto self=static_cast<RealtekVTDNextEthernet*>(owner);uint16_t causes=0;
 const bool readOK=self->backend&&self->backend->read16(0x3e,causes);
 self->irqAudit.filter(causes,readOK,kNetworkInterruptMask);
 if(!readOK||causes==0xffff||!(causes&kNetworkInterruptMask))return false;
 self->backend->write16(0x3c,0);return true;
}
void RealtekVTDNextEthernet::interrupt(OSObject *owner,IOInterruptEventSource *,int){
 auto self=static_cast<RealtekVTDNextEthernet*>(owner);IOLockLock(self->lock);
 self->irqAudit.action(self->enabled);
 if(self->enabled){bool ok=true;unsigned serviced=0;
  for(unsigned i=0;i<8;++i){uint16_t causes=0;
   if(!self->backend->read16(0x3e,causes)||causes==0xffff){ok=false;break;}
   if(!(causes&kNetworkInterruptMask)){if(!serviced)IRQAudit::add(self->irqAudit.emptyActions);break;}
   ++serviced;self->irqAudit.service(causes);
   if(!self->backend->write16(0x3e,causes)){IRQAudit::add(self->irqAudit.ackFailures);ok=false;break;}
   const unsigned txUsedBefore=self->network.used;const uint64_t txCompletedBefore=self->network.txCompleted;
   const bool serviceOK=serviceNetwork(*self->backend,*self->backend,self->rings,self->network,*self,causes);
   self->txRingDiag.irq(self->generation,causes,serviceOK,txUsedBefore,txCompletedBefore,self->network);
   if(self->network.txCompleted!=txCompletedBefore)self->noteTXQ(TXQObservation::Reclaim);
   if(serviceOK&&self->network.txCompleted!=txCompletedBefore&&self->txq3&&self->txq3->enabled())self->txq3->reclaimDone(self->generation,self->txq3->now(),self->network.used);
   if(!serviceOK||self->mac34TXQ4.receiptCounters().invalidCookies){ok=false;break;}
   if(self->tso4Software.remaining()&&self->pumpTSO4()){ok=false;break;}
   if(self->tso6Software.remaining()&&self->pumpTSO6()){ok=false;break;}
   self->retireTSO(4);self->retireTSO(6);
  }
  if(serviced==8)IRQAudit::add(self->irqAudit.budgetExhausted);
  if(ok)ok=self->backend->write16(0x3c,kNetworkInterruptMask);
  if(!ok){self->captureWatchdog("irq-service");self->halt();}
 }
 const bool active=self->enabled;
 // TXQ1: decide under the SAME lock as output/reclaim/pump. The verified flag
 // is set only by the exact initialized MAC34 native64 NoIOMMU policy.
 // No queue API under this lock, and no change to Apple's serviceCount fence.
 const bool serviceOutput=self->mac34TXQ1.interruptService(active,
  self->network.macVersion==34&&self->network.mac34Native64Verified,
  self->tso4Software.active(),self->tso6Software.active());
 self->noteTXQ2(serviceOutput?4:5);
 if(self->txq3&&self->txq3->enabled())self->txq3->decision(self->generation,serviceOutput,self->txq3->now(),self->network.used);
 const bool probe=self->rxProbeRequested&&self->rxHandoff.inputCalls>self->rxProbeInputCalls;
 if(probe)self->rxProbeRequested=false;IOLockUnlock(self->lock);
 // Keep the inherited flush outside the controller lock. The input queue is
 // serialized by this workloop. Sampling is bounded to one pair per second.
 ifnet_stats_param before{},after{};errno_t beforeStatus=ENXIO,afterStatus=ENXIO;
 auto ifp=self->interface?self->interface->getIfnet():nullptr;
 if(probe&&ifp)beforeStatus=ifnet_stat(ifp,&before);
 const UInt32 flushed=self->interface?self->interface->flushInputQueue():0;
 if(probe&&ifp)afterStatus=ifnet_stat(ifp,&after);
 IOLockLock(self->lock);self->rxHandoff.flush(flushed);
 if(probe){self->rxProbeBefore=before;self->rxProbeAfter=after;
  self->rxProbeBeforeStatus=beforeStatus;self->rxProbeAfterStatus=afterStatus;
  self->rxProbeDelivered=self->network.rxDelivered;
  self->rxProbeInputCalls=self->rxHandoff.inputCalls;
  self->rxProbeFlushReturned=self->rxHandoff.flushReturned;++self->rxProbeSequence;}
 IOLockUnlock(self->lock);
 if(serviceOutput){const bool resumed=self->txqService();self->txRingDiag.queueService(resumed);}
}
void RealtekVTDNextEthernet::publishPHYDiagnostic(const PHYDiagnostic &d,const char *reason,const char *snapshot){
 auto dict=OSDictionary::withCapacity(32);if(!dict)return;
 auto number=[&](const char *key,unsigned value){auto n=OSNumber::withNumber(value,32);if(n){dict->setObject(key,n);n->release();}setProperty(key,value,32);};
 auto boolean=[&](const char *key,bool value){dict->setObject(key,value?kOSBooleanTrue:kOSBooleanFalse);setProperty(key,value);};
 number("PHYDiagValidMask",d.valid);number("PHYDiagReadFailures",d.failures);
 const char *rawKeys[]={"PHYDiagBMCR","PHYDiagBMSRFirst","PHYDiagBMSRSecond","PHYDiagANAR","PHYDiagANLPAR","PHYDiagCTRL1000","PHYDiagSTAT1000","PHYDiagEEEAdvertisement","PHYDiagPHYStatusRaw"};
 const unsigned values[]={d.bmcr,d.bmsrFirst,d.bmsrSecond,d.anar,d.anlpar,d.ctrl1000,d.stat1000,d.eee,d.raw};
 for(unsigned i=0;i<9;++i)number(rawKeys[i],values[i]);
 // Decoded values require the associated ValidMask bit; invalid reads never
 // inherit older sample values. Unknown speed encodings produce zero.
 boolean("PHYDiagAutonegEnabled",d.autonegEnabled());boolean("PHYDiagAutonegComplete",d.autonegComplete());
 boolean("PHYDiagLink",d.link());boolean("PHYDiagFullDuplex",d.fullDuplex());number("PHYDiagSpeedMbps",d.speedMbps());
 const char *advertiseKeys[]={"PHYDiagAdvertise10Half","PHYDiagAdvertise10Full","PHYDiagAdvertise100Half","PHYDiagAdvertise100Full","PHYDiagAdvertise1000Half","PHYDiagAdvertise1000Full"};
 const char *partnerKeys[]={"PHYDiagPartner10Half","PHYDiagPartner10Full","PHYDiagPartner100Half","PHYDiagPartner100Full","PHYDiagPartner1000Half","PHYDiagPartner1000Full"};
 for(unsigned i=0;i<6;++i){boolean(advertiseKeys[i],d.advertise(i));boolean(partnerKeys[i],d.partner(i));}
 auto text=OSString::withCString(reason);if(text){dict->setObject("PHYDiagCaptureReason",text);text->release();}
 setProperty("PHYDiagCaptureReason",reason);
 if(snapshot)setProperty(snapshot,dict);dict->release();
}
void RealtekVTDNextEthernet::publishPause(const PHYDiagnostic &d,const PauseResolution &p){
 JTXPHY cached;cached.observedNS=jtxTrace.now();cached.valid=d.valid;cached.bmcr=d.bmcr;
 cached.bmsrFirst=d.bmsrFirst;cached.bmsrSecond=d.bmsrSecond;cached.anar=d.anar;cached.anlpar=d.anlpar;
 cached.raw=d.raw;cached.anPending=mtuTransaction.awaitingAutoneg;cached.mtuState=uint8_t(mtuTransaction.state);
 jtxTrace.observePHY(cached); // Existing PHY sample only; no additional register access.
 setProperty("PauseReportOnly",flowControl.mac44()||!flowControl.requested);setProperty("PauseEffectiveVerified",false);
 if(flowControl.mac46()){
  setProperty("MAC46FC1Schema",1,32);setProperty("MAC46FC1AutoRequested",flowControl.requested);
  setProperty("MAC46FC1Applied",flowControl.applied&&mtuTransaction.policyApplied);setProperty("MAC46FC1State",flowControl.state(mtuTransaction,d,p,enabled));
  setProperty("MAC46FC1Advertisement",flowControl.requested&&mtuTransaction.mtu==1500?GU25FlowControl::advertisement():0,16);
  setProperty("MAC46FC1NegotiatedValid",p.valid&&p.agreement);setProperty("MAC46FC1TX",p.valid&&p.agreement&&p.tx);setProperty("MAC46FC1RX",p.valid&&p.agreement&&p.rx);
  setProperty("MAC46FC1Active",flowControl.active(mtuTransaction,p,enabled));setProperty("MAC46FC1WireVerified",false);
 }

 if(flowControl.eligible&&!flowControl.mac44()&&!flowControl.mac34()&&!flowControl.mac46()){
  setProperty("FC1ConfigurationValid",flowControl.configurationValid);
  setProperty("FC1Requested",flowControl.requested);
  setProperty("FC1PolicyApplied",flowControl.applied&&mtuTransaction.policyApplied);
  setProperty("FC1RequestedAdvertisement",flowControl.requested&&mtuTransaction.mtu==1500?GU25FlowControl::advertisement():0,16);
  setProperty("FC1ApplyAttempts",flowControl.applyAttempts,64);
  setProperty("FC1ApplyFailures",flowControl.applyFailures,64);
  setProperty("FC1State",flowControl.state(mtuTransaction,d,p,enabled));
  // Negotiated plus agreeing PHY status, NOT counted PAUSE frames on the wire.
  setProperty("GU25PauseEnabled",flowControl.active(mtuTransaction,p,enabled));
  setProperty("FC1WireEffectVerified",false);
 }
 if(filterMACVersion==34){
  setProperty("MAC34FC1Schema",1,32);setProperty("MAC34FC1Eligible",flowControl.mac34()&&flowControl.eligible);
  setProperty("MAC34FC1Requested",flowControl.mac34()&&flowControl.requested);
  setProperty("MAC34FC1Applied",flowControl.mac34()&&flowControl.applied&&mtuTransaction.policyApplied);
  setProperty("MAC34FC1RequestedAdvertisement",flowControl.mac34()&&flowControl.requested&&mtuTransaction.mtu==1500?GU25FlowControl::advertisement():0,16);
  setProperty("MAC34FC1ApplyAttempts",flowControl.mac34()?flowControl.applyAttempts:0,64);
  setProperty("MAC34FC1ApplyFailures",flowControl.mac34()?flowControl.applyFailures:0,64);
  setProperty("MAC34FC1State",flowControl.state(mtuTransaction,d,p,enabled));
  setProperty("MAC34FC1NegotiatedValid",flowControl.mac34()&&flowControl.requested&&p.valid&&p.agreement);
  setProperty("MAC34FC1Active",flowControl.mac34()&&flowControl.active(mtuTransaction,p,enabled));
  setProperty("MAC34FC1WireVerified",false);
 }
 if(filterMACVersion==44){
  setProperty("MAC44FC1Schema",1,32);
  setProperty("MAC44FC1Requested",flowControl.mac44Preference);
  setProperty("MAC44FC1ConfigurationValid",flowControl.configurationValid);
  setProperty("MAC44FC1DMAReady",mac44Checksum.dmaReady);setProperty("MAC44FC1MapperReady",mac44Checksum.mapperReady);setProperty("MAC44FC1Native64Ready",mac44Checksum.native64Ready);setProperty("MAC44FC1Eligible",flowControl.mac44()&&flowControl.eligible);
  setProperty("MAC44FC1PolicyRequested",flowControl.mac44()&&flowControl.requested);
  setProperty("MAC44FC1Applied",flowControl.mac44()&&flowControl.applied&&mtuTransaction.policyApplied);
  setProperty("MAC44FC1RequestedAdvertisement",flowControl.requested&&mtuTransaction.mtu==1500?GU25FlowControl::advertisement():0,16);
  setProperty("MAC44FC1ApplyAttempts",flowControl.applyAttempts,64);
  setProperty("MAC44FC1ApplyFailures",flowControl.applyFailures,64);
  setProperty("MAC44FC1MTUFailures",mtuTransaction.failures,64);
  setProperty("MAC44FC1State",flowControl.state(mtuTransaction,d,p,enabled));
  setProperty("MAC44FC1NegotiatedValid",flowControl.requested&&p.valid&&p.agreement);
  setProperty("MAC44FC1Active",flowControl.active(mtuTransaction,p,enabled));
  setProperty("MAC44FC1WireVerified",false);setProperty("MAC44FC1EffectiveVerified",false);
 }
 setProperty("PauseObservationSequence",++pauseSamples,64);
 setProperty("PauseObservationValidMask",d.valid,32);setProperty("PauseObservationReadFailures",d.failures,32);
 setProperty("PauseBMCR",d.bmcr,16);setProperty("PauseBMSRFirst",d.bmsrFirst,16);setProperty("PauseBMSRSecond",d.bmsrSecond,16);
 setProperty("PauseANAR",d.anar,16);setProperty("PauseANLPAR",d.anlpar,16);setProperty("PausePHYStatusRaw",d.raw,8);
 setProperty("PauseNegotiatedValid",p.valid);setProperty("PauseNegotiatedState",p.reason);
 setProperty("PauseTxNegotiated",p.tx);setProperty("PauseRxNegotiated",p.rx);
 setProperty("PausePHYStatusValid",p.phyStatusValid);setProperty("PausePHYStatusTx",p.phyTx);setProperty("PausePHYStatusRx",p.phyRx);
 setProperty("PausePHYStatusAgreementValid",p.valid&&p.phyStatusValid);setProperty("PausePHYStatusAgreement",p.agreement);
 setProperty("PauseMediumFlowControl",p.mediumFlow);
}
void RealtekVTDNextEthernet::reportLinkInactive(const char *reason){
 PHYDiagnostic d;PauseResolution p;p.reason=reason;publishPause(d,p);
 setProperty("Media1LinkReportSucceeded",setLinkStatus(kIONetworkLinkValid,nullptr,0));setProperty("PHYLink",false);
}
void RealtekVTDNextEthernet::updateLink(){
 uint8_t phy=0;if(!backend->read8(0x6c,phy)||phy==0xff){previousPHY=0xff;reportLinkInactive("phy-read-failed");return;}
 const bool linkTransition=previousPHY==0xff||((phy^previousPHY)&0x1f)!=0;
 if(phy!=previousPHY){previousPHY=phy;
  RegisterAccess diagAccess(*backend,filterMACVersion);
  publishPHYDiagnostic(capturePHYDiagnostic(diagAccess),(phy&2)?"link-up":"link-down","PHYDiagLastLinkChange");
 }
 if(!(phy&2)){reportLinkInactive("link-down");return;}
 // Reuse the existing serialized one-second link callback. A fresh, bounded
 // observation also catches AN completion/partner changes at unchanged speed.
 RegisterAccess pauseAccess(*backend,filterMACVersion);
 const auto d=capturePauseObservation(pauseAccess,phy);auto p=resolvePause(d);
 mtuTransaction.observe(d,p);
 flowControl.qualify(mtuTransaction,d,p);
 if(gu24Profile&&!flowControl.requested)p.mediumFlow=false; // GU24 remains conservative.
 if(network.mtu>1500&&strcmp(p.reason,"jumbo-pause-mismatch")==0){
  mtuTransaction.state=MTUState::SAFE_STOP;mtuTransaction.policyApplied=false;++mtuTransaction.failures;halt();return;
 }
 if(!(d.valid&0x100)){previousPHY=0xff;reportLinkInactive("phy-sample-unstable");return;}
 const unsigned index=pauseMediumIndex(phy,p.mediumFlow);
 if(!index){reportLinkInactive("speed-invalid");return;}
 // The stopped EEE receipt does not certify later link-up transitions.
 // Reassert only MAC46 TX-LPI bits, after the existing stable link sample.
 if(linkTransition&&mac46EEE.linkEligible){
  RegisterAccess lpiAccess(*backend,filterMACVersion);
  if(!mac46EEE.linkLPI.apply(lpiAccess,generation,phy)){halt();return;}
 }
 publishPause(d,p);
 const bool reported=setLinkStatus(kIONetworkLinkValid|kIONetworkLinkActive,media[index]);
 setProperty("Media1LinkReportSucceeded",reported);setProperty("PauseMediumFlowControl",reported&&p.mediumFlow);setProperty("PHYLink",true);
}
void RealtekVTDNextEthernet::captureWatchdog(const char *reason){
 // Called with controller lock held, before halt changes registers or releases packets.
 // Read-only evidence, no descriptor polling, completion or automatic restart.
 const bool observe46=mac46VTD.enabled();MAC46VTDObservation::Sample observation;
 if(observe46)observation=MAC46VTDObservation::software(jtxClock(nullptr),generation,
  strcmp(reason,"tx-watchdog")==0?MAC46VTDObservation::Watchdog:
  strcmp(reason,"network-status")==0?MAC46VTDObservation::NetworkError:
  strcmp(reason,"irq-service")==0?MAC46VTDObservation::IRQError:MAC46VTDObservation::TSOError,
  enabled,network,backend->txPoolCount(),backend->txPoolLeased(),backend->txPoolStride(),backend->txPoolIOVA(),
  IRQAudit::read(irqAudit.actions),IRQAudit::read(irqAudit.passes),IRQAudit::read(irqAudit.budgetExhausted),stalledSeconds);
 setProperty("Stage4BWatchdogCaptured",strcmp(reason,"tx-watchdog")==0);
 setProperty("Stage4DStopReason",reason);
 setProperty("Stage4DStopStatus",unsigned(network.status),32);
 setProperty("Stage4DSleepCountAtStop",power.sleeps,64);
 setProperty("Stage4DWakeCountAtStop",power.wakes,64);
 setProperty("Stage4DIRQActionsAtStop",IRQAudit::read(irqAudit.actions),64);
 // One bounded diagnostic read after CPU synchronization. No completion or DMA release.
 const bool readable=network.tx&&rings.tx.mapped;
 const DMAStatus sync=readable?backend->syncForCPU(rings.tx):DMAStatus::InvalidState;
 setProperty("Stage4DTXSnapshotValid",sync==DMAStatus::Ok);
 setProperty("Stage4DTXSyncStatus",unsigned(sync),32);
 if(sync==DMAStatus::Ok){
  backend->memoryBarrier();
  const unsigned head=network.consumer%network.txCount;
  const uint32_t headOpts=network.tx[4*head];
  setProperty("Stage4DHeadOpts1",headOpts,32);
   const bool checksumRecord=head<64&&network.used&&network.cookies[head]&&mac44Checksum.slots[head].valid&&mac44Checksum.slots[head].epoch==generation;
   setProperty("Stage4DMAC44TXCSUM1MetadataValid",checksumRecord);
   if(checksumRecord){setProperty("Stage4DMAC44TXCSUM1Mode",unsigned(mac44Checksum.slots[head].mode),32);setProperty("Stage4DMAC44TXCSUM1Request",mac44Checksum.slots[head].request,32);}
  setProperty("Stage4DHeadOpts2",network.tx[4*head+1],32);
  setProperty("Stage4DHeadAddressLow",network.tx[4*head+2],32);
  setProperty("Stage4DHeadAddressHigh",network.tx[4*head+3],32);
  unsigned owned=0;for(unsigned j=0;j<network.used&&j<network.txCount;++j)
   owned+=(network.tx[4*((head+j)%network.txCount)]&(1U<<31))!=0;
  setProperty("Stage4DPendingOwned",owned,32);
  setProperty("Stage4DPendingReturned",network.used-owned,32);
  if(observe46)MAC46VTDObservation::descriptor(observation,readable,true,unsigned(sync),headOpts,
   network.tx[4*head+1],uint64_t(network.tx[4*head+2])|(uint64_t(network.tx[4*head+3])<<32),owned);
 }
 if(observe46&&sync!=DMAStatus::Ok)MAC46VTDObservation::descriptor(observation,readable,false,unsigned(sync),0,0,0,0);

 setProperty("Stage4BWDSubmitted",network.txSubmitted,64);
 setProperty("Stage4BWDCompleted",network.txCompleted,64);
 setProperty("Stage4BWDRXDelivered",network.rxDelivered,64);
 setProperty("Stage4BWDUsed",network.used,32);
 setProperty("Stage4BWDProducer",network.producer,32);
 setProperty("Stage4BWDConsumer",network.consumer,32);
 setProperty("Stage4BWDPoolLeased",backend->txPoolLeased(),32);
 uint32_t valid=0,value32=0;uint16_t value16=0;uint8_t value8=0;
 // Reuse exactly the existing reads, in their existing order. The new mask
 // additionally distinguishes transport return from all-ones sentinel validity.
 bool returned=backend->readConfig16(4,value16);
 if(returned){setProperty("Stage4BWDPCICommand",value16,16);valid|=1;}
 if(observe46)MAC46VTDObservation::read(observation,MAC46VTDObservation::PCI,MAC46VTDObservation::PCICommand,true,returned,value16,0xffff);
 returned=backend->read8(0x37,value8);
 if(returned){setProperty("Stage4BWDChipCommand",value8,8);valid|=2;}
 if(observe46)MAC46VTDObservation::read(observation,MAC46VTDObservation::MAC,MAC46VTDObservation::MACCommand,true,returned,value8,0xff);
 returned=backend->read16(0x3c,value16);
 if(returned){setProperty("Stage4BWDIMR",value16,16);valid|=4;}
 if(observe46)MAC46VTDObservation::read(observation,MAC46VTDObservation::InterruptMask,MAC46VTDObservation::IMR,true,returned,value16,0xffff);
 returned=backend->read16(0x3e,value16);
 if(returned){setProperty("Stage4BWDISR",value16,16);valid|=8;}
 if(observe46)MAC46VTDObservation::read(observation,MAC46VTDObservation::InterruptStatus,MAC46VTDObservation::ISR,true,returned,value16,0xffff);
 returned=backend->read8(0x38,value8);
 if(returned){setProperty("Stage4BWDTXPoll",value8,8);valid|=16;}
 if(observe46)MAC46VTDObservation::read(observation,MAC46VTDObservation::Poll,MAC46VTDObservation::TXPoll,true,returned,value8,0xff);
 returned=backend->read8(0x6c,value8);
 if(returned){setProperty("Stage4BWDPhyStatus",value8,8);valid|=32;}
 if(observe46)MAC46VTDObservation::read(observation,MAC46VTDObservation::PHYStatus,MAC46VTDObservation::PHY,true,returned,value8,0xff);
 returned=backend->read32(0x20,value32);
 if(returned){setProperty("Stage4BWDTXRingLow",value32,32);valid|=64;}
 if(observe46)MAC46VTDObservation::read(observation,MAC46VTDObservation::RingLow,MAC46VTDObservation::TXRingLow,true,returned,value32,0xffffffffU);
 returned=backend->read32(0x24,value32);
 if(returned){setProperty("Stage4BWDTXRingHigh",value32,32);valid|=128;}
 if(observe46)MAC46VTDObservation::read(observation,MAC46VTDObservation::RingHigh,MAC46VTDObservation::TXRingHigh,true,returned,value32,0xffffffffU);
 setProperty("Stage4BWDReadValid",valid,32);
 if(observe46)mac46VTD.record(observation,true);
 IOLog("RealtekVTDNext: stop reason=%s submitted=%llu completed=%llu used=%u; pre-stop snapshot captured\n",reason,network.txSubmitted,network.txCompleted,network.used);
}
void RealtekVTDNextEthernet::tick(OSObject *owner,IOTimerEventSource *){
 auto self=static_cast<RealtekVTDNextEthernet*>(owner);IOLockLock(self->lock);
 self->rxProbeRequested=true;
 if(self->enabled&&self->tso4Software.remaining()&&self->pumpTSO4()){self->captureWatchdog("tso4-progress");self->halt();}
 if(self->enabled&&self->tso6Software.remaining()&&self->pumpTSO6()){self->captureWatchdog("tso6-progress");self->halt();}
 if(self->enabled){self->retireTSO(4);self->retireTSO(6);self->updateLink();
  if(self->mac46EEE.linkEligible&&!self->enabled){IOLockUnlock(self->lock);return;} // link-up containment failed: keep halt/timer cancellation.
  if(self->network.used&&self->network.txCompleted==self->previousCompleted)++self->stalledSeconds;else self->stalledSeconds=0;
  self->previousCompleted=self->network.txCompleted;
  if(self->mac46VTD.enabled()&&self->stalledSeconds){
   // Existing timer cadence only: software/metadata, no descriptor read, MMIO or sync.
   self->mac46VTD.record(MAC46VTDObservation::software(jtxClock(nullptr),self->generation,
    MAC46VTDObservation::TimerStall,self->enabled,self->network,self->backend->txPoolCount(),
    self->backend->txPoolLeased(),self->backend->txPoolStride(),self->backend->txPoolIOVA(),
    IRQAudit::read(self->irqAudit.actions),IRQAudit::read(self->irqAudit.passes),
    IRQAudit::read(self->irqAudit.budgetExhausted),self->stalledSeconds));
  }
  // Watchdog detects lack of IRQ completion; it never collects OWN or pretends to complete packets.
  if(self->network.status!=NetworkStatus::Ok){self->captureWatchdog("network-status");self->halt();}
  else if(self->stalledSeconds>=10){self->captureWatchdog();self->setProperty("Stage3BTXWatchdog",true);self->halt();}
  else{self->publish();self->timer->setTimeoutMS(1000);}
 }
 IOLockUnlock(self->lock);
 // Separate framework observations at the existing timer cadence, outside the controller lock.
 // createOutputQueue() always constructs IOBasicOutputQueue; no queue lock inversion.
 auto queue=static_cast<IOBasicOutputQueue*>(self->getOutputQueue());
 if(queue){static_cast<RVTTXQOutputQueue*>(queue)->publishObservation(self);self->setProperty("P12TXQueueDrops",queue->getDropCount(),32);
  self->setProperty("P12TXQueueRetries",queue->getRetryCount(),32);
  self->setProperty("P12TXQueueStalls",queue->getStallCount(),32);
  self->setProperty("P12TXQueueState",queue->getState(),32);
  self->setProperty("P13TXQueueCapacity",queue->getCapacity(),32);
  self->setProperty("P12TXQueueObservationAvailable",true);}
}
IOReturn RealtekVTDNextEthernet::selectMedium(const IONetworkMedium *medium){
 if(!medium)medium=media[0];if(medium!=media[0])return kIOReturnUnsupported;
 return setSelectedMedium(medium)?kIOReturnSuccess:kIOReturnError;
}
bool RealtekVTDNextEthernet::applyReceiveFilter(){
 const bool ok=backend&&multicast.apply(*backend,promiscuous,filterMACVersion,gu24Profile);
 publishReceiveFilter();return ok;
}
void RealtekVTDNextEthernet::publishReceiveFilter(){
 const char *names[]={"unknown","normal","selective","promiscuous","all-multicast-fallback"};
 setProperty("MC1Mode",unsigned(multicast.applied),32);setProperty("MC1ModeName",names[unsigned(multicast.applied)]);
 setProperty("MC1MulticastRequested",multicast.enabled);setProperty("MC1PromiscuousRequested",promiscuous);
 setProperty("MC1AddressCount",multicast.addressCount,32);setProperty("MC1HashMask",multicast.mask,64);
 setProperty("MC1FilterReady",multicast.valid);setProperty("MC1FallbackAttempted",multicast.fallback);
 setProperty("MC1FallbackVerified",multicast.fallback&&multicast.valid);setProperty("MC1HashImperfect",true);
 setProperty("MC1RegisterLow",multicast.observedLow,32);setProperty("MC1RegisterHigh",multicast.observedHigh,32);
 setProperty("MC1RXConfig",multicast.observedConfig,32);setProperty("MC1ReadValidMask",multicast.readValid,32);
 setProperty("MC1Generation",multicast.generation,64);setProperty("MC1AppliedGeneration",multicast.appliedGeneration,64);
}
IOReturn RealtekVTDNextEthernet::setMulticastMode(bool active){
 IOLockLock(lock);multicast.enabled=active;const bool ok=!enabled||applyReceiveFilter();
 publishReceiveFilter();IOLockUnlock(lock);return ok?kIOReturnSuccess:kIOReturnIOError;
}
IOReturn RealtekVTDNextEthernet::setMulticastList(IOEthernetAddress *addresses,UInt32 count){
 static_assert(sizeof(IOEthernetAddress)==6,"Ethernet address layout");
 IOLockLock(lock);
 if(!multicast.list(reinterpret_cast<const uint8_t*>(addresses),count)){IOLockUnlock(lock);return kIOReturnBadArgument;}
 const bool ok=!enabled||applyReceiveFilter();publishReceiveFilter();IOLockUnlock(lock);return ok?kIOReturnSuccess:kIOReturnIOError;
}
IOReturn RealtekVTDNextEthernet::setPromiscuousMode(bool active){
 IOLockLock(lock);promiscuous=active;const bool ok=!enabled||applyReceiveFilter();
 publishReceiveFilter();IOLockUnlock(lock);return ok?kIOReturnSuccess:kIOReturnIOError;
}
void RealtekVTDNextEthernet::publish(){
 const bool precheckEligible=mac46N64PrecheckEligible();
 mac46N64Precheck.write(*this,precheckEligible,precheckEligible&&enabled&&network.running&&network.status==NetworkStatus::Ok,generation);
 if(filterMACVersion==46){
  setProperty("MAC46FULLSchema",1,32);setProperty("MAC46FULLProfile",mac46Features.native64Ready?"MAC46-FULL-NOIOMMU":"MAC46-FULL-APPLEVTD-ON");
  setProperty("MAC46NOIOMMU1Schema",1,32);setProperty("MAC46FULLNative64Ready",mac46Features.native64Ready&&backend&&backend->native64NoIOMMUReady());
  setProperty("MAC46FULLDMAReady",mac46Features.dmaReady);setProperty("MAC46FULLBackendNative64Ready",backend&&backend->native64NoIOMMUReady());
  setProperty("MAC46FULLDMAAddressKind",mac46Features.native64Ready?"physical64":mac46Features.mapperReady?"mapper-IOVA64":"unavailable");
  setProperty("MAC46FULLIdentityEligible",mac46Features.identityEligible);setProperty("MAC46FULLHardwareInitialized",mac46Features.hardwareInitialized);
  setProperty("MAC46FULLMapperReady",mac46Features.mapperReady);setProperty("MAC46FULLResourcesReady",mac46Features.resourcesReady);
  setProperty("MAC46FULLTXChecksum",mac46Features.tx.enabled());setProperty("MAC46FULLRXChecksum",mac46Features.rx.enabled());
  setProperty("MAC46FULLTXAssistVerified",mac46Features.txAssist.configured);setProperty("MAC46FULLTXAssistStatus",uint32_t(mac46Features.txAssist.status),32);
  setProperty("MAC46FULLTSO4",mac46Features.tso4.enabled());setProperty("MAC46FULLTSO6",mac46Features.tso6.enabled());
  setProperty("MAC46FULLTSOMaxIP",mac46Features.limit,32);setProperty("MAC46FULLSoftwareAggregateMaxIP",65535,32);
  setProperty("MAC46FULLTSO4AssistVerified",mac46Features.assist4.configured);setProperty("MAC46FULLTSO6AssistVerified",mac46Features.assist6.configured);
  setProperty("MAC46FULLTSO4AssistStatus",uint32_t(mac46Features.assist4.status),32);setProperty("MAC46FULLTSO6AssistStatus",uint32_t(mac46Features.assist6.status),32);
  setProperty("MAC46FULLTSO4HardwareSubmitted",tso4Stats.hardwareSubmitted,64);setProperty("MAC46FULLTSO6HardwareSubmitted",tso6Stats.hardwareSubmitted,64);
  setProperty("MAC46FULLTSO4SoftwareAccepted",tso4Stats.softwareAccepted,64);setProperty("MAC46FULLTSO6SoftwareAccepted",tso6Stats.softwareAccepted,64);
  setProperty("MAC46FULLTSO4SoftwareCompleted",tso4Stats.softwareCompleted,64);setProperty("MAC46FULLTSO6SoftwareCompleted",tso6Stats.softwareCompleted,64);
  setProperty("MAC46FULLTSO4SoftwareSegments",tso4Stats.softwareSegments,64);setProperty("MAC46FULLTSO6SoftwareSegments",tso6Stats.softwareSegments,64);
  setProperty("MAC46FULLTSO4RequestErrors",tso4Stats.requestErrors,64);setProperty("MAC46FULLTSO6RequestErrors",tso6Stats.requestErrors,64);
  setProperty("MAC46FULLTXChecksum4Hardware",mac46Features.txHardware[0],64);setProperty("MAC46FULLTXChecksum6Hardware",mac46Features.txHardware[1],64);
  setProperty("MAC46FULLTXChecksum4Software",mac46Features.txSoftware[0],64);setProperty("MAC46FULLTXChecksum6Software",mac46Features.txSoftware[1],64);
  setProperty("MAC46FULLRXChecksum4Published",mac46Features.rxPublished[0],64);setProperty("MAC46FULLRXChecksum6Published",mac46Features.rxPublished[1],64);
  setProperty("MAC46FULLRXHardwareVerified",network.rxChecksumHardware.verified);setProperty("MAC46FULLRXHardwareActive",network.rxChecksumActive);
  setProperty("MAC46FULLVLANSoftware",true);setProperty("MAC46FULLVLANStripping",false);setProperty("MAC46FULLJumboReady",resources.jumboReady);
  setProperty("MAC46FULLTXRing",network.txCount,32);setProperty("MAC46FULLTXPool",backend?backend->txPoolCount():0,32);
  setProperty("MAC46FULLQueue",configuredOutputQueueCapacity,32);setProperty("MAC46FULLWireVerified",false);
  mac46EEE.write(*this);
 }

 if(mac46VTD.identityEligible()){
  mac46VTD.write(*this);
  setProperty("MAC46VTD1PoolShapeAvailable",backend&&backend->dmaReady());
  setProperty("MAC46VTD1RingCount",network.txCount,32);
  setProperty("MAC46VTD1PoolCount",backend?backend->txPoolCount():0,32);
  setProperty("MAC46VTD1PoolStride",backend?backend->txPoolStride():0,32);
  setProperty("MAC46VTD1PoolArenaBytes",backend?uint64_t(backend->txPoolCount())*backend->txPoolStride():0,64);
  if(mac46VTD.needsExport()){
   unsigned size=0;const auto bytes=mac46VTD.encode(size);auto data=OSData::withBytes(bytes,size);
   const bool stored=data&&setProperty("MAC46VTD1Snapshot",data);
   setProperty("MAC46VTD1SnapshotStored",stored);
   if(stored)mac46VTD.didExport();if(data)data->release();
  }
 }
 if(filterMACVersion==34){
  setProperty("MAC34TXQ1Schema",1,32);
  setProperty("MAC34TXQ1Requested",mac34TXQ1.requested);
  setProperty("MAC34TXQ1Active",mac34TXQ1.requested&&network.mac34Native64Verified);
  setProperty("MAC34TXQ1IRQDecisions",mac34TXQ1.decisions,64);
  setProperty("MAC34TXQ1DeferredParentIRQs",mac34TXQ1.deferredParent,64);
  setProperty("MAC34TXQ1ServiceAllowed",mac34TXQ1.serviceAllowed,64);
 }

 network.stopSafety.write(*this);setProperty("STOPSAFE1OwnerPinned",stopSafetyOwnerPinned);
 setProperty("RXFILL1Schema",1,32);setProperty("RXFILL1Requested",rxFillPolicy.enabled());setProperty("RXFILL1Active",rxFillActive());
 setProperty("RXFILL1Attempts",rxFillAttempts,64);setProperty("RXFILL1PacketsFilled",rxFillPacketsFilled,64);
 setProperty("RXFILL1AllocationFailures",rxFillAllocationFailures,64);setProperty("RXFILL1PreparationFailures",rxFillPreparationFailures,64);
 setProperty("RXNOWAIT1Schema",1,32);setProperty("RXNOWAIT1Requested",rxNoWaitPolicy.requested());
 setProperty("RXNOWAIT1Active",rxNoWaitActive());
 setProperty("RXNOWAIT1Attempts",rxNoWaitAttempts,64);setProperty("RXNOWAIT1Allocated",rxNoWaitAllocated,64);
 setProperty("RXNOWAIT1Failures",rxNoWaitFailures,64);
 setProperty("TXQOBS1Schema",1,32);setProperty("TXQOBS1Policy",txq&&txq->enabled());
 setProperty("TXPRECHECK1Schema",1,32);setProperty("TXPRECHECK1Requested",txPrecheckPolicy.enabled());
 setProperty("TXPRECHECK1Active",txPrecheckPolicy.enabled()&&backend&&backend->dmaReady()&&!backend->deviceMapperPresent());
 setProperty("TXPRECHECK1TSO4EarlyFull",txPrecheck4EarlyFull,64);
 setProperty("SINGLE1Schema",1,32);setProperty("SINGLE1Policy",single1Policy.enabled());
 setProperty("SINGLE1IPv4Submitted",single1Submitted4,64);setProperty("SINGLE1IPv6Submitted",single1Submitted6,64);
 // These are ordinary, software-finalized single frames, NOT HW TSO aggregates.

 // ECN1 changes DecisionECN semantics: only IP CE, not ECT(0)/ECT(1).
 setProperty("TSO4OPT1Schema",2,32);
 setProperty("TSO4ECN1Schema",1,32);
 setProperty("TSO4ECN1DecisionECNMeaning","CE-only");
 setProperty("TSO4ECN1WireVerified",false);
 setProperty("TSO4ECN1EvaluatedNotECT",tso4Stats.ecnEvaluations[0],64);
 setProperty("TSO4ECN1EvaluatedECT1",tso4Stats.ecnEvaluations[1],64);
 setProperty("TSO4ECN1EvaluatedECT0",tso4Stats.ecnEvaluations[2],64);
 setProperty("TSO4ECN1EvaluatedCE",tso4Stats.ecnEvaluations[3],64);
 setProperty("TSO4ECN1HardwareNotECT",tso4Stats.ecnHardwareSubmitted[0],64);
 setProperty("TSO4ECN1HardwareECT1",tso4Stats.ecnHardwareSubmitted[1],64);
 setProperty("TSO4ECN1HardwareECT0",tso4Stats.ecnHardwareSubmitted[2],64);
 setProperty("TSO4ECN1HardwareCE",tso4Stats.ecnHardwareSubmitted[3],64);
 setProperty("TSO4OPT1Evaluations",tso4Stats.hardwareEvaluations,64);
 setProperty("TSO4OPT1PolicyOff",tso4Stats.hardwarePolicyOff,64);
 setProperty("TSO4OPT1RegularPacketWaits",tso4Stats.regularPacketWaits,64);
 setProperty("TSO4OPT1HardwareWithTCPOptions",tso4Stats.hardwareWithTCPOptions,64);
 setProperty("TSO4OPT1HardwareWithPSH",tso4Stats.hardwareWithPSH,64);
 char tsoReasonKey[64];
 for(unsigned i=0;i<unsigned(TSO4HardwareReason::Count);++i){
  snprintf(tsoReasonKey,sizeof(tsoReasonKey),"TSO4OPT1Decision%s",tso4HardwareReasonName(TSO4HardwareReason(i)));
  setProperty(tsoReasonKey,tso4Stats.hardwareReasons[i],64);
 }
 setProperty("TSO4HardwarePolicy",tso4Policy.enabled());setProperty("TSO4AssistConfigured",tso4Assist.configured);setProperty("TSO4AssistEnabled",tso4Assist.enabled);setProperty("TSO4AssistStatus",uint32_t(tso4Assist.status),32);
 setProperty("TSO4IfnetOffload",tso4Assist.afterAssist,32);setProperty("TSO4IfnetCapabilities",tso4Assist.afterSupported,32);setProperty("TSO4IfnetEnabled",tso4Assist.afterEnabled,32);setProperty("TSO4LimitIPBytes",tso4Assist.afterLimit,32);
 setProperty("TSO4RollbackAttempted",tso4Assist.rollbackAttempted);setProperty("TSO4RollbackOK",tso4Assist.rollbackOK);setProperty("TSO4PriorLimitKnown",tso4Assist.priorLimitKnown);
 setProperty("TSO4Requests",tso4Stats.requests,64);setProperty("TSO4HardwareSubmitted",tso4Stats.hardwareSubmitted,64);setProperty("TSO4SoftwareAccepted",tso4Stats.softwareAccepted,64);setProperty("TSO4SoftwareSegments",tso4Stats.softwareSegments,64);setProperty("TSO4SoftwareCompleted",tso4Stats.softwareCompleted,64);
 setProperty("TSO4MetadataErrors",tso4Stats.metadataErrors,64);setProperty("TSO4RequestErrors",tso4Stats.requestErrors,64);setProperty("TSO4Retries",tso4Stats.retries,64);setProperty("TSO4PartialFailures",tso4Stats.partialFailures,64);setProperty("TSO4Cancelled",tso4Stats.cancelled,64);
 setProperty("TSO4Pending",tso4Software.active());setProperty("TSO4PendingNext",tso4Software.next,32);setProperty("TSO4PendingOutstanding",tso4Software.outstanding,32);setProperty("TSO4WireVerified",false);
 setProperty("TSO6Schema",1,32);setProperty("TSO6Policy",tso6Policy.enabled());setProperty("TSO6WireVerified",false);
 setProperty("TSO6Requests",tso6Stats.requests,64);setProperty("TSO6HardwareSubmitted",tso6Stats.hardwareSubmitted,64);setProperty("TSO6SoftwareAccepted",tso6Stats.softwareAccepted,64);setProperty("TSO6SoftwareSegments",tso6Stats.softwareSegments,64);setProperty("TSO6SoftwareCompleted",tso6Stats.softwareCompleted,64);
 setProperty("TSO6MetadataErrors",tso6Stats.metadataErrors,64);setProperty("TSO6RequestErrors",tso6Stats.requestErrors,64);setProperty("TSO6Retries",tso6Stats.retries,64);setProperty("TSO6RegularPacketWaits",tso6Stats.regularPacketWaits,64);setProperty("TSO6PartialFailures",tso6Stats.partialFailures,64);setProperty("TSO6Cancelled",tso6Stats.cancelled,64);
 setProperty("TSO6Evaluations",tso6Stats.hardwareEvaluations,64);setProperty("TSO6PolicyOff",tso6Stats.hardwarePolicyOff,64);setProperty("TSO6HardwareWithTCPOptions",tso6Stats.hardwareWithTCPOptions,64);setProperty("TSO6HardwareWithPSH",tso6Stats.hardwareWithPSH,64);
 for(unsigned j=0;j<unsigned(TSO6HardwareReason::Count);++j){char key[64];snprintf(key,sizeof(key),"TSO6Decision%s",tso6HardwareReasonName(TSO6HardwareReason(j)));setProperty(key,tso6Stats.hardwareReasons[j],64);}
 const char *ecn6Names[]={"NotECT","ECT1","ECT0","CE"};for(unsigned j=0;j<4;++j){char key[64];snprintf(key,sizeof(key),"TSO6ECNEvaluated%s",ecn6Names[j]);setProperty(key,tso6Stats.ecnEvaluations[j],64);snprintf(key,sizeof(key),"TSO6ECNHardware%s",ecn6Names[j]);setProperty(key,tso6Stats.ecnHardwareSubmitted[j],64);}
 setProperty("TSO6Pending",tso6Software.active());setProperty("TSO6PendingNext",tso6Software.next,32);setProperty("TSO6PendingOutstanding",tso6Software.outstanding,32);
 setProperty("TSO6AssistConfigured",tso6Assist.configured);setProperty("TSO6AssistStatus",uint32_t(tso6Assist.status),32);setProperty("TSO6IfnetOffload",tso6Assist.afterAssist,32);setProperty("TSO6IfnetCapabilities",tso6Assist.afterSupported,32);setProperty("TSO6IfnetEnabled",tso6Assist.afterEnabled,32);setProperty("TSO6Limit",tso6Assist.afterLimit,32);setProperty("TSO6RollbackAttempted",tso6Assist.rollbackAttempted);setProperty("TSO6RollbackOK",tso6Assist.rollbackOK);
 setProperty("CSUM6Schema",1,32);setProperty("CSUM6Policy",csum6Policy.enabled());setProperty("CSUM6WireVerified",false);
 setProperty("CSUM6TXRequests",tx6Requests,64);setProperty("CSUM6TXHardware",tx6Hardware,64);setProperty("CSUM6TXSoftware",tx6Software,64);setProperty("CSUM6TXErrors",tx6Errors,64);
 setProperty("CSUM6RXObservations",network.rxChecksumStats.ipv6Observations,64);setProperty("CSUM6RXTCPResults",network.rxChecksumStats.tcp6Results,64);setProperty("CSUM6RXUDPResults",network.rxChecksumStats.udp6Results,64);
 setProperty("CSUM6RXPublished",rx6Published,64);setProperty("CSUM6RXPublishErrors",rx6PublishErrors,64);
 setProperty("CSUM6HardwareActive",csum6Policy.enabled()&&network.rxChecksumActive);setProperty("CSUM6RXRequested",network.rxChecksum6Requested);
 setProperty("RXCSUM1Schema",2,32);setProperty("RXCSUM2ProtocolScopedFailures",true);setProperty("RXCSUM1WireVerified",false);
 setProperty("RXCSUM1Policy",rxChecksumPolicy.enabled());setProperty("RXCSUM1HardwareActive",network.rxChecksumActive);
 setProperty("RXCSUM1HardwareAttempted",network.rxChecksumHardware.attempted);setProperty("RXCSUM1HardwareVerified",network.rxChecksumHardware.verified);
 setProperty("RXCSUM1CPlusWritten",network.rxChecksumHardware.written,16);setProperty("RXCSUM1CPlusRead",network.rxChecksumHardware.read,16);setProperty("RXCSUM1CPlusReadValid",network.rxChecksumHardware.readValid);
 setProperty("RXCSUM1Observations",network.rxChecksumStats.observations,64);setProperty("RXCSUM1IPResults",network.rxChecksumStats.ipResults,64);setProperty("RXCSUM1TCPResults",network.rxChecksumStats.tcpResults,64);setProperty("RXCSUM1UDPResults",network.rxChecksumStats.udpResults,64);
 setProperty("RXCSUM1Published",rxChecksumPublished,64);setProperty("RXCSUM1PublishErrors",rxChecksumPublishErrors,64);setProperty("RXCSUM1Cleared",rxChecksumCleared,64);
 const char *rxReasons[]={"Off","Descriptor","Encapsulation","IPv6","Header","Options","Fragment","Length","Protocol","Contradiction","IPFailed","TransportFailed","UDPZero","Eligible"};
 static_assert(sizeof(rxReasons)/sizeof(*rxReasons)==unsigned(RXChecksumReason::Count),"RX reason schema");
 for(unsigned i=0;i<unsigned(RXChecksumReason::Count);++i){char key[64];snprintf(key,sizeof(key),"RXCSUM1Reason%s",rxReasons[i]);setProperty(key,network.rxChecksumStats.reasons[i],64);snprintf(key,sizeof(key),"CSUM6RXReason%s",rxReasons[i]);setProperty(key,network.rxChecksumStats.ipv6Reasons[i],64);}
 if(filterMACVersion==44){
   setProperty("MAC44VLANTSO3Schema",1,32);setProperty("MAC44VLANTSO3SoftwareIPv6VLANInputMax",kMAC44VLAN6SoftwareIPMax,32);setProperty("MAC44VLANTSO3SoftwareAccepted",mac44TSO6.vlanSoftwareAccepted,64);setProperty("MAC44VLANTSO3MaxAcceptedIPBytes",mac44TSO6.vlanSoftwareMaxIPBytes,64);
   setProperty("MAC44VLANTSO2Schema",1,32);setProperty("MAC44VLANTSO2SoftwareIPv4VLANInputMax",kMAC44VLANSoftwareIPMax,32);setProperty("MAC44VLANTSO2SoftwareAccepted",mac44TSO4.vlanSoftwareAccepted,64);setProperty("MAC44VLANTSO2MaxAcceptedIPBytes",mac44TSO4.vlanSoftwareMaxIPBytes,64);
   setProperty("MAC44VLANTSO1Schema",1,32);setProperty("MAC44VLANTSO1DiagnosticOnly",true);setProperty("MAC44VLANTSO1Words",VlanTSOFailure::Words,32);setProperty("MAC44VLANTSO1Sites",VlanTSOFailure::SiteCount,32);setProperty("MAC44VLANTSO1PerSite",VlanTSOFailure::PerSite,32);
  setProperty("MAC44VLANTSO1IPv4Total",tso4Stats.vlanFailure.total,64);setProperty("MAC44VLANTSO1IPv4Stored",tso4Stats.vlanFailure.stored,64);setProperty("MAC44VLANTSO1IPv4Suppressed",tso4Stats.vlanFailure.suppressed,64);
  setProperty("MAC44VLANTSO1IPv6Total",tso6Stats.vlanFailure.total,64);setProperty("MAC44VLANTSO1IPv6Stored",tso6Stats.vlanFailure.stored,64);setProperty("MAC44VLANTSO1IPv6Suppressed",tso6Stats.vlanFailure.suppressed,64);
  setProperty("MAC44VLANTSO1IPv4Counts",tso4Stats.vlanFailure.counts,sizeof(tso4Stats.vlanFailure.counts));setProperty("MAC44VLANTSO1IPv6Counts",tso6Stats.vlanFailure.counts,sizeof(tso6Stats.vlanFailure.counts));
  setProperty("MAC44VLANTSO1IPv4Samples",tso4Stats.vlanFailure.samples,sizeof(tso4Stats.vlanFailure.samples));setProperty("MAC44VLANTSO1IPv6Samples",tso6Stats.vlanFailure.samples,sizeof(tso6Stats.vlanFailure.samples));
   setProperty("MAC44NOIOMMU1Schema",1,32);setProperty("MAC44NOIOMMU1ModeVerified",network.mac44Native64ModeVerified);
  setProperty("MAC44NOIOMMU1BackendReady",backend&&backend->dmaReady());setProperty("MAC44NOIOMMU1BackendNative64Ready",backend&&backend->native64NoIOMMUReady());
  setProperty("MAC44NOIOMMU1MapperPresent",backend&&backend->deviceMapperPresent());setProperty("MAC44NOIOMMU1WireVerified",false);
  setProperty("MAC44TSO6Schema",1,32);setProperty("MAC44TSO6Experimental",true);setProperty("MAC44TSO6Requested",mac44TSO6.requested);
  setProperty("MAC44TSO6IdentityEligible",mac44TSO6.identityEligible);setProperty("MAC44TSO6MapperReady",mac44TSO6.mapperReady);setProperty("MAC44TSO6DMAReady",mac44TSO6.dmaReady);setProperty("MAC44TSO6Native64Ready",mac44TSO6.native64Ready);setProperty("MAC44TSO6HardwareInitialized",mac44TSO6.hardwareInitialized);setProperty("MAC44TSO6ResourcesReady",mac44TSO6.resourcesReady);
  setProperty("MAC44TSO6Ready",mac44TSO6.ready());setProperty("MAC44TSO6Declared",mac44TSO6.enabled());setProperty("MAC44TSO6Active",enabled&&mac44TSO6.enabled());setProperty("MAC44TSO6MaxIPBytes",mac44TSO6.limit,32);setProperty("MAC44TSO6HardwareMaxSegments",kMAC44TSO4HardwareSegments,32);
  setProperty("MAC44TSO6IfnetAttempted",mac44TSO6.assist.attempted);setProperty("MAC44TSO6IfnetConfigured",mac44TSO6.assist.configured);setProperty("MAC44TSO6IfnetEnabled",mac44TSO6.assist.enabled);setProperty("MAC44TSO6IfnetStatus",uint32_t(mac44TSO6.assist.status),32);setProperty("MAC44TSO6IfnetLimit",mac44TSO6.assist.afterLimit,32);
  setProperty("MAC44TSO6IfnetOffload",mac44TSO6.assist.afterAssist,32);setProperty("MAC44TSO6IfnetSupported",mac44TSO6.assist.afterSupported,32);setProperty("MAC44TSO6IfnetCapabilities",mac44TSO6.assist.afterEnabled,32);setProperty("MAC44TSO6RollbackAttempted",mac44TSO6.assist.rollbackAttempted);setProperty("MAC44TSO6RollbackOK",mac44TSO6.assist.rollbackOK);
  setProperty("MAC44TSO6Requests",tso6Stats.requests,64);setProperty("MAC44TSO6HardwareSubmitted",tso6Stats.hardwareSubmitted,64);setProperty("MAC44TSO6SoftwareAccepted",tso6Stats.softwareAccepted,64);setProperty("MAC44TSO6SoftwareSegments",tso6Stats.softwareSegments,64);setProperty("MAC44TSO6SoftwareCompleted",tso6Stats.softwareCompleted,64);
  setProperty("MAC44TSO6Retries",tso6Stats.retries,64);setProperty("MAC44TSO6MetadataErrors",tso6Stats.metadataErrors,64);setProperty("MAC44TSO6RequestErrors",tso6Stats.requestErrors,64);setProperty("MAC44TSO6PartialFailures",tso6Stats.partialFailures,64);setProperty("MAC44TSO6Cancelled",tso6Stats.cancelled,64);
  setProperty("MAC44TSO6Pending",tso6Software.active());setProperty("MAC44TSO6PendingNext",tso6Software.next,32);setProperty("MAC44TSO6PendingOutstanding",tso6Software.outstanding,32);setProperty("MAC44TSO6PendingOwned",tso6Software.owned);setProperty("MAC44TSO6SingleSubmitted",single1Submitted6,64);
  setProperty("MAC44TSO6AggregateRejected",mac44TSO6.aggregateRejected,64);setProperty("MAC44TSO6SegmentLimitFallback",mac44TSO6.segmentLimitFallback,64);setProperty("MAC44TSO6PostOwnErrors",mac44TSO6.postOwnErrors,64);
  for(unsigned i=0;i<unsigned(TSO6HardwareReason::Count);++i){char key[64];snprintf(key,sizeof(key),"MAC44TSO6Reason%s",tso6HardwareReasonName(TSO6HardwareReason(i)));setProperty(key,tso6Stats.hardwareReasons[i],64);}
  setProperty("MAC44TSO6WireVerified",false);
  setProperty("MAC44TSO6IfnetFailures",mac44TSO6.ifnetFailures,64);setProperty("MAC44TSO6OffFallbackConfigured",mac44TSO6.disableFallback.configured);
  setProperty("MAC44TSO6MaxIPIncludesIPv6Header",true);setProperty("MAC44TSO6HardwareWithTCPOptions",tso6Stats.hardwareWithTCPOptions,64);setProperty("MAC44TSO6HardwareWithPSH",tso6Stats.hardwareWithPSH,64);
  for(unsigned i=0;i<4;++i){char key[64];snprintf(key,sizeof(key),"MAC44TSO6ECN%uHardwareSubmitted",i);setProperty(key,tso6Stats.ecnHardwareSubmitted[i],64);}
  setProperty("MAC44TSO4Schema",1,32);setProperty("MAC44TSO4Experimental",true);setProperty("MAC44TSO4Requested",mac44TSO4.requested);
  setProperty("MAC44TSO4IdentityEligible",mac44TSO4.identityEligible);setProperty("MAC44TSO4MapperReady",mac44TSO4.mapperReady);setProperty("MAC44TSO4DMAReady",mac44TSO4.dmaReady);setProperty("MAC44TSO4Native64Ready",mac44TSO4.native64Ready);setProperty("MAC44TSO4HardwareInitialized",mac44TSO4.hardwareInitialized);setProperty("MAC44TSO4ResourcesReady",mac44TSO4.resourcesReady);
  setProperty("MAC44TSO4Ready",mac44TSO4.ready());setProperty("MAC44TSO4Declared",mac44TSO4.enabled());setProperty("MAC44TSO4Active",enabled&&mac44TSO4.enabled());setProperty("MAC44TSO4MaxIPBytes",mac44TSO4.limit,32);setProperty("MAC44TSO4HardwareMaxSegments",kMAC44TSO4HardwareSegments,32);
  setProperty("MAC44TSO4IfnetAttempted",mac44TSO4.assist.attempted);setProperty("MAC44TSO4IfnetConfigured",mac44TSO4.assist.configured);setProperty("MAC44TSO4IfnetEnabled",mac44TSO4.assist.enabled);setProperty("MAC44TSO4IfnetStatus",uint32_t(mac44TSO4.assist.status),32);setProperty("MAC44TSO4IfnetLimit",mac44TSO4.assist.afterLimit,32);
  setProperty("MAC44TSO4IfnetOffload",mac44TSO4.assist.afterAssist,32);setProperty("MAC44TSO4IfnetSupported",mac44TSO4.assist.afterSupported,32);setProperty("MAC44TSO4IfnetCapabilities",mac44TSO4.assist.afterEnabled,32);setProperty("MAC44TSO4RollbackAttempted",mac44TSO4.assist.rollbackAttempted);setProperty("MAC44TSO4RollbackOK",mac44TSO4.assist.rollbackOK);
  setProperty("MAC44TSO4Requests",tso4Stats.requests,64);setProperty("MAC44TSO4HardwareSubmitted",tso4Stats.hardwareSubmitted,64);setProperty("MAC44TSO4SoftwareAccepted",tso4Stats.softwareAccepted,64);setProperty("MAC44TSO4SoftwareSegments",tso4Stats.softwareSegments,64);setProperty("MAC44TSO4SoftwareCompleted",tso4Stats.softwareCompleted,64);
  setProperty("MAC44TSO4Retries",tso4Stats.retries,64);setProperty("MAC44TSO4MetadataErrors",tso4Stats.metadataErrors,64);setProperty("MAC44TSO4RequestErrors",tso4Stats.requestErrors,64);setProperty("MAC44TSO4PartialFailures",tso4Stats.partialFailures,64);setProperty("MAC44TSO4Cancelled",tso4Stats.cancelled,64);
  setProperty("MAC44TSO4Pending",tso4Software.active());setProperty("MAC44TSO4PendingNext",tso4Software.next,32);setProperty("MAC44TSO4PendingOutstanding",tso4Software.outstanding,32);setProperty("MAC44TSO4PendingOwned",tso4Software.owned);setProperty("MAC44TSO4SingleSubmitted",single1Submitted4,64);
  setProperty("MAC44TSO4AggregateRejected",mac44TSO4.aggregateRejected,64);setProperty("MAC44TSO4SegmentLimitFallback",mac44TSO4.segmentLimitFallback,64);setProperty("MAC44TSO4PostOwnErrors",mac44TSO4.postOwnErrors,64);
  for(unsigned i=0;i<unsigned(TSO4HardwareReason::Count);++i){char key[64];snprintf(key,sizeof(key),"MAC44TSO4Reason%s",tso4HardwareReasonName(TSO4HardwareReason(i)));setProperty(key,tso4Stats.hardwareReasons[i],64);}
  setProperty("MAC44TSO4WireVerified",false);
 setProperty("MAC44RXCSUM1Schema",1,32);setProperty("MAC44RXCSUM1Requested",mac44RX.requested);
  setProperty("MAC44RXCSUM1IdentityEligible",mac44RX.identityEligible);setProperty("MAC44RXCSUM1MapperReady",mac44RX.mapperReady);setProperty("MAC44RXCSUM1DMAReady",mac44RX.dmaReady);setProperty("MAC44RXCSUM1Native64Ready",mac44RX.native64Ready);
  setProperty("MAC44RXCSUM1HardwareInitialized",mac44RX.hardwareInitialized);setProperty("MAC44RXCSUM1Declared",mac44RX.declared());
  setProperty("MAC44RXCSUM1Active",enabled&&mac44RX.publishable(network.rxChecksumActive));
  setProperty("MAC44RXCSUM1HardwareEnableRequested",(network.rxChecksumRequested||network.rxChecksum6Requested)&&network.mtu==1500&&network.rxChecksumProfile==RXChecksumProfile::MAC44RevisionCDeviceMapper);
  setProperty("MAC44RXCSUM1HardwareAttempted",network.rxChecksumProfile==RXChecksumProfile::MAC44RevisionCDeviceMapper&&network.rxChecksumHardware.attempted);
  setProperty("MAC44RXCSUM1HardwareVerified",network.rxChecksumProfile==RXChecksumProfile::MAC44RevisionCDeviceMapper&&network.rxChecksumHardware.verified);
  setProperty("MAC44RXCSUM1HardwareActive",network.rxChecksumProfile==RXChecksumProfile::MAC44RevisionCDeviceMapper&&network.rxChecksumActive);
  setProperty("MAC44RXCSUM1CPlusReadValid",network.rxChecksumHardware.readValid);setProperty("MAC44RXCSUM1CPlusWritten",network.rxChecksumHardware.written,16);setProperty("MAC44RXCSUM1CPlusRead",network.rxChecksumHardware.read,16);
  setProperty("MAC44RXCSUM1HardwareStatus",unsigned(network.mac44RXHardware.status),32);
  setProperty("MAC44RXCSUM1HardwareAttempts",network.mac44RXHardwareAttempts,64);setProperty("MAC44RXCSUM1HardwareFailures",network.mac44RXHardwareFailures,64);
  setProperty("MAC44RXCSUM1HardwareOffAttempted",network.mac44RXHardware.offAttempted);setProperty("MAC44RXCSUM1HardwareOffVerified",network.mac44RXHardware.offVerified);
  setProperty("MAC44RXCSUM1HardwareOffReadValid",network.mac44RXHardware.offReadValid);setProperty("MAC44RXCSUM1HardwareOffRead",network.mac44RXHardware.offRead,16);
  setProperty("MAC44RXCSUM1IfnetAttempted",mac44RX.assist.attempted);setProperty("MAC44RXCSUM1IfnetConfigured",mac44RX.assist.configured);setProperty("MAC44RXCSUM1IfnetRXEnabled",mac44RX.assist.enabled);
  setProperty("MAC44RXCSUM1IfnetStatus",uint32_t(mac44RX.assist.status),32);setProperty("MAC44RXCSUM1IfnetOffload",mac44RX.assist.afterAssist,32);setProperty("MAC44RXCSUM1IfnetSupported",mac44RX.assist.afterSupported,32);setProperty("MAC44RXCSUM1IfnetEnabled",mac44RX.assist.afterEnabled,32);
  setProperty("MAC44RXCSUM1IfnetRollbackAttempted",mac44RX.assist.rollbackAttempted);setProperty("MAC44RXCSUM1IfnetRollbackOK",mac44RX.assist.rollbackOK);setProperty("MAC44RXCSUM1IfnetFailures",mac44RX.ifnetFailures,64);
  setProperty("MAC44RXCSUM1IfnetOffFallbackConfigured",mac44RX.disableFallback.configured);
  const auto &r=network.rxChecksumStats;
  setProperty("MAC44RXCSUM1IPv4Observations",network.mac44RXObserved4,64);setProperty("MAC44RXCSUM1IPv6Observations",network.mac44RXObserved6,64);setProperty("MAC44RXCSUM1OtherObservations",network.mac44RXObservedOther,64);setProperty("MAC44RXCSUM1DecodedObservations",r.observations,64);
  setProperty("MAC44RXCSUM1IPResults",r.ipResults,64);setProperty("MAC44RXCSUM1TCPResults",r.tcpResults,64);setProperty("MAC44RXCSUM1UDPResults",r.udpResults,64);setProperty("MAC44RXCSUM1TCP6Results",r.tcp6Results,64);setProperty("MAC44RXCSUM1UDP6Results",r.udp6Results,64);
  const char *names[]={"IP","TCP","UDP","TCP6","UDP6"};for(unsigned i=0;i<5;++i){char key[64];snprintf(key,sizeof(key),"MAC44RXCSUM1Published%s",names[i]);setProperty(key,mac44RX.published[i],64);}
  setProperty("MAC44RXCSUM1IPv4Published",rxChecksumPublished,64);setProperty("MAC44RXCSUM1IPv6Published",rx6Published,64);setProperty("MAC44RXCSUM1IPv4PublishErrors",rxChecksumPublishErrors,64);setProperty("MAC44RXCSUM1IPv6PublishErrors",rx6PublishErrors,64);setProperty("MAC44RXCSUM1Cleared",rxChecksumCleared,64);
  for(unsigned i=0;i<unsigned(RXChecksumReason::Count);++i){char key[64];snprintf(key,sizeof(key),"MAC44RXCSUM1Reason%s",rxReasons[i]);setProperty(key,r.reasons[i],64);snprintf(key,sizeof(key),"MAC44RXCSUM1IPv6Reason%s",rxReasons[i]);setProperty(key,r.ipv6Reasons[i],64);}
  setProperty("MAC44RXCSUM1WireVerified",false);
 }
 setProperty("MAC44TXCSUM1Schema",1,32);setProperty("MAC44TXCSUM1Requested",mac44Checksum.requested);
 setProperty("MAC44TXCSUM1IdentityEligible",mac44Checksum.identityEligible);setProperty("MAC44TXCSUM1MapperReady",mac44Checksum.mapperReady);setProperty("MAC44TXCSUM1DMAReady",mac44Checksum.dmaReady);setProperty("MAC44TXCSUM1Native64Ready",mac44Checksum.native64Ready);
 setProperty("MAC44TXCSUM1HardwareInitialized",mac44Checksum.hardwareInitialized);setProperty("MAC44TXCSUM1Declared",mac44Checksum.enabled());
 setProperty("MAC44TXCSUM1IfnetAttempted",mac44Checksum.assist.attempted);setProperty("MAC44TXCSUM1IfnetConfigured",mac44Checksum.assist.configured);
 setProperty("MAC44TXCSUM1Active",enabled&&mac44Checksum.enabled()&&mac44Checksum.assist.configured);
 setProperty("MAC44TXCSUM1IfnetStatus",uint32_t(mac44Checksum.assist.status),32);setProperty("MAC44TXCSUM1IfnetOffload",mac44Checksum.assist.afterAssist,32);
 setProperty("MAC44TXCSUM1IfnetSupported",mac44Checksum.assist.afterSupported,32);setProperty("MAC44TXCSUM1IfnetEnabled",mac44Checksum.assist.afterEnabled,32);
 setProperty("MAC44TXCSUM1RollbackAttempted",mac44Checksum.assist.rollbackAttempted);setProperty("MAC44TXCSUM1RollbackOK",mac44Checksum.assist.rollbackOK);
 setProperty("MAC44TXCSUM1IPv4Attempts",mac44Checksum.requests[0],64);setProperty("MAC44TXCSUM1IPv6Attempts",mac44Checksum.requests[1],64);
 setProperty("MAC44TXCSUM1IPv4HardwareSubmitted",mac44Checksum.hardware[0],64);setProperty("MAC44TXCSUM1IPv6HardwareSubmitted",mac44Checksum.hardware[1],64);
 setProperty("MAC44TXCSUM1IPv4SoftwareSubmitted",mac44Checksum.software[0],64);setProperty("MAC44TXCSUM1IPv6SoftwareSubmitted",mac44Checksum.software[1],64);
 setProperty("MAC44TXCSUM1RequestErrors",mac44Checksum.errors,64);setProperty("MAC44TXCSUM1MetadataErrors",mac44Checksum.metadataErrors,64);setProperty("MAC44TXCSUM1MappingErrors",mac44Checksum.mappingErrors,64);
 const char *mac44Reasons[]={"None","Policy","Encapsulation","Jumbo","Options","Fragment","Padding","Seed","Other"};
 for(unsigned i=0;i<9;++i){char key[64];snprintf(key,sizeof(key),"MAC44TXCSUM1Fallback%s",mac44Reasons[i]);setProperty(key,mac44Checksum.fallback[i],64);}
 setProperty("MAC44TXCSUM1WireVerified",false);
 setProperty("TXCSUM1AssistConfigured",txChecksumAssist.configured);setProperty("TXCSUM1AssistStatus",uint32_t(txChecksumAssist.status),32);
 setProperty("TXCSUM1IfnetOffload",txChecksumAssist.afterAssist,32);setProperty("TXCSUM1IfnetCapabilities",txChecksumAssist.afterSupported,32);setProperty("TXCSUM1IfnetEnabled",txChecksumAssist.afterEnabled,32);
 setProperty("TXCSUM1AssistRollbackAttempted",txChecksumAssist.rollbackAttempted);setProperty("TXCSUM1AssistRollbackOK",txChecksumAssist.rollbackOK);
 setProperty("TXCSUM1HardwarePolicy",txChecksumPolicy.enabled());setProperty("TXCSUM1TXIPv4Mask",txChecksumPolicy.enabled()?kChecksumIP|kChecksumTCP|kChecksumUDP:0,32);
 setProperty("TXCSUM1RequestAttempts",txChecksumStats.requestAttempts,64);setProperty("TXCSUM1HardwareScheduled",txChecksumStats.hardwareScheduled,64);
 setProperty("TXCSUM1SoftwareCompletedAttempts",txChecksumStats.softwareCompletedAttempts,64);setProperty("TXCSUM1SoftwareSubmitted",txChecksumStats.softwareSubmitted,64);
 setProperty("TXCSUM1RequestErrors",txChecksumStats.requestErrors,64);setProperty("TXCSUM1MetadataErrors",txChecksumStats.metadataErrors,64);
 setProperty("TXCSUM1WireEffectVerified",false);setProperty("TXCSUM1RXEnabled",rxChecksumPolicy.enabled());setProperty("TXCSUM1TSOEnabled",tso4Policy.enabled());
 if(filterMACVersion==34){
  setProperty("MAC34Schema",1,32);setProperty("MAC34IdentityEligible",mac34.identity);setProperty("MAC34HardwareInitialized",mac34.initialized);setProperty("MAC34Native64Ready",mac34.nativeReady);setProperty("MAC34MapperReady",false);setProperty("MAC34TSOResourcesReady",mac34.resources);
  setProperty("MAC34TXChecksumRequested",mac34.txRequested);setProperty("MAC34TXChecksumDeclared",mac34.tx.enabled());setProperty("MAC34TXHardwareTCPOnly",true);setProperty("MAC34TXHardwareActive",mac34.tx.enabled()&&network.mtu==1500);setProperty("MAC34TXIfnetConfigured",mac34.txAssist.configured);setProperty("MAC34TXIfnetStatus",uint32_t(mac34.txAssist.status),32);setProperty("MAC34TXRollbackOK",mac34.txAssist.rollbackOK);
  setProperty("MAC34RXChecksumRequested",mac34.rxRequested);setProperty("MAC34RXChecksumDeclared",mac34.rx.enabled());setProperty("MAC34RXHardwareActive",network.rxChecksumActive);setProperty("MAC34RXHardwareVerified",network.rxChecksumHardware.verified);setProperty("MAC34RXHardwareAttempts",network.mac34RXHardwareAttempts,64);setProperty("MAC34RXHardwareFailures",network.mac34RXHardwareFailures,64);
  setProperty("MAC34RXIPv4IPPublished",mac34.rxPublished[0],64);setProperty("MAC34RXIPv4TCPPublished",mac34.rxPublished[1],64);setProperty("MAC34RXIPv4UDPPublished",mac34.rxPublished[2],64);setProperty("MAC34RXIPv6TCPPublished",mac34.rxPublished[3],64);setProperty("MAC34RXIPv6UDPPublished",mac34.rxPublished[4],64);
  setProperty("MAC34TXIPv4HardwareSubmitted",mac34.txHardware[0],64);setProperty("MAC34TXIPv6HardwareSubmitted",mac34.txHardware[1],64);setProperty("MAC34TXIPv4SoftwareSubmitted",mac34.txSoftware[0],64);setProperty("MAC34TXIPv6SoftwareSubmitted",mac34.txSoftware[1],64);
  setProperty("MAC34TSO4SoftwareDeclared",mac34.tso4.enabled());setProperty("MAC34TSO6SoftwareDeclared",mac34.tso6.enabled());setProperty("MAC34TSO4HardwareEnabled",false);setProperty("MAC34TSO6HardwareEnabled",false);setProperty("MAC34TSO4IfnetConfigured",mac34.assist4.configured);setProperty("MAC34TSO6IfnetConfigured",mac34.assist6.configured);setProperty("MAC34TSOIfnetIPLimit",9000,32);setProperty("MAC34TSOSoftwareInputIPMax",65535,32);setProperty("MAC34TSOSoftwareSegmentsMax",64,32);
  setProperty("MAC34RXSTATStatus","DEFERRED_FRAMEWORK_BSD_ACCOUNTING");
  unsigned live=0;for(unsigned j=0;j<network.txCount;++j)live+=network.cookies[j]!=nullptr;
  const bool counted=network.txCompleted<=network.txSubmitted&&network.mac34TXCancelled<=network.txSubmitted-network.txCompleted;
  const uint64_t pending=counted?network.txSubmitted-network.txCompleted-network.mac34TXCancelled:0;
  setProperty("MAC34TXCancelledOnSafeStop",network.mac34TXCancelled,64);setProperty("MAC34TXLiveCookies",live,32);setProperty("MAC34TXAccountingValid",counted&&pending==live);setProperty("MAC34TXOutstandingByAccounting",pending,64);
 }
 setProperty("ExperimentalBuild",true);setProperty("RXSTAT1Status","DEFERRED");
 setProperty("VLJ1MTU",network.mtu,32);setProperty("VLJ1MTUState",mtuStateName(mtuTransaction.state));
 setProperty("VLJ1PauseSaved",mtuTransaction.savedPause,16);setProperty("VLJ1PauseSavedValid",mtuTransaction.savedValid);
 setProperty("VLJ1PausePolicyApplied",mtuTransaction.policyApplied);setProperty("VLJ1ANPending",mtuTransaction.awaitingAutoneg);
 setProperty("VLJ1ANTransitionSeen",mtuTransaction.transitionSeen);setProperty("VLJ1MTUSequence",mtuTransaction.sequence,64);
 setProperty("VLJ1MTUFailures",mtuTransaction.failures,64);
 setProperty("VLJ1MTUQueueFlushed",mtuQueueFlushed,64);setProperty("VLJ1MTUCancelledTXCookies",mtuCancelledCookies,64);
 setProperty("VLJ1RXBufferBytes",network.bufferSize,32);setProperty("VLJ1SoftwareVLAN",(getFeatures()&kIONetworkFeatureSoftwareVlan)!=0);setProperty("VLJ1HardwareVLAN",false);
 publishReceiveFilter();
 setProperty("P12TXSchema",2,32);setProperty("P12TXSampleCapacity",TXRingDiagnostic::Capacity,32);
 setProperty("P12TXSampleWords",TXRingDiagnostic::Words,32);setProperty("P12TXSampleCount",txRingDiag.count,32);
 setProperty("P13TXSampleTotal",txRingDiag.total,64);setProperty("P13TXSampleOverwritten",txRingDiag.overwritten,64);
 setProperty("P13TXDriverFlushCalls",TXRingDiagnostic::read(txRingDiag.flushCalls),64);
 setProperty("P13TXDriverFlushPackets",TXRingDiagnostic::read(txRingDiag.flushPackets),64);
 setProperty("P13TXSampleMode","rolling-last32");setProperty("P13TXRingAB",configuredTXCount==256);
 setProperty("P12TXSuppressed",txRingDiag.suppressed,64);setProperty("P12TXPoolFull",txRingDiag.poolFull,64);
 setProperty("P12TXCapacityFull",txRingDiag.capacityFull,64);setProperty("P12TXOWNFull",txRingDiag.ownFull,64);
 setProperty("P12TXReclaimPasses",txRingDiag.irqPasses,64);setProperty("P12TXIRQPasses",txRingDiag.txIRQPasses,64);
 setProperty("P12TXReclaimedDescriptors",txRingDiag.reclaimedDescriptors,64);setProperty("P12TXReclaimedPackets",txRingDiag.reclaimedPackets,64);
 setProperty("P12TXNoReclaimPasses",txRingDiag.noReclaim,64);
 setProperty("P12TXQueueServiceCalls",TXRingDiagnostic::read(txRingDiag.queueCalls),64);
 setProperty("P12TXQueueServiceTrue",TXRingDiagnostic::read(txRingDiag.queueTrue),64);
 setProperty("P12TXFixImplemented",false);setProperty("P12TXAdditionalMMIO",false);
 setProperty("P12TXRingCount",network.txCount,32);setProperty("P12TXEpoch",generation,32);
 auto txData=OSData::withBytes(txRingDiag.samples,txRingDiag.count*TXRingDiagnostic::Words*sizeof(uint64_t));
 if(txData){setProperty("P12TXSamples",txData);txData->release();}
 const auto &txq4=mac34TXQ4.receiptCounters();
 setProperty("MAC34TXQ4OwnershipRetained",txq4OwnershipRetained);
 setProperty("MAC34TXQ4Schema",1,32);setProperty("MAC34TXQ4Policy",mac34TXQ4.enabled());
 setProperty("MAC34TXQ4Transfers4",txq4.transferred[0],64);setProperty("MAC34TXQ4Transfers6",txq4.transferred[1],64);
 setProperty("MAC34TXQ4Completed4",txq4.completed[0],64);setProperty("MAC34TXQ4Completed6",txq4.completed[1],64);
 setProperty("MAC34TXQ4Cancelled4",txq4.cancelled[0],64);setProperty("MAC34TXQ4Cancelled6",txq4.cancelled[1],64);
 setProperty("MAC34TXQ4ChildCleanupCallbacks",txq4.childReclaims,64);setProperty("MAC34TXQ4Live",txq4.live,64);setProperty("MAC34TXQ4MaximumLive",txq4.maximumLive,64);
 setProperty("MAC34TXQ4Attempts",txq4.attempts,64);setProperty("MAC34TXQ4InvariantErrors",txq4.invariantErrors,64);setProperty("MAC34TXQ4InvalidCookies",txq4.invalidCookies,64);
 for(unsigned i=0;i<MAC34TXCompletion::ReasonCount;++i){char key[48];snprintf(key,sizeof(key),"MAC34TXQ4Refusal%u",i);setProperty(key,txq4.fallback[i],64);}
 setProperty("Stage3CTXAttempts",audit.attempts,64);setProperty("Stage3CTXAccepted",audit.accepted,64);
 setProperty("Stage3CTXAcceptedBytes",audit.acceptedBytes,64);setProperty("Stage3CTXDropped",audit.totalDrops(),64);
 setProperty("Stage3CTXDropInactive",audit.drops[0],64);setProperty("Stage3CTXDropSize",audit.drops[1],64);
 setProperty("Stage3CTXDropPadding",audit.drops[2],64);setProperty("Stage3CTXDropMapping",audit.drops[3],64);
 setProperty("Stage3CTXDropSubmit",audit.drops[4],64);setProperty("Stage3CTXRingFull",audit.full,64);setProperty("Stage3CTXGatedRetry",audit.gated,64);
 setProperty("Stage3CTXMapCalls",audit.mapCalls,64);setProperty("Stage3CTXMapAverageNS",audit.mapCalls?audit.mapTotalNS/audit.mapCalls:0,64);
 setProperty("Stage3CTXMapMaxNS",audit.mapMaxNS,64);setProperty("Stage3CTXMapOver1MS",audit.mapOver1MS,64);
 setProperty("Stage3CTXChainMax",audit.chainMax,64);setProperty("Stage3CTXLengthMax",audit.lengthMax,64);
 setProperty("Stage3CTXPendingChecksum",audit.pendingChecksum,64);setProperty("Stage3CTXChecksumFlags",audit.checksumFlags,64);
 char key[40];for(unsigned i=0;i<16;++i){snprintf(key,sizeof(key),"Stage3CTXMapStatus%u",i);setProperty(key,audit.mapStatus[i],64);}
 for(unsigned i=0;i<5;++i){snprintf(key,sizeof(key),"Stage3CTXSegments%u",i);setProperty(key,audit.segments[i],64);}

 setProperty("Stage4APowerRegistered",powerRegistered);
 setProperty("Stage4ASuspended",power.suspended);setProperty("Stage4ARequestedEnabled",power.requested);
 setProperty("Stage4ASleepCount",power.sleeps,64);setProperty("Stage4AWakeCount",power.wakes,64);
 setProperty("Stage4AFailures",power.failures,64);setProperty("Stage4ALastStatus",power.lastStatus,32);
 setProperty("Stage3FIRQFilters",IRQAudit::read(irqAudit.filters),64);
 setProperty("Stage3FIRQQualified",IRQAudit::read(irqAudit.qualified),64);
 setProperty("Stage3FIRQReadFailures",IRQAudit::read(irqAudit.readFailures),64);
 setProperty("Stage3FIRQUnqualified",IRQAudit::read(irqAudit.unqualified),64);
 setProperty("Stage3FIRQActions",IRQAudit::read(irqAudit.actions),64);
 setProperty("Stage3FIRQDisabledActions",IRQAudit::read(irqAudit.disabledActions),64);
 setProperty("Stage3FIRQEmptyActions",IRQAudit::read(irqAudit.emptyActions),64);
 setProperty("Stage3FIRQPasses",IRQAudit::read(irqAudit.passes),64);
 setProperty("Stage3FIRQAckFailures",IRQAudit::read(irqAudit.ackFailures),64);
 setProperty("Stage3FIRQBudgetExhausted",IRQAudit::read(irqAudit.budgetExhausted),64);
 for(unsigned i=0;i<16;++i){snprintf(key,sizeof(key),"Stage3FIRQFilterBit%u",i);setProperty(key,IRQAudit::read(irqAudit.filterBits[i]),64);}
 for(unsigned i=0;i<4;++i){snprintf(key,sizeof(key),"Stage3FIRQServiceBit%u",i);setProperty(key,IRQAudit::read(irqAudit.serviceBits[i]),64);}
 setProperty("Stage3ETXPoolCount",backend?backend->txPoolCount():0,32);setProperty("Stage3ETXPoolLeased",backend?backend->txPoolLeased():0,32);
 setProperty("Stage3ETXPoolIOVA64",backend?backend->txPoolIOVA():0,64);
 setProperty("Stage3BEnabled",enabled);setProperty("Stage3BPrepared",network.prepared);
 setProperty("Stage3BTXRingCount",network.txCount,32);setProperty("Stage3BRXRingCount",kNetworkRXCount,32);setProperty("Stage3BStatus",unsigned(network.status),32);
 setProperty("Stage3BTXSubmitted",network.txSubmitted,64);setProperty("Stage3BTXCompleted",network.txCompleted,64);
 setProperty("Stage3BRXDelivered",network.rxDelivered,64);setProperty("Stage3BRXDropped",network.rxDropped,64);
 setProperty("Stage3BInterruptActions",network.irqActions,64);setProperty("Stage3BTXErrors",network.txErrors,64);
 setProperty("Stage3BTXDescriptorsUsed",network.used,32);setProperty("Stage3BUnsafeMappingRetained",network.unsafeRetained);
 setProperty("Stage3BTimerCompletionPolling",false);setProperty("BusMasterEnabled",rings.busMasterEnabled);
 setProperty("Stage3BPacketSource","macOS-network-stack");setProperty("Stage3BTXMapping",backend->deviceMapperPresent()?"pooled-snapshot-device-IOMapper-native64":"pooled-snapshot-no-iommu-native64");
 setProperty("TXRingIOVA64",rings.tx.segments[0].address,64);setProperty("RXRingIOVA64",rings.rx.segments[0].address,64);
 setProperty("Stage3BRXBuffer0IOVA64",network.rxBuffers[0].segments[0].address,64);
 uint8_t command=255;if(backend)backend->read8(0x37,command);setProperty("ChipCommand",command,32);
 if(stats){stats->inputPackets=uint32_t(network.rxDelivered);stats->inputErrors=uint32_t(network.rxDropped);
  stats->outputPackets=uint32_t(network.txCompleted);stats->outputErrors=uint32_t(network.txErrors);}
 publishRXStatistics();
 publishJTXTrace();
}
void RealtekVTDNextEthernet::publishJTXTrace(){
 setProperty("JTX1DiagnosticOnly",true);setProperty("JTX1FixImplemented",false);
 setProperty("JTX1Schema",1,32);setProperty("JTX1Capacity",JTXTrace::Capacity,32);
 setProperty("JTX1Count",jtxTrace.count(),32);setProperty("JTX1Suppressed",jtxTrace.suppressed(),64);
 setProperty("JTX1TimestampUnit","monotonic-nanoseconds");setProperty("JTX1PHYSource","cached-existing-link-samples");
 if(jtxExportedRevision==jtxTrace.revision())return;
 unsigned size=0;const auto bytes=jtxTrace.encode(size);auto data=OSData::withBytes(bytes,size);
 if(data){if(setProperty("JTX1Trace",data))jtxExportedRevision=jtxTrace.revision();data->release();}
}
void RealtekVTDNextEthernet::publishRXStatistics(){
 // Observe counters and handoff only. No increment/set of BSD statistics.
 setProperty("RXMC1CandidateKind","diagnostic-rx-handoff-multicast");
 setProperty("RXMC1RXFixImplemented",false);
 setProperty("RXD2Sampling", "read-only-before-after-flush-max-1Hz");
 setProperty("RXD2FlushReturnIsBSDAcceptance",false);
 setProperty("RXD2ReceiveCalls",rxHandoff.calls,64);
 setProperty("RXD2InterfaceAbsent",rxHandoff.interfaceAbsent,64);
 setProperty("RXD2AllocationsFailed",rxHandoff.allocationsFailed,64);
 setProperty("RXD2CopiesFailed",rxHandoff.copiesFailed,64);
 setProperty("RXD2InputCalls",rxHandoff.inputCalls,64);
 setProperty("RXD2InputBytes",rxHandoff.inputBytes,64);
 setProperty("RXD2InputReturned",rxHandoff.inputReturned,64);
 setProperty("RXD2MissingPacketHeader",rxHandoff.missingHeader,64);
 setProperty("RXD2UnexpectedNextPacket",rxHandoff.unexpectedNextPacket,64);
 setProperty("RXD2DetachedAtInput",rxHandoff.detachedAtInput,64);
 setProperty("RXD2FlushCalls",rxHandoff.flushCalls,64);
 setProperty("RXD2FlushReturned",rxHandoff.flushReturned,64);
 setProperty("RXD2EmptyFlushes",rxHandoff.emptyFlushes,64);
 setProperty("RXD2FlushMax",rxHandoff.flushMax,64);
 setProperty("RXD2SampleSequence",rxProbeSequence,64);
 setProperty("RXD2SampleDelivered",rxProbeDelivered,64);
 setProperty("RXD2SampleInputCalls",rxProbeInputCalls,64);
 setProperty("RXD2SampleFlushReturned",rxProbeFlushReturned,64);
 setProperty("RXD2BeforeStatus",unsigned(rxProbeBeforeStatus),32);
 setProperty("RXD2AfterStatus",unsigned(rxProbeAfterStatus),32);
 setProperty("RXD2BeforeValid",rxProbeBeforeStatus==0);
 setProperty("RXD2AfterValid",rxProbeAfterStatus==0);
 setProperty("RXD2BeforePackets",rxProbeBefore.packets_in,64);
 setProperty("RXD2AfterPackets",rxProbeAfter.packets_in,64);
 setProperty("RXD2BeforeBytes",rxProbeBefore.bytes_in,64);
 setProperty("RXD2AfterBytes",rxProbeAfter.bytes_in,64);
 setProperty("RXD2BeforeMulticast",rxProbeBefore.multicasts_in,64);
 setProperty("RXD2AfterMulticast",rxProbeAfter.multicasts_in,64);
 setProperty("RXMC1DriverStatsAvailable",stats!=nullptr);
 auto data=interface?interface->getNetworkData(kIONetworkStatsKey):nullptr;
 setProperty("RXMC1StatsBufferMatchesInterface",data&&data->getBuffer()==stats);
 setProperty("RXMC1DriverInputPackets",stats?stats->inputPackets:0,32);
 setProperty("RXMC1DriverInputErrors",stats?stats->inputErrors:0,32);
 auto ifp=interface?interface->getIfnet():nullptr;
 setProperty("RXMC1BSDInterfaceAttached",ifp!=nullptr);
 if(ifp){setProperty("RXMC1BSDName",ifnet_name(ifp));setProperty("RXMC1BSDUnit",ifnet_unit(ifp),32);}
 struct ifnet_stats_param observed{};
 const errno_t result=ifp?ifnet_stat(ifp,&observed):ENXIO;
 setProperty("RXMC1BSDStatsStatus",unsigned(result),32);
 setProperty("RXMC1BSDStatsValid",result==0);
 // A zero value with BSDStatsValid=false is unavailable, not a measured zero.
 setProperty("RXMC1BSDInputPackets",observed.packets_in,64);
 setProperty("RXMC1BSDInputBytes",observed.bytes_in,64);
 setProperty("RXMC1BSDInputErrors",observed.errors_in,64);
 setProperty("RXMC1BSDInputMulticast",observed.multicasts_in,64);
 setProperty("RXMC1BSDOutputPackets",observed.packets_out,64);
 setProperty("RXMC1BSDOutputBytes",observed.bytes_out,64);
}
void RealtekVTDNextEthernet::finishStopSafetyOwner(){
 if(!stopSafetyOwnerPinned||!backend)return;
 // Terminal start-failure/stop only: the consumer is stopped by the framework
 // or disable(); remove callback sources before any DMA owner can be released.
 if(irq){irq->disable();if(irqAdded&&loop)loop->removeEventSource(irq);irqAdded=false;irq->release();irq=nullptr;}
 if(timer){timer->cancelTimeout();if(timerAdded&&loop)loop->removeEventSource(timer);timerAdded=false;timer->release();timer=nullptr;}
 mac34TXQ4.markCancelled(); // terminal teardown is cancellation, never TX completion
 const bool networkSafe=releaseNetwork(*backend,*backend,rings,network,*this,backend);
 if(retainTXQ4Ownership(true))return;
 const bool safe=networkSafe&&backend->releaseTXPool();
 if(safe)teardownStage2A(*backend,*backend,rings);
 if(!safe||rings.unsafeMappingRetained||rings.stopSafetyRetained){
  if(!network.stopSafety.latched){auto r=network.stopSafety.last;r.failures|=StopSafetyReceipt::TerminalCleanup;network.stopSafety.record(r);}
  rings.stopSafetyRetained=true;network.status=NetworkStatus::UnsafeStop;
  backend->retainStopSafety();quarantined=true;network.unsafeRetained=true;
  network.stopSafety.write(*this);setProperty("STOPSAFE1OwnerPinned",true);return;
 }
 backend->detach();delete backend;backend=nullptr;
 stopSafetyOwnerPinned=false;
 release(); // Last action: normal OSObject destruction may now become possible.
}
void RealtekVTDNextEthernet::stop(IOService *provider){
 PMstop();
 if(lock)disable(interface);
 if(interface){detachInterface(interface,true);interface->release();interface=nullptr;stats=nullptr;}
 if(irq){irq->disable();if(irqAdded)loop->removeEventSource(irq);irqAdded=false;}
 if(timer){timer->cancelTimeout();if(timerAdded)loop->removeEventSource(timer);timerAdded=false;}
 txq3=nullptr;txq2=nullptr;IOEthernetController::stop(provider);
 finishStopSafetyOwner();
}
void RealtekVTDNextEthernet::free(){
 if(txq4OwnershipRetained)return; // explicit ownership quarantine; no terminal destruction
 PMstop();
 if(irq){irq->disable();if(irqAdded&&loop)loop->removeEventSource(irq);irq->release();irq=nullptr;}
 if(timer){timer->cancelTimeout();if(timerAdded&&loop)loop->removeEventSource(timer);timer->release();timer=nullptr;}
 if(backend){
  mac34TXQ4.markCancelled(); // retain live records unchanged if stop is unconfirmed
  const bool networkSafe=releaseNetwork(*backend,*backend,rings,network,*this);
  if(retainTXQ4Ownership(true))return;
  const bool safe=networkSafe&&backend->releaseTXPool();
  if(safe)teardownStage2A(*backend,*backend,rings);
  if(safe&&!rings.unsafeMappingRetained){backend->detach();delete backend;backend=nullptr;}
  else{IOLog("RealtekVTDNext: preserving device and DMA after unsafe stop\n");device=nullptr;backend=nullptr;}
 }
 if(device){if(opened)device->close(this);device->release();device=nullptr;}
 for(auto &m:media)if(m){m->release();m=nullptr;}
 if(lock){IOLockFree(lock);lock=nullptr;}if(loop){loop->release();loop=nullptr;}
 txq3=nullptr;txq2=nullptr;IOEthernetController::free();
}
