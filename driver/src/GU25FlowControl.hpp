// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "ChipIdentity.hpp"
#include "MTUTransaction.hpp"
namespace realtek_next {
// FC1 is negotiated IEEE 802.3 PAUSE, never a forced MAC override or PFC.
// The existing GU25 MAC FIFO thresholds and all packet/DMA paths are unchanged.
enum class FC1Request { Default, Off, On, Invalid };
enum class FC1Profile { Unmanaged, GU25, MAC44, MAC44DeviceMapper=MAC44, MAC34NoIOMMU=3, MAC46DeviceMapper=4, MAC46NoIOMMU=5 };
constexpr bool fc1Identity(const Identity &id) {
 return id.status==ProbeStatus::Supported&&id.chip==Chip::RTL8168GU_Method25&&
        id.xid==0x509&&id.linuxMacVersion==42&&cfgMethod(id.chip)==25;
}
// Inputs are current, successful reads made while the controller lock is held.
inline bool fc1QuiescentState(bool enabled,bool unsafe,bool pciValid,uint16_t pciCommand,
                             bool macValid,uint8_t macCommand) {
 return !enabled&&!unsafe&&pciValid&&macValid&&pciCommand!=0xffff&&macCommand!=0xff&&
        !(pciCommand&4U)&&!(macCommand&0x0cU);
}
struct GU25FlowControl {
 FC1Profile profile=FC1Profile::Unmanaged;
 bool mac44Preference=false;
 bool eligible=false,configurationValid=true,requested=false,applied=false;
 uint64_t applyAttempts=0,applyFailures=0;
 bool managedSavedPause=false;
 // Called after identifying the device on each existing hardware initialization.
 // No PHY, PCI, DMA or payload access. Invalid property values fail CLOSED to OFF.
 void configure(const Identity &id,FC1Request request) {
  profile=fc1Identity(id)?FC1Profile::GU25:FC1Profile::Unmanaged;mac44Preference=false;
  eligible=fc1Identity(id);configurationValid=!eligible||request!=FC1Request::Invalid;
  requested=eligible&&configurationValid&&request!=FC1Request::Off;
  applied=false;
 }
 // Explicit second profile; GU25 identity and Default=ON semantics stay intact.
 // Eligibility comes from checked exact PCI/chip identity, completed hardware init
 // and a completed eligible DMA backend (mapper or explicit native64 readiness).
 // It does not depend on TX checksum preference; PAUSE programming itself is MMIO/PHY.
 void configureMAC44(bool identity,bool initialized,bool dmaReady,FC1Request request) {
  profile=FC1Profile::MAC44DeviceMapper;
  mac44Preference=request==FC1Request::On;
  eligible=identity&&initialized&&dmaReady;
  configurationValid=request!=FC1Request::Invalid;
  requested=eligible&&configurationValid&&mac44Preference;applied=false;
 }
 // Exact MAC34 eligibility is supplied after checked chip/hardware identity and
 // completed native64 NoIOMMU selection. Never infer it from absent mapper alone.
 // This preference survives PHY reset/default ANAR; it is independent of TX/RX offloads.
 void configureMAC34(bool identity,bool initialized,bool native64Ready) {
  profile=FC1Profile::MAC34NoIOMMU;mac44Preference=false;
  eligible=identity&&initialized&&native64Ready;configurationValid=true;
  requested=eligible;applied=false;
 }
 // MAC46 FULL: negotiated AUTO follows the checked, completed DMA profile.
 // This does not infer mapper readiness from AppleVTD presence or a null pointer.
 // The caller obtains identity/init/mode from the real completed profile.
 void configureMAC46(bool identity,bool initialized,bool mapperReady,
                     FC1Request request=FC1Request::Default,bool native64Ready=false) {
  profile=native64Ready&&!mapperReady?FC1Profile::MAC46NoIOMMU:FC1Profile::MAC46DeviceMapper;mac44Preference=false;
  eligible=identity&&initialized&&(mapperReady||native64Ready);
  configurationValid=request!=FC1Request::Invalid;
  requested=eligible&&configurationValid&&request!=FC1Request::Off;applied=false;
 }
 bool mac34()const{return profile==FC1Profile::MAC34NoIOMMU;}
 bool mac44()const{return profile==FC1Profile::MAC44DeviceMapper;}
 bool mac46()const{return profile==FC1Profile::MAC46DeviceMapper||profile==FC1Profile::MAC46NoIOMMU;}
 bool mac46Native64()const{return profile==FC1Profile::MAC46NoIOMMU;}
 static constexpr uint16_t advertisement() {return 0x0c00;}
 template<class Target> bool reapply(MTUTransaction &mtu,Target &target) {
  applied=false;
  // Exactly the previous MTU policy for all other profiles and explicit OFF.
  if(!requested){
   // initializeHardware has already reapplied the conservative GU25 PHY init.
   // Drop only the saved PAUSE preference injected by this policy, never another
   // profile's pre-existing MTU preference. No run-time toggle bypasses init.
   if(managedSavedPause){mtu.savedPause=0;mtu.savedValid=false;managedSavedPause=false;}
   return mtu.reapply(target);
  }
  if(mtu.state==MTUState::SAFE_STOP)return false;
  ++applyAttempts;
  if(!target.fc1Quiescent()) {
   ++applyFailures;++mtu.failures;mtu.policyApplied=false;mtu.state=MTUState::SAFE_STOP;
   target.safeStop();return false;
  }
  // Reuse the existing checked RMW, readback, AN restart and epoch observation.
  // MTUTransaction requests zero in Jumbo and restores savedPause at 1500.
  mtu.savedPause=advertisement();mtu.savedValid=true;managedSavedPause=true;
  const bool ok=mtu.reapply(target);applied=ok;
  if(!ok){++applyFailures;mtu.policyApplied=false;}
  return ok;
 }
 // Called AFTER MTUTransaction::observe on the existing link observation.
 // Never add periodic reads, poll loops, a restart, or a hardware write here.
 void qualify(const MTUTransaction &mtu,const PHYDiagnostic &d,PauseResolution &p)const {
  if(!requested)return;
  if(mtu.mtu>1500){p.mediumFlow=false;return;} // retain the existing Jumbo policy/failure
  const bool advertisementMatches=(d.valid&8)&&d.anar!=0xffff&&
                                  (d.anar&kPauseAdvertisementMask)==advertisement();
  if(!applied||!mtu.policyApplied||!advertisementMatches) {
   p.valid=p.tx=p.rx=p.mediumFlow=p.agreement=false;
   p.reason=(!applied||!mtu.policyApplied)?"fc1-policy-unverified":"fc1-advertisement-unverified";
  }
 }
 bool active(const MTUTransaction &mtu,const PauseResolution &p,bool enabled)const {
  return requested&&applied&&enabled&&mtu.policyApplied&&mtu.mtu==1500&&
         mtu.state==MTUState::STANDARD_1500&&!mtu.awaitingAutoneg&&
         p.valid&&p.agreement&&(p.tx||p.rx);
 }
 const char *state(const MTUTransaction &mtu,const PHYDiagnostic &d,
                   const PauseResolution &p,bool enabled)const {
  if(!eligible)return "profile-unchanged";
  if(!configurationValid)return "configuration-invalid-disabled";
  if(!requested)return "disabled";
  if(mtu.state==MTUState::SAFE_STOP)return "safe-stop";
  if(!enabled||!d.link())return "link-inactive";
  if(mtu.mtu>1500)return "jumbo-pause-disabled";
  if(!applied||!mtu.policyApplied)return "policy-not-applied";
  if(!p.valid)return p.reason;
  if(!p.agreement)return "phy-status-mismatch";
  return p.reason; // none/symmetric/rx-only/tx-only, from the existing resolver
 }
};
}
