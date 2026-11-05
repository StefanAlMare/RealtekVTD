// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "JumboProfile.hpp"
#include "PauseProgram.hpp"
#include "PauseObservation.hpp"
namespace realtek_next {
enum class MTUState {STANDARD_1500,JUMBO_ACTIVE,TRANSITION,ROLLBACK,SAFE_STOP};
inline const char *mtuStateName(MTUState s){
 switch(s){case MTUState::STANDARD_1500:return "STANDARD_1500";case MTUState::JUMBO_ACTIVE:return "JUMBO_ACTIVE";
  case MTUState::TRANSITION:return "TRANSITION";case MTUState::ROLLBACK:return "ROLLBACK";default:return "SAFE_STOP";}
}
// Target owns controller serialization. Queue stop/join happens BEFORE its lock.
// stop/safeStop never release mappings; start reuses the prepared resources.
struct MTUTransaction {
 unsigned mtu=1500;MTUState state=MTUState::STANDARD_1500;
 uint16_t savedPause=0;bool savedValid=false,policyApplied=false;
 bool awaitingAutoneg=false,transitionSeen=false;uint64_t sequence=0,failures=0;
 void stable(){state=mtu>1500?MTUState::JUMBO_ACTIVE:MTUState::STANDARD_1500;}
 void arm(bool restarted,bool seen){if(restarted){awaitingAutoneg=true;transitionSeen=seen;}}
 void offline(unsigned requested){mtu=requested;policyApplied=false;awaitingAutoneg=true;transitionSeen=false;stable();}
 template<class Target> bool reapply(Target &t){
  if(state==MTUState::SAFE_STOP)return false;
  uint16_t current=0;bool restarted=false,seen=false;
  if(!t.readPause(current)){state=MTUState::SAFE_STOP;++failures;t.safeStop();return false;}
  if(mtu>1500&&!savedValid){savedPause=current;savedValid=true;}
  const uint16_t wanted=mtu>1500?0:(savedValid?savedPause:current);
  if(!t.applyPause(wanted,mtu>1500||savedValid,restarted,seen)){
   state=MTUState::SAFE_STOP;policyApplied=false;++failures;t.safeStop();return false;
  }
  arm(restarted,seen);policyApplied=true;stable();return true;
 }
 template<class Target> bool change(Target &t,unsigned requested){
  if(requested<1500||requested>kJumboMTU||state==MTUState::SAFE_STOP)return false;
  if(requested==mtu)return true;
  const MTUTransaction old=*this;uint16_t before=0;
  if(!t.readPause(before)){++failures;return false;} // Nothing mutated; retain running configuration.
  ++sequence;state=MTUState::TRANSITION;policyApplied=false;
  if(!t.stop()){state=MTUState::SAFE_STOP;++failures;t.safeStop();return false;}
  if(old.mtu==1500&&requested>1500){savedPause=before;savedValid=true;}
  const uint16_t wanted=requested>1500?0:(savedValid?savedPause:before);
  bool restarted=false,seen=false;
  const bool applied=t.applyPause(wanted,(old.mtu>1500)!=(requested>1500),restarted,seen);
  arm(restarted,seen);
  if(applied&&t.start(requested)){mtu=requested;policyApplied=true;stable();return true;}
  state=MTUState::ROLLBACK;++failures;
  // A failed start may already have enabled BME. Quiesce again BEFORE PHY rollback.
  if(!t.stop()||!t.applyPause(before,true,restarted,seen)||!t.start(old.mtu)){
   state=MTUState::SAFE_STOP;policyApplied=false;t.safeStop();return false;
  }
  const auto count=sequence,failed=failures;
  *this=old;sequence=count;failures=failed;policyApplied=true;arm(restarted,seen);stable();return false;
 }
 void observe(const PHYDiagnostic &d,PauseResolution &p){
  if(!policyApplied||state==MTUState::TRANSITION||state==MTUState::ROLLBACK||state==MTUState::SAFE_STOP){
   p.valid=p.tx=p.rx=p.mediumFlow=p.agreement=false;p.reason="mtu-policy-unverified";return;
  }
  if(awaitingAutoneg){
   if((d.valid&7)==7&&d.bmcr!=0xffff&&d.bmsrFirst!=0xffff&&d.bmsrSecond!=0xffff)
    transitionSeen|=bool(d.bmcr&0x0200)||!(d.bmsrFirst&0x20)||!(d.bmsrSecond&0x20)||!(d.bmsrSecond&4);
   if(transitionSeen&&p.valid&&p.agreement)awaitingAutoneg=false;
   else {p.valid=p.tx=p.rx=p.mediumFlow=p.agreement=false;p.reason="mtu-aneg-unverified";return;}
  }
  if(mtu>1500&&((d.anar&kPauseAdvertisementMask)||p.tx||p.rx||p.phyTx||p.phyRx)){
   p.valid=p.tx=p.rx=p.mediumFlow=p.agreement=false;p.reason="jumbo-pause-mismatch";
  }
 }
};
}
