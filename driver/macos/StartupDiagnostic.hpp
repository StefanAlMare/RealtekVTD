// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <stdint.h>
#include "OwnerDiagnostic.hpp"
#include "../src/MACConfigDiagnostic.hpp"
#include "../src/PHYConfigDiagnostic.hpp"

namespace realtek_next {
// Software evidence only. No hardware access, waiting, retry or owned provider.
// One immutable provider snapshot is published per event, best effort. The
// native publisher ignores allocation/setProperty failure and seals on exit.
struct ProviderOpenObservation {
 bool available=false,selfOpen=false,anyOpen=false,inactive=false;
};
struct StartupDiagnostic {
 MACConfigDiagnostic macConfig;PHYConfigDiagnostic phyConfig;
 static constexpr unsigned limit=64;
 unsigned sequence=0;bool sealed=false;
 bool probeEntered=false,scoreValid=false,probeReturned=false,probeAccepted=false,probeReturnedSelf=false;
 int32_t scoreBefore=0,scoreAfter=0;
 bool startEntered=false,superStartReturned=false,superStartOK=false;
 ProviderOpenObservation beforeSuper,afterSuper,beforeOpen,afterOpen;
 ProviderOpenObservation beforeProbe,afterProbe,beforeStart;
 uint64_t providerRegistryID=0;unsigned probeCalls=0,startCalls=0;
 char providerClass[64]{},providerName[96]{};
 bool superStopCalled=false,failureUnsafeDMA=false,failureRetainAdded=false,failureQuarantined=false;
 OpenOwnerSnapshot ownersBeforeSuper,ownersBeforeOpen;
 bool providerOpenReturned=false,providerOpenOK=false;
 bool workLoopCalled=false,workLoopOK=false,outputQueueCalled=false,outputQueueOK=false;
 bool gu25PatchVerified=false,gu25PHYMCUApplied=false;
 bool txConfigValid=false;uint32_t txConfig=0,xid=0,identityStatus=0,chip=0,cfgMethod=0;
 bool hardwareReturned=false,hardwareComplete=false,patchVerified=false;
 const char *hardwareStage="not-reached";uint32_t hardwareStatus=0;
 uint32_t programXID=0,programIdentityStatus=0,programChip=0,programCFGMethod=0;
 bool dmaReturned=false,mapperPresent=false;const char *dmaMode="not-reached";
 bool ringsReturned=false,ringsComplete=false,unsafeRetained=false;
 const char *ringsStage="not-reached";uint32_t ringsStatus=0;
 bool resourcesReturned=false,resourcesReady=false;
 bool irqReturned=false,irqAvailable=false;uint32_t irqIndex=0;
 bool configureReturned=false,configureSuperOK=false,statsReady=false;
 bool attachReturned=false,attached=false;
 bool next(){if(sealed||sequence>=limit)return false;++sequence;return true;}
 template<class Sink> bool write(Sink &s,uint64_t owner,const char *stage,const char *outcome,uint32_t status) const {
  bool ok=true;
#define N(k,v) do{if(!s.number(k,uint64_t(v)))ok=false;}while(0)
#define T(k,v) do{if(!s.text(k,v))ok=false;}while(0)
  N("Schema",1);T("Build","0.21.0-GU25-P14-IDX257-SCAN1-JTXFAST1-FC1-BME1-VLAN1-JUMBO1-TXCSUM1-TSO4-OPT1-PHYMCU1-ECN1-RXCSUM1-RXCSUM2-CSUM6-TSO6-SINGLE1-TXQOBS1-EMITCOPY1-RXNOWAIT1-TXPRECHECK1-RXFILL1-STOPSAFE1-MAC44-TXCSUM1-MAC44-FC1-MAC44-RXCSUM1-MAC44-TSO4-MAC44-TSO6-MAC44-NOIOMMU1-MAC44-VLANTSO1-MAC44-VLANTSO2-MAC44-VLANTSO3-MAC34-NOIOMMU1-MAC34-NOIOMMU2-FC1-MAC34-TXQ1-MAC34-TXQ2-DIAG-MAC34-TXQ3-DIAG-MAC34-TXQ4-MAC46-APPLEVTD1-DIAG-MAC46-FULL-APPLEVTD-ON-MAC46-FULL-NOIOMMU-MAC46-LPI1-MAC46-LPI2-MAC46-N64-TXPRECHECK-B");N("OwnerRegistryID",owner);
  N("Sequence",sequence);N("EventLimit",limit);T("Stage",stage);T("Outcome",outcome);N("Status",status);
  N("ProbeEntered",probeEntered);N("ProbeReturned",probeReturned);
  if(probeEntered){N("ProbeScoreValid",scoreValid);if(scoreValid){
   // Preserve the exact signed 32-bit bit pattern; collector decodes it.
   N("ProbeScoreBeforeU32",uint32_t(scoreBefore));N("ProbeScoreAfterU32",uint32_t(scoreAfter));}
   if(probeReturned){N("ProbeAccepted",probeAccepted);N("ProbeReturnedSelf",probeReturnedSelf);}}
  N("StartEntered",startEntered);N("SuperStartReturned",superStartReturned);
  if(superStartReturned)N("SuperStartOK",superStartOK);
  // Separate public software queries; these are not one atomic ownership read.
#define O(prefix,o) do{if(o.available){N(prefix "Available",1);N(prefix "Self",o.selfOpen);N(prefix "Any",o.anyOpen);N(prefix "Inactive",o.inactive);}}while(0)
  O("BeforeProbe",beforeProbe);O("AfterProbe",afterProbe);O("BeforeStart",beforeStart);
  O("BeforeSuper",beforeSuper);O("AfterSuper",afterSuper);
  O("BeforeOpen",beforeOpen);O("AfterOpen",afterOpen);
#undef O
  N("ProviderRegistryID",providerRegistryID);T("ProviderClass",providerClass);T("ProviderName",providerName);
  N("ProbeCalls",probeCalls);N("StartCalls",startCalls);N("SuperStopCalled",superStopCalled);
  N("FailureUnsafeDMA",failureUnsafeDMA);N("FailureRetainAdded",failureRetainAdded);N("FailureQuarantined",failureQuarantined);
  if(!ownersBeforeSuper.write(s,"OwnerBeforeSuper"))ok=false;
  if(!ownersBeforeOpen.write(s,"OwnerBeforeOpen"))ok=false;
  if(providerOpenReturned){N("ProviderOpenReturned",1);N("ProviderOpenOK",providerOpenOK);
   N("ProviderOpenAttempts",1);N("ProviderOpenOptions",0);T("ProviderOpenReturnType","bool");}
  if(workLoopCalled){N("WorkLoopCallback",1);N("WorkLoopReady",workLoopOK);}
  if(outputQueueCalled){N("OutputQueueCallback",1);N("OutputQueueReady",outputQueueOK);}
  if(txConfigValid){N("TxConfigValid",1);N("TxConfig",txConfig);N("XID",xid);
   N("IdentityStatus",identityStatus);N("Chip",chip);N("CFGMethod",cfgMethod);}
  if(hardwareReturned){N("HardwareResultAvailable",1);T("HardwareStage",hardwareStage);
   N("HardwareStatus",hardwareStatus);N("HardwareComplete",hardwareComplete);N("MACPatchVerified",patchVerified);
   N("ProgramXID",programXID);N("ProgramIdentityStatus",programIdentityStatus);
   N("ProgramChip",programChip);N("ProgramCFGMethod",programCFGMethod);N("GU25MACPatchVerified",gu25PatchVerified);N("GU25PHYMCUApplied",gu25PHYMCUApplied);if(!macConfig.publish(s))ok=false;if(!phyConfig.publish(s))ok=false;}
  if(dmaReturned){N("DMAResultAvailable",1);T("DMAMode",dmaMode);N("MapperPresent",mapperPresent);}
  if(ringsReturned){N("RingsResultAvailable",1);T("RingsStage",ringsStage);
   N("RingsStatus",ringsStatus);N("RingsComplete",ringsComplete);N("UnsafeRetained",unsafeRetained);}
  if(resourcesReturned){N("ResourcesResultAvailable",1);N("ResourcesReady",resourcesReady);}
  if(irqReturned){N("IRQResultAvailable",1);N("IRQAvailable",irqAvailable);if(irqAvailable)N("IRQIndex",irqIndex);}
  if(configureReturned){N("ConfigureResultAvailable",1);N("ConfigureSuperOK",configureSuperOK);N("StatsReady",statsReady);}
  if(attachReturned){N("AttachResultAvailable",1);N("InterfaceAttached",attached);}
#undef N
#undef T
  return ok;
 }
};
}
