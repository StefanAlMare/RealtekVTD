// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "ChipIdentity.hpp"
#include "PatchHandshakeDiagnostic.hpp"
namespace realtek_next {
inline bool trialTextEqual(const char *a,const char *b){if(!a||!b)return false;while(*a&&*a==*b){++a;++b;}return *a==*b;}
// Software-only policy. This does NOT prove MCU exit: B800 must be read later.
inline bool eligibleEEEACKTrial(const Identity *id,bool critical,const PatchHandshakeDiagnostic &p,
                                const RegisterWaitDiagnostic &w,bool exactLeaveFailure){
 if(!id||!critical||!exactLeaveFailure||id->status!=ProbeStatus::Supported||id->chip!=Chip::RTL8168GU_Method25||
    id->xid!=0x509||id->linuxMacVersion!=42||cfgMethod(id->chip)!=25||p.count!=p.stored||p.stored<2||p.stored>p.capacity)return false;
 const auto &e=p.samples[p.stored-2];const auto &l=p.samples[p.stored-1];
 auto commandOK=[](const PatchHandshakeSample &v,bool enter){return v.requestReturned&&v.requestStatus==AccessStatus::Ok&&v.writeIssued&&
  v.requestWait.available&&v.requestWait.readAvailable&&v.requestWait.readValid&&!(v.requestWait.value&0x80000000U)&&v.requestWait.status==0&&v.requestWait.attempts>=1&&v.requestWait.attempts<=10&&v.requestWait.commandAvailable&&
  v.requestWait.logical==0xb820&&((v.requestWait.command>>15)&0xfffeU)==0xb820&&(v.requestWait.command&0x80000000U)&&bool(v.requestWait.command&0x10)==enter;};
 if(!trialTextEqual(e.context,"advanced-eee")||!trialTextEqual(l.context,"advanced-eee")||!e.enter||l.enter||
    !commandOK(e,true)||!commandOK(l,false)||e.result!=AccessStatus::Ok||!e.ackStarted||!e.ackReadValid||
    e.ackAttempts<1||e.ackAttempts>1000||e.ackValue==0xffff||!(e.ackValue&0x40)||l.result!=AccessStatus::Timeout||!l.ackStarted||
    !l.ackReadValid||l.ackValue==0xffff||l.ackAttempts!=1000||(l.ackValue&0x10)||!l.postReadAttempted||!l.postReadValid||
    l.postReadStatus!=AccessStatus::Ok||l.postReadValue==0xffff||(l.postReadValue&0x10))return false;
 // Earlier calls need their own proof. Only critical phy-mcu LEAVE may use
 // PHYMCU1's typed fresh B800 proof. Never accept an old A224 timeout as success.
 for(unsigned i=0;i+2<p.stored;++i){const auto &v=p.samples[i];
  if(v.result!=AccessStatus::Ok||v.requestStatus!=AccessStatus::Ok)return false;
  if(v.exitProof==PatchExitProof::FreshB800Bit6Clear){
   if(v.enter||!trialTextEqual(v.context,"phy-mcu")||!commandOK(v,false)||!v.exitStarted||!v.exitReadValid||
      !v.exitConfirmed||v.exitStatus!=AccessStatus::Ok||v.exitAttempts<1||v.exitAttempts>1000||v.exitValue==0xffff||
      (v.exitValue&0x40)||v.ackStarted||i==0||!p.samples[i-1].enter||
      !trialTextEqual(p.samples[i-1].context,"phy-mcu"))return false;
  }else if(v.exitProof!=PatchExitProof::None||!v.ackReadValid||v.ackAttempts<1||v.ackAttempts>1000||
            v.ackValue==0xffff||!(v.ackValue&(v.enter?0x40:0x10)))return false;
 }
 return w.available&&trialTextEqual(w.kind,"PATCH-LEAVE")&&w.logical==0xa224&&w.port==0xb8&&w.mask==0x10&&w.expected==0x10&&
  w.status==unsigned(AccessStatus::Timeout)&&w.attempts==1000&&w.limit==1000&&w.intervalUs==100&&w.readAvailable&&w.readValid&&
  w.sampleAttempt==1000&&w.value==l.ackValue&&!(w.value&0x10);
}
struct EEEACKTrial {
 bool attempted=false,eligible=false,criticalPHYMCUConfirmed=false,mcuReadAttempted=false,mcuReadValid=false;
 bool mcuExitConfirmed=false,continued=false,configurationComplete=false;
 uint16_t xid=0,mcuValue=0;unsigned cfg=0;AccessStatus mcuReadStatus=AccessStatus::Unsupported;
 RegisterWaitDiagnostic legacyWait,mcuWait;
 const char *disposition="NOT_NEEDED";
 template<class Sink>bool publish(Sink &s)const{
  bool ok=true;
#define N(k,v) do{if(!s.number("GU25EEETrial" k,uint64_t(v)))ok=false;}while(0)
#define T(k,v) do{if(!s.text("GU25EEETrial" k,v))ok=false;}while(0)
  N("Schema",1);N("Attempted",attempted);N("Eligible",eligible);T("Disposition",disposition);
  N("Continued",continued);N("ConfigurationComplete",configurationComplete);N("LegacyACKConfirmed",0);
  N("MCUReadAttempted",mcuReadAttempted);N("MCUExitConfirmed",mcuExitConfirmed);
  if(attempted){N("XID",xid);N("CFGMethod",cfg);N("CriticalPHYMCUConfirmed",criticalPHYMCUConfirmed);
   N("LegacyStatus",unsigned(AccessStatus::Timeout));N("LegacyWaitAvailable",legacyWait.available);
   if(legacyWait.available){T("LegacyWaitKind",legacyWait.kind);N("LegacyRegister",legacyWait.logical);N("LegacyMask",legacyWait.mask);
    N("LegacyExpected",legacyWait.expected);N("LegacyAttempts",legacyWait.attempts);N("LegacyLimit",legacyWait.limit);N("LegacyIntervalUS",legacyWait.intervalUs);
    N("LegacyReadAvailable",legacyWait.readAvailable);N("LegacyReadValid",legacyWait.readValid);if(legacyWait.readAvailable)N("LegacyReadValue",legacyWait.value);}
  }
  if(mcuReadAttempted){N("MCURegister",0xb800);N("MCUMask",0x40);N("MCUExpected",0);N("MCUReadStatus",unsigned(mcuReadStatus));
   N("MCUReadValid",mcuReadValid);if(mcuReadValid)N("MCUReadValue",mcuValue);
   N("TransportWaitAvailable",mcuWait.available);if(mcuWait.available){N("TransportStatus",mcuWait.status);N("TransportAttempts",mcuWait.attempts);
    N("TransportLimit",mcuWait.limit);N("TransportIntervalUS",mcuWait.intervalUs);}
  }
  T("MCUCriterionSource","Realtek-r8168-8.055.00-B800-bit6-clear");N("ExtraReadMaxWaitUS",250);
#undef N
#undef T
  return ok;
 }
};
}
