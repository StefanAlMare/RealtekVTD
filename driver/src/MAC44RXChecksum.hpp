// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "RXChecksum.hpp"
#include "TXChecksum.hpp"
namespace realtek_next {
// Explicit contracts, not a widening of the GU25 identity/policy guard.
constexpr uint16_t kMAC44CPlusCmd=0xe0,kMAC44RxChecksum=1U<<5,kMAC44RxVlan=1U<<6;
enum class RXChecksumProfile:uint8_t {GU25RevisionC,MAC44RevisionC,MAC34RevisionC=2,MAC44RevisionCDeviceMapper=MAC44RevisionC,MAC46RevisionC=3};
enum class MAC44RXHardwareStatus:uint8_t {NotAttempted,Ok,Read,Sentinel,Write,Readback,Compare};
struct MAC44RXHardwareReceipt {
 MAC44RXHardwareStatus status=MAC44RXHardwareStatus::NotAttempted;
 uint16_t before=0,offWritten=0,offRead=0;
 bool beforeValid=false,offAttempted=false,offReadValid=false,offVerified=false;
};
// Only MAC44's explicit initialized DMA profile calls this adapter; the
// legacy DeviceMapper enum name aliases the same revision-C packet format.
// CPlusCmd consists of control bits. RMW only RxChkSum/RxVlan; do not force
// PAUSE/duplex/debug bits. An optional enable failure can degrade only after a
// checked OFF write/readback. Failed/sentinel initial reads or OFF failure stop.
template<class IO> bool configureMAC44RXHardware(IO &io,bool enable,
 RXChecksumHardwareReceipt &r,MAC44RXHardwareReceipt &s){
 r={};s={};r.attempted=true;
 if(!io.read16(kMAC44CPlusCmd,s.before)){s.status=MAC44RXHardwareStatus::Read;return false;}
 if(s.before==0xffff){s.status=MAC44RXHardwareStatus::Sentinel;return false;}s.beforeValid=true;
 r.written=uint16_t((s.before&~(kMAC44RxChecksum|kMAC44RxVlan))|(enable?kMAC44RxChecksum:0));
 if(!io.write16(kMAC44CPlusCmd,r.written))s.status=MAC44RXHardwareStatus::Write;
 else {
  r.readValid=io.read16(kMAC44CPlusCmd,r.read);
  if(!r.readValid)s.status=MAC44RXHardwareStatus::Readback;
  else if(r.read==0xffff||r.read!=r.written)s.status=MAC44RXHardwareStatus::Compare;
  else {s.status=MAC44RXHardwareStatus::Ok;r.verified=true;return true;}
 }
 if(!enable)return false;
 // This is a bounded disable/cleanup, never a retry of the failed enable.
 s.offAttempted=true;s.offWritten=uint16_t(s.before&~(kMAC44RxChecksum|kMAC44RxVlan));
 if(!io.write16(kMAC44CPlusCmd,s.offWritten))return false;
 s.offReadValid=io.read16(kMAC44CPlusCmd,s.offRead);
 s.offVerified=s.offReadValid&&s.offRead!=0xffff&&s.offRead==s.offWritten;
 return s.offVerified; // r.verified remains false: do not relabel failure success.
}
// MAC46 has its own explicit profile/gate. Its pinned revision-C CPlusCmd
// transport contract is identical; sharing the RMW adapter is not identity reuse.
using MAC46RXHardwareReceipt=MAC44RXHardwareReceipt;
template<class IO>bool configureMAC46RXHardware(IO &io,bool enable,RXChecksumHardwareReceipt &r,MAC46RXHardwareReceipt &s){return configureMAC44RXHardware(io,enable,r,s);}
struct MAC44RXAssistReceipt {
 bool attempted=false,configured=false,enabled=false,rollbackAttempted=false,rollbackOK=false;
 uint32_t beforeAssist=0,beforeSupported=0,beforeEnabled=0,afterAssist=0,afterSupported=0,afterEnabled=0;
 int status=0;
};
// RX capability only. No set_offload call and no writes to TX or unrelated bits.
// IO setters must use the public RX-only mask; controller calls outside its lock.
template<class IO> bool configureMAC44RXAssist(IO &io,bool enable,MAC44RXAssistReceipt &r){
 r={};if(!io.valid()){r.status=-1;return false;}r.attempted=true;
 r.beforeAssist=io.offload();r.beforeSupported=io.supported();r.beforeEnabled=io.enabled();
 const uint32_t bit=IO::rxCapability;
 const uint32_t supported=(r.beforeSupported&~bit)|(enable?bit:0),active=(r.beforeEnabled&~bit)|(enable?bit:0);
 auto read=[&](){r.afterAssist=io.offload();r.afterSupported=io.supported();r.afterEnabled=io.enabled();};
 auto rollback=[&](){r.rollbackAttempted=true;const int a=io.setSupported(r.beforeSupported),b=io.setEnabled(r.beforeEnabled);read();
  r.rollbackOK=!a&&!b&&r.afterAssist==r.beforeAssist&&r.afterSupported==r.beforeSupported&&r.afterEnabled==r.beforeEnabled;return false;};
 r.status=io.setSupported(supported);if(r.status)return rollback();
 r.status=io.setEnabled(active);if(r.status)return rollback();read();
 if(r.afterAssist!=r.beforeAssist||r.afterSupported!=supported||r.afterEnabled!=active){r.status=-2;return rollback();}
 r.configured=true;r.enabled=enable;return true;
}
struct MAC44RXChecksumState {
 TXChecksumPolicy support; // existing atomic publication discipline for framework queries.
 bool requested=false,identityEligible=false,hardwareInitialized=false,mapperReady=false,native64Ready=false,dmaReady=false,ifnetManaged=false;
 MAC44RXAssistReceipt assist,disableFallback;
 uint64_t ifnetFailures=0,published[5]{};
 void configure(bool on,bool identity,bool initialized,bool mapper,bool native64=false){
  support.configure(false);requested=on;identityEligible=identity;hardwareInitialized=initialized;mapperReady=mapper;native64Ready=!mapper&&native64;dmaReady=mapperReady||native64Ready;assist={};disableFallback={};
  support.configure(requested&&identityEligible&&hardwareInitialized&&dmaReady);
 }
 bool declared()const{return support.enabled();}
 bool publishable(bool hardware)const{return declared()&&assist.configured&&assist.enabled&&hardware;}
 void notePublished(const RXChecksumMetadata &m){const unsigned v=m.checked&m.valid;
  for(unsigned i=0;i<5;++i)if(v&(1U<<i))++published[i];
 }
};
}
