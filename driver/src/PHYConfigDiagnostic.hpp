// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "RegisterAccess.hpp"
#include "PatchHandshakeDiagnostic.hpp"
#include "EEEACKTrial.hpp"
namespace realtek_next {
// Version and transport observations of the critical MCU path only; no I/O here.
struct PHYMCUDiagnostic {
 bool initialAttempted=false,initialValid=false,programmingRequired=false,bodyComplete=false;
 bool finalAttempted=false,finalValid=false,versionConfirmed=false;
 uint16_t initialVersion=0,finalVersion=0;
 AccessStatus initialStatus=AccessStatus::Unsupported,finalStatus=AccessStatus::Unsupported;
 RegisterWaitDiagnostic exitTransport;
 template<class Sink>bool publish(Sink &s)const{
  bool ok=true;auto n=[&](const char*k,uint64_t v){if(!s.number(k,v))ok=false;};
  n("PHYMCU1VersionInitialAttempted",initialAttempted);n("PHYMCU1VersionInitialValid",initialValid);
  n("PHYMCU1VersionInitialStatus",unsigned(initialStatus));if(initialValid)n("PHYMCU1VersionInitial",initialVersion);
  n("PHYMCU1ProgrammingRequired",programmingRequired);n("PHYMCU1BodyComplete",bodyComplete);
  n("PHYMCU1VersionFinalAttempted",finalAttempted);n("PHYMCU1VersionFinalValid",finalValid);
  n("PHYMCU1VersionFinalStatus",unsigned(finalStatus));if(finalValid)n("PHYMCU1VersionFinal",finalVersion);
  n("PHYMCU1VersionConfirmed",versionConfirmed);n("PHYMCU1ExitTransportAvailable",exitTransport.available);
  if(exitTransport.available){n("PHYMCU1ExitTransportStatus",exitTransport.status);n("PHYMCU1ExitTransportAttempts",exitTransport.attempts);
   n("PHYMCU1ExitTransportRegister",exitTransport.logical);n("PHYMCU1ExitTransportReadValid",exitTransport.readValid);
   n("PHYMCU1ExitTransportReadAvailable",exitTransport.readAvailable);if(exitTransport.readAvailable)n("PHYMCU1ExitTransportValue",exitTransport.value);}
  return ok;
 }
};
struct PHYConfigDiagnostic {
 bool available=false,failed=false,complete=false,readValid=false,expectedValid=false;
 bool cleanupAttempted=false,cleanupReturned=false;
 unsigned stepIndex=0;uint32_t reg=0,mask=0,expected=0,observed=0;
 AccessStatus status=AccessStatus::Ok,cleanupStatus=AccessStatus::Ok;
 const char *step="not-reached",*space="none",*operation="none";
 RegisterWaitDiagnostic latest,firstWait;
 PatchHandshakeDiagnostic patch;
 EEEACKTrial trial;
 PHYMCUDiagnostic mcu;
 void begin(const char *name,const char *bank,uint32_t address,uint32_t m=0,uint32_t want=0,bool hasExpected=false){
  if(failed)return;available=true;++stepIndex;step=name;space=bank;reg=address;mask=m;expected=want;
  expectedValid=hasExpected;readValid=false;observed=0;operation="operation";latest=RegisterWaitDiagnostic{};
 }
 AccessStatus fail(const char *op,AccessStatus s){
  if(!failed){failed=true;status=s;operation=op;firstWait=latest;}return s;
 }
 void read(uint32_t v){if(!failed){observed=v;readValid=true;}}
 template<class Sink> bool publish(Sink &s) const {
  bool ok=true;
#define N(k,v) do{if(!s.number("GU25PHY" k,uint64_t(v)))ok=false;}while(0)
#define T(k,v) do{if(!s.text("GU25PHY" k,v))ok=false;}while(0)
  N("Available",available);if(available){
   N("Schema",1);N("Failed",failed);N("Complete",complete);N("Status",unsigned(status));N("StepIndex",stepIndex);
   T("Step",step);T("Space",space);T("Operation",operation);N("Register",reg);N("Mask",mask);
   N("ExpectedValid",expectedValid);if(expectedValid)N("ExpectedValue",expected);
   N("ReadValid",readValid);if(readValid)N("ReadValue",observed);
   N("CleanupAttempted",cleanupAttempted);N("CleanupReturned",cleanupReturned);if(cleanupReturned)N("CleanupStatus",unsigned(cleanupStatus));
   const auto &w=failed?firstWait:latest;N("WaitAvailable",w.available);
   if(w.available){T("WaitKind",w.kind);N("WaitLogicalRegister",w.logical);N("WaitMMIORegister",w.port);
    N("WaitMask",w.mask);N("WaitExpected",w.expected);N("WaitAttempts",w.attempts);N("WaitLimit",w.limit);
    N("WaitIntervalUS",w.intervalUs);N("WaitStatus",w.status);N("WaitReadAvailable",w.readAvailable);N("WaitReadValid",w.readValid);
    if(w.readAvailable){N("WaitReadValue",w.value);N("WaitSampleAttempt",w.sampleAttempt);}
    N("WaitCommandAvailable",w.commandAvailable);if(w.commandAvailable)N("WaitCommand",w.command);
   }
  }
#undef N
#undef T
  return mcu.publish(s)&&trial.publish(s)&&patch.publish(s)&&ok;
 }
};
}
