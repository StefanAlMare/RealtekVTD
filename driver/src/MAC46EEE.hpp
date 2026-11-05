// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "ChipIdentity.hpp"
#include "RegisterAccess.hpp"
#include "MAC46LinkLPI.hpp"
namespace realtek_next {
// The pinned r8169 MAC_VER_46 policy explicitly calls phy_disable_eee().
// AUTO therefore chooses verified-off on this exact profile. Capability is
// observed separately: a capable PHY is not a warrant to override that policy.
// No new reset, PHY-MCU handshake, periodic poll or LPI-enable write is added.
enum class MAC46EEEDecision:unsigned {
 Unmanaged,AutoDisabledMAC46Reference,AutoFallbackCapabilityUnknown,
 AutoUnsupportedCapabilities,NotQuiescent,TransportFailure,VerificationFailed
};
struct MAC46EEEReceipt {
 enum:uint32_t {Capability=1,Partner=2,AdvertisementBefore=4,AdvertisementAfter=8,
                LPIBefore=16,LPIAfter=32};
 bool attempted=false,applied=false,offVerified=false,advertisementWritten=false,lpiWritten=false;
 bool capabilityReadReturned=false,partnerReadReturned=false;
 bool restartRequired=false;unsigned mtu=1500;uint32_t valid=0;
 uint16_t capability=0,partner=0,advertisementBefore=0,advertisementAfter=0;
 uint32_t lpiBefore=0,lpiAfter=0;
 AccessStatus status=AccessStatus::Unsupported;
 AccessStatus capabilityStatus=AccessStatus::Unsupported,partnerStatus=AccessStatus::Unsupported;
 MAC46EEEDecision decision=MAC46EEEDecision::Unmanaged;
 const char *stage="not-run";
};
struct MAC46EEEPolicy {
 bool eligible=false,requestedAuto=false,native64Ready=false,dmaReady=false;
 bool linkEligible=false; // Separate MAC-only containment; never enables the H feature policy.
 uint64_t attempts=0,failures=0,capabilityUnknown=0;
 MAC46EEEReceipt last;MAC46LinkLPI linkLPI;
 static constexpr bool identity(const Identity&id){
  return id.status==ProbeStatus::Supported&&id.chip==Chip::RTL8168H&&
         id.linuxMacVersion==46&&id.xid==0x541&&cfgMethod(id.chip)==0;
 }
 void configure(const Identity&id,bool initialized,bool mapperReady,bool firmwareApplied,bool native64=false){
  native64Ready=!mapperReady&&native64;dmaReady=mapperReady||native64Ready;
  eligible=identity(id)&&initialized&&dmaReady&&firmwareApplied;
  requestedAuto=eligible;linkEligible=eligible;last=MAC46EEEReceipt{};linkLPI.invalidate();
 }
 // Linux maps M/XID6c0 to MAC46 and the same H2 firmware. Only the MAC
 // link-up containment is shared; eligible/apply()/FC1/offloads stay H-only.
 // Called after configure(), using concordant current initialization and the
 // actual backend's completed DMA facts. Keep this receipt latched through
 // halt(): failure guards must still run after enabled/nativeReady changes.
 void configureLinkM(const Identity&id,bool initialized,bool ready,bool mapper,
                     bool firmwareApplied,bool native64){
  if(id.chip!=Chip::RTL8168M)return;
  linkEligible=id.status==ProbeStatus::Supported&&id.linuxMacVersion==46&&
   id.xid==0x6c0&&cfgMethod(id.chip)==0&&initialized&&firmwareApplied&&ready&&
   (mapper||(!mapper&&native64));
 }
 // All calls are made under controller serialization with verified BME/MAC off.
 // Cap/partner reads are optional observations. Unknown capability falls back
 // to verified-off; an error in mandatory containment never becomes success.
 bool apply(RegisterAccess&r,bool quiescent,unsigned mtu){
  last=MAC46EEEReceipt{};last.mtu=mtu;
  if(!eligible)return true;
  last.attempted=true;if(attempts!=UINT64_MAX)++attempts;
  auto fail=[&](AccessStatus status,const char*stage,MAC46EEEDecision decision)->bool{
   last.status=status;last.stage=stage;last.decision=decision;
   if(failures!=UINT64_MAX)++failures;return false;
  };
  if(!quiescent)return fail(AccessStatus::VerificationFailed,"quiescent-required",MAC46EEEDecision::NotQuiescent);
  // Mieze CFG29/30 get_eee: pageA5C reg12 (PCS capability), pageA5D
  // reg10/11 (AN advertisement/partner); identical internal PHY-OCP mapping.
  auto supported=r.phyRead(0xa5c4);
  last.capabilityStatus=supported.status;last.capabilityReadReturned=supported.status==AccessStatus::Ok;
  if(last.capabilityReadReturned)last.capability=supported.value;
  if(supported.status==AccessStatus::Ok&&supported.value!=0xffff){last.capability=supported.value;last.valid|=MAC46EEEReceipt::Capability;}
  auto partner=r.phyRead(0xa5d2);
  last.partnerStatus=partner.status;last.partnerReadReturned=partner.status==AccessStatus::Ok;
  if(last.partnerReadReturned)last.partner=partner.value;
  if(partner.status==AccessStatus::Ok&&partner.value!=0xffff){last.partner=partner.value;last.valid|=MAC46EEEReceipt::Partner;}
  auto before=r.phyRead(0xa5d0);
  if(before.status!=AccessStatus::Ok)return fail(before.status,"advertisement-read",MAC46EEEDecision::TransportFailure);
  if(before.value==0xffff)return fail(AccessStatus::IOFailure,"advertisement-sentinel",MAC46EEEDecision::TransportFailure);
  last.advertisementBefore=before.value;last.valid|=MAC46EEEReceipt::AdvertisementBefore;
  last.restartRequired=before.value!=0;
  auto status=r.phyWrite(0xa5d0,0);
  if(status!=AccessStatus::Ok)return fail(status,"advertisement-write",MAC46EEEDecision::TransportFailure);
  last.advertisementWritten=true;
  auto after=r.phyRead(0xa5d0);
  if(after.status!=AccessStatus::Ok)return fail(after.status,"advertisement-readback",MAC46EEEDecision::TransportFailure);
  if(after.value==0xffff)return fail(AccessStatus::IOFailure,"advertisement-readback-sentinel",MAC46EEEDecision::TransportFailure);
  last.advertisementAfter=after.value;last.valid|=MAC46EEEReceipt::AdvertisementAfter;
  if(after.value!=0)return fail(AccessStatus::VerificationFailed,"advertisement-mismatch",MAC46EEEDecision::VerificationFailed);
  auto lpi=r.eriRead(0x1b0);
  if(lpi.status!=AccessStatus::Ok)return fail(lpi.status,"mac-lpi-read",MAC46EEEDecision::TransportFailure);
  if(lpi.value==0xffffffffU)return fail(AccessStatus::IOFailure,"mac-lpi-sentinel",MAC46EEEDecision::TransportFailure);
  last.lpiBefore=lpi.value;last.valid|=MAC46EEEReceipt::LPIBefore;
  status=r.eriWrite(0x1b0,15,lpi.value&~3U);
  if(status!=AccessStatus::Ok)return fail(status,"mac-lpi-write",MAC46EEEDecision::TransportFailure);
  last.lpiWritten=true;
  auto got=r.eriRead(0x1b0);
  if(got.status!=AccessStatus::Ok)return fail(got.status,"mac-lpi-readback",MAC46EEEDecision::TransportFailure);
  if(got.value==0xffffffffU)return fail(AccessStatus::IOFailure,"mac-lpi-readback-sentinel",MAC46EEEDecision::TransportFailure);
  last.lpiAfter=got.value;last.valid|=MAC46EEEReceipt::LPIAfter;
  if(got.value&3U)return fail(AccessStatus::VerificationFailed,"mac-lpi-mismatch",MAC46EEEDecision::VerificationFailed);
  last.status=AccessStatus::Ok;last.stage="auto-verified-off";
  last.applied=last.offVerified=true;
  if(!(last.valid&MAC46EEEReceipt::Capability)){
   last.decision=MAC46EEEDecision::AutoFallbackCapabilityUnknown;
   if(capabilityUnknown!=UINT64_MAX)++capabilityUnknown;
  }else last.decision=(last.capability&6U)?MAC46EEEDecision::AutoDisabledMAC46Reference:MAC46EEEDecision::AutoUnsupportedCapabilities;
  return true;
 }
 const char*reason()const{
  switch(last.decision){
   case MAC46EEEDecision::AutoDisabledMAC46Reference:return "auto-disabled-pinned-linux-MAC46-policy";
   case MAC46EEEDecision::AutoFallbackCapabilityUnknown:return "auto-fallback-capability-unknown-verified-off";
   case MAC46EEEDecision::AutoUnsupportedCapabilities:return "auto-no-100-or-1000-EEE-capability-verified-off";
   case MAC46EEEDecision::NotQuiescent:return "quiescent-required";
   case MAC46EEEDecision::TransportFailure:return "transport-failure";
   case MAC46EEEDecision::VerificationFailed:return "readback-failed";
   default:return "profile-unchanged";
  }
 }
 template<class Sink>void write(Sink&s)const{
  linkLPI.write(s,linkEligible);
  s.setProperty("MAC46EEE1Schema",1,32);s.setProperty("MAC46EEE1Eligible",eligible);
  s.setProperty("MAC46EEE1AutoRequested",requestedAuto);s.setProperty("MAC46EEE1Applied",last.applied);
  s.setProperty("MAC46EEE1OffVerified",last.offVerified);s.setProperty("MAC46EEE1Active",false);
  s.setProperty("MAC46EEE1ActiveKnown",eligible&&last.offVerified);
  s.setProperty("MAC46EEE1PolicyBlocksLPI",eligible);s.setProperty("MAC46EEE1Reason",reason());
  s.setProperty("MAC46EEE1Status",unsigned(last.status),32);s.setProperty("MAC46EEE1Decision",unsigned(last.decision),32);
  s.setProperty("MAC46EEE1Stage",last.stage);s.setProperty("MAC46EEE1MTU",last.mtu,32);
  s.setProperty("MAC46EEE1Valid",last.valid,32);s.setProperty("MAC46EEE1Capability",last.capability,16);
  s.setProperty("MAC46EEE1Partner",last.partner,16);s.setProperty("MAC46EEE1AdvertisementBefore",last.advertisementBefore,16);
  s.setProperty("MAC46EEE1CapabilityReadReturned",last.capabilityReadReturned);s.setProperty("MAC46EEE1PartnerReadReturned",last.partnerReadReturned);
  s.setProperty("MAC46EEE1CapabilityStatus",unsigned(last.capabilityStatus),32);s.setProperty("MAC46EEE1PartnerStatus",unsigned(last.partnerStatus),32);
  s.setProperty("MAC46EEE1AdvertisementAfter",last.advertisementAfter,16);s.setProperty("MAC46EEE1MACLPIBefore",last.lpiBefore,32);
  s.setProperty("MAC46EEE1MACLPIAfter",last.lpiAfter,32);s.setProperty("MAC46EEE1RestartRequired",last.restartRequired);
  s.setProperty("MAC46EEE1Attempts",attempts,64);s.setProperty("MAC46EEE1Failures",failures,64);
 }
};
}
