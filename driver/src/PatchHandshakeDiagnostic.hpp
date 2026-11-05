// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "RegisterAccess.hpp"
namespace realtek_next {
// One bounded receipt per actual PATCH-ENTER/LEAVE. No hardware access here.
struct PatchTransportSample {
 bool available=false,commandAvailable=false,readAvailable=false,readValid=false;
 unsigned attempts=0,status=0;uint32_t logical=0,command=0,value=0;
 void save(const RegisterWaitDiagnostic &w){available=w.available;commandAvailable=w.commandAvailable;
  readAvailable=w.readAvailable;readValid=w.readValid;attempts=w.attempts;status=w.status;
  logical=w.logical;command=w.command;value=w.value;}
};
// Separate proof: never populate the legacy A224 ACK with a B800 observation.
enum class PatchExitProof : uint8_t { None, FreshB800Bit6Clear };
struct PatchHandshakeSample {
 const char *context="none";bool enter=false,requestReturned=false,writeIssued=false;
 bool ackStarted=false,ackReadValid=false,postReadAttempted=false,postReadValid=false;
 AccessStatus requestStatus=AccessStatus::Ok,result=AccessStatus::Ok,postReadStatus=AccessStatus::Ok;
 unsigned ackAttempts=0;uint16_t ackValue=0,postReadValue=0;
 PatchTransportSample requestWait,postReadWait;
 PatchExitProof exitProof=PatchExitProof::None;
 bool exitStarted=false,exitReadValid=false,exitConfirmed=false;
 unsigned exitAttempts=0;uint16_t exitValue=0;AccessStatus exitStatus=AccessStatus::Unsupported;
};
struct PatchHandshakeDiagnostic {
 static constexpr unsigned capacity=6;
 unsigned count=0,stored=0;PatchHandshakeSample samples[capacity];
 PatchHandshakeSample *begin(const char *context,bool enter){
  ++count;if(stored==capacity)return nullptr;
  auto &v=samples[stored++];v=PatchHandshakeSample{};v.context=context;v.enter=enter;return &v;
 }
 template<class Sink> bool publish(Sink &s)const{
  bool ok=true;
  auto num=[&](const char *k,uint64_t v){if(!s.number(k,v))ok=false;};
  num("GU25PatchSchema",2);num("GU25PatchCount",count);num("GU25PatchStored",stored);
  num("GU25PatchTruncated",count!=stored);num("GU25PatchObservationAfterACKTimeoutOnly",1);
  num("GU25PatchAdditionalReadMaxWaitUS",250);num("GU25PatchACKLimit",1000);num("GU25PatchACKIntervalUS",100);
  for(unsigned i=0;i<stored;++i){
   const auto &v=samples[i];
   auto key=[&](const char *suffix,char *out){const char *p="GU25Patch";while(*p)*out++=*p++;*out++=char('0'+i);while(*suffix)*out++=*suffix++;*out=0;};
   auto n=[&](const char *k,uint64_t value){char name[96];key(k,name);num(name,value);};
   auto t=[&](const char *k,const char *value){char name[96];key(k,name);if(!s.text(name,value))ok=false;};
   t("Context",v.context);n("Enter",v.enter);n("RequestReturned",v.requestReturned);n("RequestStatus",unsigned(v.requestStatus));
   n("WriteIssued",v.writeIssued);n("Result",unsigned(v.result));
   const auto &w=v.requestWait;n("RequestWaitAvailable",w.available);n("RequestCommandAvailable",w.commandAvailable);
   if(w.commandAvailable)n("RequestCommand",w.command);
   if(w.available){n("RequestLogicalRegister",w.logical);n("RequestWaitStatus",w.status);n("RequestWaitAttempts",w.attempts);
    n("RequestWaitLimit",10);n("RequestWaitIntervalUS",25);n("RequestWaitReadAvailable",w.readAvailable);
    n("RequestWaitReadValid",w.readValid);if(w.readAvailable)n("RequestWaitReadValue",w.value);}
   n("MCUExitProof",unsigned(v.exitProof));n("MCUExitStarted",v.exitStarted);n("MCUExitConfirmed",v.exitConfirmed);
   if(v.exitStarted){n("MCUExitRegister",0xb800);n("MCUExitMask",0x40);n("MCUExitExpected",0);
    n("MCUExitAttempts",v.exitAttempts);n("MCUExitReadValid",v.exitReadValid);n("MCUExitStatus",unsigned(v.exitStatus));
    if(v.exitReadValid)n("MCUExitReadValue",v.exitValue);}
   n("ACKStarted",v.ackStarted);n("ACKAttempts",v.ackAttempts);n("ACKReadValid",v.ackReadValid);if(v.ackReadValid)n("ACKReadValue",v.ackValue);
   n("PostReadAttempted",v.postReadAttempted);n("PostReadStatus",unsigned(v.postReadStatus));n("PostReadValid",v.postReadValid);
   if(v.postReadValid)n("PostReadValue",v.postReadValue);
   const auto &p=v.postReadWait;n("PostWaitAvailable",p.available);
   if(p.available){n("PostWaitStatus",p.status);n("PostWaitAttempts",p.attempts);n("PostWaitReadAvailable",p.readAvailable);n("PostWaitReadValid",p.readValid);
    if(p.readAvailable)n("PostWaitReadValue",p.value);}
  }
  return ok;
 }
};
}
