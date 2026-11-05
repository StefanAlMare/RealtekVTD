// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "RegisterAccess.hpp"
namespace realtek_next {
// Stack-owned scalar evidence. No allocation, I/O, timing call or publication.
struct MACConfigDiagnostic {
 bool available=false,failed=false,complete=false;
 bool readValid=false,beforeValid=false,expectedValid=false,writtenValid=false,operationOK=false;
 bool unlockAttempted=false,unlockOK=false,relockAttempted=false,relockOK=false;
 // Persistent receipt survives subsequent begin() calls and first-failure capture.
 // WriteIssued means the guarded MMIO store returned success, not latch verification.
 bool c140Attempted=false,c140WriteIssued=false;
 AccessStatus c140Status=AccessStatus::Ok;
 unsigned stepIndex=0,width=0;uint32_t address=0,mask=0,targetMask=0;
 uint32_t before=0,observed=0,expected=0,written=0;
 AccessStatus status=AccessStatus::Ok;
 const char *step="not-reached",*space="none",*operation="none";
 void begin(const char *name,const char *bank,unsigned reg,unsigned bits,uint32_t effectiveMask){
  if(failed)return;available=true;++stepIndex;step=name;space=bank;address=reg;width=bits;mask=effectiveMask;
  targetMask=effectiveMask;readValid=beforeValid=expectedValid=writtenValid=false;
  before=observed=expected=written=0;operationOK=false;operation="none";status=AccessStatus::Ok;
 }
 AccessStatus fail(const char *op,AccessStatus s){
  if(!failed){failed=true;status=s;operation=op;operationOK=false;}return s;
 }
 void read(uint32_t v){observed=v;readValid=true;}
 void expect(uint32_t v){expected=v;expectedValid=true;}
 void write(uint32_t v){written=v;writtenValid=true;}
 template<class Sink> bool publish(Sink &s) const {
  bool ok=true;
#define N(k,v) do{if(!s.number("GU25MACCFG" k,uint64_t(v)))ok=false;}while(0)
#define T(k,v) do{if(!s.text("GU25MACCFG" k,v))ok=false;}while(0)
  N("Available",available);if(available){
   N("Schema",1);N("Failed",failed);N("Complete",complete);N("StepIndex",stepIndex);
   T("Step",step);T("Space",space);T("Operation",operation);N("OperationOK",operationOK);N("Status",unsigned(status));
   N("Register",address);N("Width",width);N("Mask",mask);N("TargetMask",targetMask);
   N("ReadValid",readValid);if(readValid)N("ReadValue",observed);
   N("BeforeValid",beforeValid);if(beforeValid)N("BeforeValue",before);
   N("ExpectedValid",expectedValid);if(expectedValid)N("ExpectedValue",expected);
   N("WrittenValid",writtenValid);if(writtenValid)N("WrittenValue",written);
   N("UnlockAttempted",unlockAttempted);N("UnlockWriteOK",unlockOK);
   N("RelockAttempted",relockAttempted);N("RelockWriteOK",relockOK);
  }
#undef N
#undef T
  if(available){
#define C(k,v) do{if(!s.number("GU25C140" k,uint64_t(v)))ok=false;}while(0)
   C("Schema",1);C("WriteAttempted",c140Attempted);C("WriteIssued",c140WriteIssued);
   C("StatusValid",c140Attempted);if(c140Attempted)C("WriteStatus",unsigned(c140Status));
   C("Register",0xc140);C("Width",16);C("RequestedValue",0xffff);
   C("ReadbackApplicable",0);C("ReadbackPerformed",0);C("ValueVerified",0);
#undef C
  }
  return ok;
 }
};
}
