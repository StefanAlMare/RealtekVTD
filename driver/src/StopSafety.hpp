// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <stdint.h>
namespace realtek_next {
// Scope is fixed at startup, after exact identity and DMA mode selection.
constexpr bool stopSafetyGU25(unsigned mac,unsigned cfg,unsigned xid,bool nativeReady){
 return nativeReady&&mac==42&&cfg==25&&xid==0x509;
}
// Separate exact mapper eligibility; never reinterpret nativeReady as mapper.
constexpr bool stopSafetyMAC46VTD(unsigned mac,unsigned cfg,unsigned xid,bool initialized,
                                bool dmaReady,bool mapper,bool resourcesReady){
 return mac==46&&cfg==0&&xid==0x541&&initialized&&dmaReady&&mapper&&resourcesReady;
}
// Separate native64 proof, never mapper absence alone. The caller verifies
// exact PCI/revision, firmware/init, completed native mode and prepared shape.
constexpr bool stopSafetyMAC46Native64(unsigned mac,unsigned cfg,unsigned xid,bool initialized,
                                     bool dmaReady,bool mapper,bool native64Ready,bool resourcesReady){
 return mac==46&&cfg==0&&xid==0x541&&initialized&&dmaReady&&!mapper&&native64Ready&&resourcesReady;
}
struct StopSafetyReceipt {
 enum:uint32_t { IRQWrite=1,MACWrite=2,MACRead=4,MACActive=8,PCIIdentity=16,
  PCIBefore=32,PCIAfter=64,BMEBackend=128,BMEStillSet=256,TerminalCleanup=512 };
 bool irqAttempted=false,irqWritten=false,macAttempted=false,macWritten=false;
 bool macReadAttempted=false,macReadReturned=false,macReadValid=false,macStopped=false;
 uint8_t mac=0xff;
 bool identityReadReturned=false,identityValid=false;uint32_t identity=0xffffffffU;
 bool beforeReadReturned=false,beforeValid=false,afterReadReturned=false,afterValid=false;
 uint16_t before=0xffff,after=0xffff;
 bool bmeAttempted=false,bmeReturnedOK=false,bmeOffObserved=false;
 uint32_t bmeStatus=0,failures=0;
};
struct StopSafety {
 bool enabled=false,latched=false;uint64_t attempts=0;StopSafetyReceipt last,firstFailure;
 void record(const StopSafetyReceipt &r){last=r;if(attempts!=UINT64_MAX)++attempts;
  if(r.failures&&!latched){firstFailure=r;latched=true;}}
 template<class Sink>void write(Sink &s)const{
  s.setProperty("STOPSAFE1Schema",1,32);s.setProperty("STOPSAFE1Policy",enabled);
  s.setProperty("STOPSAFE1Retained",latched);s.setProperty("STOPSAFE1Attempts",attempts,64);
  s.setProperty("STOPSAFE1LastFailures",last.failures,32);s.setProperty("STOPSAFE1FirstFailures",firstFailure.failures,32);
  s.setProperty("STOPSAFE1IRQAttempted",last.irqAttempted);s.setProperty("STOPSAFE1IRQWritten",last.irqWritten);
  s.setProperty("STOPSAFE1MACAttempted",last.macAttempted);s.setProperty("STOPSAFE1MACWritten",last.macWritten);
  s.setProperty("STOPSAFE1MACReadAttempted",last.macReadAttempted);s.setProperty("STOPSAFE1MACReadReturned",last.macReadReturned);
  s.setProperty("STOPSAFE1MACReadValid",last.macReadValid);s.setProperty("STOPSAFE1MACStopped",last.macStopped);s.setProperty("STOPSAFE1MAC",last.mac,8);
  s.setProperty("STOPSAFE1PCIIdentityValid",last.identityValid);s.setProperty("STOPSAFE1PCIIdentity",last.identity,32);
  s.setProperty("STOPSAFE1PCIBeforeReturned",last.beforeReadReturned);s.setProperty("STOPSAFE1PCIBeforeValid",last.beforeValid);s.setProperty("STOPSAFE1PCIBefore",last.before,16);
  s.setProperty("STOPSAFE1PCIAfterReturned",last.afterReadReturned);s.setProperty("STOPSAFE1PCIAfterValid",last.afterValid);s.setProperty("STOPSAFE1PCIAfter",last.after,16);
  s.setProperty("STOPSAFE1BMEAttempted",last.bmeAttempted);s.setProperty("STOPSAFE1BMEReturnedOK",last.bmeReturnedOK);s.setProperty("STOPSAFE1BMEStatus",last.bmeStatus,32);s.setProperty("STOPSAFE1BMEOffObserved",last.bmeOffObserved);
  // Freeze the first refusal separately from subsequent containment attempts.
  s.setProperty("STOPSAFE1FirstMACWritten",firstFailure.macWritten);s.setProperty("STOPSAFE1FirstMACReadValid",firstFailure.macReadValid);s.setProperty("STOPSAFE1FirstMAC",firstFailure.mac,8);
  s.setProperty("STOPSAFE1FirstPCIAfterValid",firstFailure.afterValid);s.setProperty("STOPSAFE1FirstPCIAfter",firstFailure.after,16);
  s.setProperty("STOPSAFE1FirstBMEStatus",firstFailure.bmeStatus,32);
  s.setProperty("STOPSAFE1FirstIRQAttempted",firstFailure.irqAttempted);s.setProperty("STOPSAFE1FirstIRQWritten",firstFailure.irqWritten);
  s.setProperty("STOPSAFE1FirstMACAttempted",firstFailure.macAttempted);s.setProperty("STOPSAFE1FirstMACReadAttempted",firstFailure.macReadAttempted);s.setProperty("STOPSAFE1FirstMACReadReturned",firstFailure.macReadReturned);s.setProperty("STOPSAFE1FirstMACStopped",firstFailure.macStopped);
  s.setProperty("STOPSAFE1FirstIdentityReadReturned",firstFailure.identityReadReturned);s.setProperty("STOPSAFE1FirstIdentityValid",firstFailure.identityValid);s.setProperty("STOPSAFE1FirstIdentity",firstFailure.identity,32);
  s.setProperty("STOPSAFE1FirstPCIBeforeReturned",firstFailure.beforeReadReturned);s.setProperty("STOPSAFE1FirstPCIBeforeValid",firstFailure.beforeValid);s.setProperty("STOPSAFE1FirstPCIBefore",firstFailure.before,16);
  s.setProperty("STOPSAFE1FirstPCIAfterReturned",firstFailure.afterReadReturned);s.setProperty("STOPSAFE1FirstBMEAttempted",firstFailure.bmeAttempted);s.setProperty("STOPSAFE1FirstBMEReturnedOK",firstFailure.bmeReturnedOK);s.setProperty("STOPSAFE1FirstBMEOffObserved",firstFailure.bmeOffObserved);
 }
};
}
