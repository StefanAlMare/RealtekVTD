// SPDX-License-Identifier: GPL-2.0-only
// CFG25 register programs: Mieze a3dad34ed4f1e72805ce42f6d506f6917fdf79a0.
#include "GU25Program.hpp"
namespace realtek_next {
#define TRY(x) do { const auto s=(x); if(s!=AccessStatus::Ok)return s; } while(0)
namespace {
AccessStatus modify8(RegisterAccess &r,uint16_t off,uint8_t clear,uint8_t set){
 uint8_t v=0;if(!r.mmio().read8(off,v)||v==0xff)return AccessStatus::IOFailure;
 const auto wanted=uint8_t((v&~clear)|set);
 if(!r.mmio().write8(off,wanted)||!r.mmio().read8(off,v))return AccessStatus::IOFailure;
 return v==wanted?AccessStatus::Ok:AccessStatus::VerificationFailed;
}
AccessStatus stopped(RegisterAccess &r){
 uint8_t v=0xff;if(!r.mmio().read8(0x37,v)||v==0xff)return AccessStatus::IOFailure;
 return (v&0x0c)?AccessStatus::VerificationFailed:AccessStatus::Ok;
}
AccessStatus checkedMAC(RegisterAccess &r,uint16_t address,uint16_t wanted){
 TRY(r.macWrite(address,wanted));auto v=r.macRead(address);
 if(v.status!=AccessStatus::Ok)return v.status;
 return v.value==wanted?AccessStatus::Ok:AccessStatus::VerificationFailed;
}
template<class Body> AccessStatus unlocked(RegisterAccess &r,Body body){
 if(!r.mmio().write8(0x50,0xc0))return AccessStatus::IOFailure;
 const auto status=body();const bool locked=r.mmio().write8(0x50,0);
 return status!=AccessStatus::Ok?status:locked?AccessStatus::Ok:AccessStatus::IOFailure;
}
struct Restore {RegisterAccess &r;~Restore(){r.restorePHYBase();}};
AccessStatus patchRequest(RegisterAccess &r,bool on,PHYConfigDiagnostic *d,const char *context,bool criticalExit=false){
 auto *receipt=d?d->patch.begin(context,on):nullptr;
 // Observe the EXISTING B820 RMW transport; no extra access before/during ACK.
 RegisterWaitDiagnostic requestWait;
 auto *previous=receipt?r.observeWaits(&requestWait):nullptr;
 auto s=r.pagedModify(0xb82,0x10,on?0:0x10,on?0x10:0);
 if(receipt){
  r.observeWaits(previous);if(previous)*previous=requestWait;
  receipt->requestWait.save(requestWait);receipt->requestReturned=true;receipt->requestStatus=s;
  // phyWrite starts its wait only after the backend accepts the write command.
  receipt->writeIssued=requestWait.commandAvailable&&requestWait.logical==0xb820&&(requestWait.command&0x80000000U);
 }
 auto finish=[&](AccessStatus value){if(receipt)receipt->result=value;return value;};
 if(s!=AccessStatus::Ok)return finish(d?d->fail(on?"patch-enter-write":"patch-leave-write",s):s);
 const bool mcuExit=!on&&criticalExit;
 if(receipt){if(mcuExit)receipt->exitStarted=true;else receipt->ackStarted=true;}
 RegisterWaitDiagnostic handshake;handshake.begin(mcuExit?"PHY-MCU-LEAVE-B800-CLEAR":on?"PATCH-ENTER":"PATCH-LEAVE",
  on||mcuExit?0xb800:0xa224,0xb8,on||mcuExit?0x40:0x10,mcuExit?0:on?0x40:0x10,1000,100);
 // Realtek r8168 8.055.00 CFG21..37: critical exit requires a fresh B800
 // bit6 CLEAR after B820 request-clear. Other contexts retain the legacy loop.
 // Same bounded budget, and the same final delay on timeout.
 for(unsigned i=0;i<1000;++i){
  RegisterWaitDiagnostic transport;auto *old=mcuExit?r.observeWaits(&transport):nullptr;
  auto v=r.pagedRead(on||mcuExit?0xb80:0xa22,on||mcuExit?0x10:0x12);
  if(mcuExit){r.observeWaits(old);if(old)*old=transport;if(d)d->mcu.exitTransport=transport;}
  handshake.attempts=i+1;
  if(mcuExit&&receipt){receipt->exitAttempts=i+1;receipt->exitReadValid=false;receipt->exitStatus=v.status;}
  if(v.status!=AccessStatus::Ok){
   if(mcuExit&&d&&!d->failed){handshake.status=unsigned(v.status);d->latest=handshake;}
   return finish(d?d->fail(on?"patch-enter-read":"patch-leave-read",v.status):v.status);
  }
  handshake.sample(v.value);handshake.readValid=v.value!=0xffff;
  if(receipt){if(mcuExit){receipt->exitReadValid=handshake.readValid;receipt->exitValue=v.value;}
   else{receipt->ackAttempts=i+1;receipt->ackReadValid=handshake.readValid;receipt->ackValue=v.value;}}
  if(v.value==0xffff){if(receipt&&mcuExit)receipt->exitStatus=AccessStatus::IOFailure;
   if(d&&!d->failed){handshake.status=unsigned(AccessStatus::IOFailure);d->latest=handshake;}
   return finish(d?d->fail("invalid-handshake-read",AccessStatus::IOFailure):AccessStatus::IOFailure);}
  if(mcuExit?!(v.value&0x40):bool(v.value&(on?0x40:0x10))){
   if(mcuExit){if(receipt){receipt->exitProof=PatchExitProof::FreshB800Bit6Clear;receipt->exitConfirmed=true;receipt->exitStatus=AccessStatus::Ok;}
    if(d&&!d->failed)d->latest=handshake;}
   return finish(AccessStatus::Ok);
  }
  r.delay(100);
 }
 // B820 is already read by the reference RMW. One diagnostic read ONLY after
 // PATCH-LEAVE ACK timeout observes the request at that later instant. It cannot
 // prove earlier retention or distinguish an ignored write from reassertion.
 // Its bounded PHY transport may add up to 10 x 25us, never an ACK retry.
 if(!on&&receipt){
  receipt->postReadAttempted=true;RegisterWaitDiagnostic postWait;auto *old=r.observeWaits(&postWait);
  const auto observed=r.pagedRead(0xb82,0x10);r.observeWaits(old);receipt->postReadWait.save(postWait);
  receipt->postReadStatus=observed.status;receipt->postReadValid=observed.status==AccessStatus::Ok&&observed.value!=0xffff;
  if(observed.status==AccessStatus::Ok&&observed.value==0xffff)receipt->postReadStatus=AccessStatus::IOFailure;
  if(receipt->postReadValid)receipt->postReadValue=observed.value;
 }
 if(receipt&&mcuExit)receipt->exitStatus=AccessStatus::Timeout;
 if(d&&!d->failed){handshake.status=unsigned(AccessStatus::Timeout);d->latest=handshake;}
 // Preserve the primary timeout even if the later observation fails.
 return finish(d?d->fail(on?"patch-enter-ack":"patch-leave-ack",AccessStatus::Timeout):AccessStatus::Timeout);
}
template<class Body> AccessStatus withPatchRequest(RegisterAccess &r,Body body,PHYConfigDiagnostic *d=nullptr,const char *context="advanced-eee",bool criticalExit=false){
 if(d&&context[0]!='a')d->begin(context[0]=='o'?"ocp-power-enter":"phy-mcu-enter","PHY-OCP",0xb820,0x10,0x10,true);
 const auto enter=patchRequest(r,true,d,context);
 if(enter!=AccessStatus::Ok){
  if(d)d->cleanupAttempted=true;const auto cleanup=patchRequest(r,false,d,context,criticalExit);
  if(d){d->cleanupReturned=true;d->cleanupStatus=cleanup;}return enter;
 }
 const auto status=body();
 if(d&&status!=AccessStatus::Ok)d->fail("patch-body",status);
 if(d){d->cleanupAttempted=true;d->begin(context[0]=='a'?"advanced-eee-leave":context[0]=='o'?"ocp-power-leave":"phy-mcu-leave","PHY-OCP",0xb820,0x10,0,true);}
 const auto leave=patchRequest(r,false,d,context,criticalExit);
 if(d){d->cleanupReturned=true;d->cleanupStatus=leave;}
 return status!=AccessStatus::Ok?status:leave;
}
Read16 parameterRead(RegisterAccess &r,uint16_t parameter){
 auto s=r.pagedWrite(0xa43,0x13,parameter);if(s!=AccessStatus::Ok)return {s,0};
 return r.pagedRead(0xa43,0x14);
}
}
AccessStatus applyMACPatchGU25(RegisterAccess &r){
 TRY(stopped(r));
 const auto result=unlocked(r,[&]()->AccessStatus{
  TRY(modify8(r,0x56,1,0));TRY(modify8(r,0x53,0x80,0));
  for(uint16_t a=0xfc28;a<=0xfc36;a+=2)TRY(checkedMAC(r,a,0));
  r.delay(3000);TRY(checkedMAC(r,0xfc26,0));
  struct Word{uint16_t address,value;};
  static const Word words[]={
#include "GU25MACPatch.inc"
  };
  // Read back all 408 RAM words before dispatch. GU24's 119-pair table is never used.
  for(const auto &w:words)if(w.address<0xfc00)TRY(checkedMAC(r,w.address,w.value));
  for(const auto &w:words)if(w.address>=0xfc00&&w.address!=0xfc26)TRY(checkedMAC(r,w.address,w.value));
  return checkedMAC(r,0xfc26,0x8000);
 });
 if(result!=AccessStatus::Ok){
  r.macWrite(0xfc26,0);for(uint16_t a=0xfc28;a<=0xfc36;a+=2)r.macWrite(a,0);
 }
 return result;
}
AccessStatus disableOCPPowerSavingGU25(RegisterAccess &r,PHYConfigDiagnostic *diagnostic){
 Restore restore{r};auto v=r.pagedRead(0xc41,0x13);
 if(v.status!=AccessStatus::Ok)return v.status;
 if(v.value==0x0500)return AccessStatus::Ok;
 return withPatchRequest(r,[&]()->AccessStatus{
  TRY(r.pagedWrite(0xc41,0x13,0));TRY(r.pagedWrite(0xc41,0x13,0x0500));
  auto check=r.pagedRead(0xc41,0x13);if(check.status!=AccessStatus::Ok)return check.status;
  return check.value==0x0500?AccessStatus::Ok:AccessStatus::VerificationFailed;
 },diagnostic,"ocp-power");
}
AccessStatus configureEPHYGU25(RegisterAccess &r){
 TRY(stopped(r));TRY(r.ephyModify(0x00,8,0));TRY(r.ephyModify(0x0c,0x3ff0,0x0820));
 struct Word{unsigned address;uint16_t value;};
 const Word words[]={{0x19,0x7c00},{0x1e,0x20eb},{0x0d,0x1666},{0,0x10a3},{6,0xf050}};
 for(const auto &w:words){TRY(r.ephyWrite(w.address,w.value));
  auto v=r.ephyRead(w.address);if(v.status!=AccessStatus::Ok)return v.status;
  if(v.value!=w.value)return AccessStatus::VerificationFailed;}
 TRY(r.ephyModify(4,0,0x10));return r.ephyModify(0x1d,0x4000,0);
}
AccessStatus applyPHYMCUGU25(RegisterAccess &r,PHYConfigDiagnostic *diagnostic,const Identity *identity){
 // Reject identity mismatch before any access; no generic PHY helper relaxation.
 if(!identity||identity->status!=ProbeStatus::Supported||identity->chip!=Chip::RTL8168GU_Method25||
    identity->xid!=0x509||identity->linuxMacVersion!=42||cfgMethod(identity->chip)!=25)return AccessStatus::Unsupported;
 Restore restore{r};PHYMCUDiagnostic local;auto &m=diagnostic?diagnostic->mcu:local;
 m=PHYMCUDiagnostic{};m.initialAttempted=true;auto version=parameterRead(r,0x801e);
 m.initialStatus=version.status;m.initialValid=version.status==AccessStatus::Ok&&version.value!=0xffff;
 if(!m.initialValid){if(version.status==AccessStatus::Ok)m.initialStatus=AccessStatus::IOFailure;return m.initialStatus;}
 m.initialVersion=version.value;
 // NIC_RAMCODE_VERSION_CFG_METHOD_24 == 1, also selected by CFG25 in the reference.
 if(version.value==1){m.versionConfirmed=true;return AccessStatus::Ok;}
 m.programmingRequired=true;
 const auto patched=withPatchRequest(r,[&]()->AccessStatus{
  struct Word{unsigned reg;uint16_t value;};static const Word words[]={
#include "GU25PHYMCU.inc"
  };
  for(const auto &w:words)TRY(r.mdioWrite(w.reg,w.value));
  TRY(r.pagedModify(0xb82,0x17,1,0));
  TRY(r.pagedWrite(0xa43,0x13,0x8146));TRY(r.pagedWrite(0xa43,0x14,0));m.bodyComplete=true;return AccessStatus::Ok;
 },diagnostic,"phy-mcu",true);TRY(patched);
 TRY(r.pagedWrite(0xa43,0x13,0x801e));TRY(r.pagedWrite(0xa43,0x14,1));
 m.finalAttempted=true;version=parameterRead(r,0x801e);m.finalStatus=version.status;
 m.finalValid=version.status==AccessStatus::Ok&&version.value!=0xffff;
 if(!m.finalValid){if(version.status==AccessStatus::Ok)m.finalStatus=AccessStatus::IOFailure;return m.finalStatus;}
 m.finalVersion=version.value;m.versionConfirmed=version.value==1;
 if(!m.versionConfirmed)m.finalStatus=AccessStatus::VerificationFailed;
 return m.finalStatus;
}
AccessStatus configurePHYGU25(RegisterAccess &r,PHYConfigDiagnostic *diagnostic,const Identity *identity,bool criticalPHYMCUConfirmed){
 Restore restore{r};PHYConfigDiagnostic local;auto &d=diagnostic?*diagnostic:local;const auto history=d.patch;const auto mcu=d.mcu;d=PHYConfigDiagnostic{};d.patch=history;d.mcu=mcu;
 auto previous=diagnostic?r.observeWaits(&d.latest):nullptr;
 struct ObserverRestore{RegisterAccess &r;RegisterWaitDiagnostic *old;bool active;~ObserverRestore(){if(active)r.observeWaits(old);}} observerRestore{r,previous,diagnostic!=nullptr};
#define PHYOP(name,bank,address,call) do{d.begin(name,bank,address);const auto s=(call);if(s!=AccessStatus::Ok)return d.fail("register-operation",s);}while(0)
 PHYOP("bcc-bit8-clear","PHY-OCP",0xbcc8,r.pagedModify(0xbcc,0x14,0x100,0));
 PHYOP("a44-bit7-set","PHY-OCP",0xa442,r.pagedModify(0xa44,0x11,0,0x80));
 PHYOP("a44-bit6-set","PHY-OCP",0xa442,r.pagedModify(0xa44,0x11,0,0x40));
 PHYOP("parameter-8084","PHY-PARAM",0x8084,r.phyParameter(0x8084,0x6000,0));
 PHYOP("a43-bit12-set","PHY-OCP",0xa430,r.pagedModify(0xa43,0x10,0,0x1000));
 PHYOP("a43-bit1-set","PHY-OCP",0xa430,r.pagedModify(0xa43,0x10,0,2));
 PHYOP("a43-bit0-set","PHY-OCP",0xa430,r.pagedModify(0xa43,0x10,0,1));
 PHYOP("parameter-8012","PHY-PARAM",0x8012,r.phyParameter(0x8012,0,0x8000));
 PHYOP("bce-8860","PHY-OCP",0xbce4,r.pagedWrite(0xbce,0x12,0x8860));
 struct Param{uint16_t address,value;};const Param params[]={
 {0x80f3,0x8b00},{0x80f0,0x3a00},{0x80ef,0x0500},{0x80f6,0x6e00},
 {0x80ec,0x6800},{0x80ed,0x7c00},{0x80f2,0xf400},{0x80f4,0x8500},
 {0x8110,0xa800},{0x810f,0x1d00},{0x8111,0xf500},{0x8113,0x6100},
 {0x8115,0x9200},{0x810e,0x0400},{0x810c,0x7c00},{0x810b,0x5a00},
 {0x80d1,0xff00},{0x80cd,0x9e00},{0x80d3,0x0e00},{0x80d5,0xca00},{0x80d7,0x8400}};
 for(const auto &p:params)PHYOP("parameter-trim","PHY-PARAM",p.address,r.phyParameter(p.address,0xff00,p.value));
 PHYOP("ppsw-5065","PHY-OCP",0xbcd8,r.pagedWrite(0xbcd,0x14,0x5065));
 PHYOP("ppsw-d065","PHY-OCP",0xbcd8,r.pagedWrite(0xbcd,0x14,0xd065));
 PHYOP("ppsw-5655","PHY-OCP",0xbc82,r.pagedWrite(0xbc8,0x11,0x5655));
 PHYOP("ppsw-1065-first","PHY-OCP",0xbcd8,r.pagedWrite(0xbcd,0x14,0x1065));
 PHYOP("ppsw-9065","PHY-OCP",0xbcd8,r.pagedWrite(0xbcd,0x14,0x9065));
 PHYOP("ppsw-1065-last","PHY-OCP",0xbcd8,r.pagedWrite(0xbcd,0x14,0x1065));
 PHYOP("eee-lpi-clear","ERI",0x1b0,r.eriClearBits(0x1b0,3));
 PHYOP("eee-mode-clear","PHY-OCP",0xa432,r.pagedModify(0xa43,0x11,0x10,0));
 PHYOP("eee-advertisement-clear","PHY-OCP",0xa5d0,r.phyWrite(0xa5d0,0));
 // CFG25 Advanced EEE handshake and sequence are unchanged.
 d.begin("advanced-eee-enter","PHY-OCP",0xb820,0x10,0x10,true);
 const auto advancedStatus=withPatchRequest(r,[&]()->AccessStatus{
  PHYOP("advanced-eee-eri-clear","ERI",0x1e8,r.eriWrite(0x1e8,4,0));
  PHYOP("advanced-eee-bit-clear","PHY-OCP",0xa42c,r.pagedModify(0xa42,0x16,2,0));return AccessStatus::Ok;
 },&d);
 if(advancedStatus!=AccessStatus::Ok){
  if(advancedStatus!=AccessStatus::Timeout)return d.fail("advanced-eee",advancedStatus);
  auto &trial=d.trial;trial.attempted=true;trial.disposition="REFUSED_EVIDENCE";
  trial.xid=identity?identity->xid:0;trial.cfg=identity?cfgMethod(identity->chip):0;
  trial.criticalPHYMCUConfirmed=criticalPHYMCUConfirmed;trial.legacyWait=d.firstWait;
  const bool exactLeaveFailure=d.failed&&d.status==AccessStatus::Timeout&&trialTextEqual(d.step,"advanced-eee-leave")&&
   trialTextEqual(d.operation,"patch-leave-ack")&&d.cleanupReturned&&d.cleanupStatus==AccessStatus::Timeout;
  trial.eligible=eligibleEEEACKTrial(identity,criticalPHYMCUConfirmed,d.patch,d.firstWait,exactLeaveFailure);
  if(!trial.eligible)return d.fail("advanced-eee",advancedStatus);
  // Realtek r8168 8.055.00 uses B800 bit6 CLEAR to confirm PATCH-LEAVE,
  // including CFG25. Never substitute B820 request-clear for MCU exit.
  // One observation only, after the unchanged P10 timeout; no ACK retry.
  trial.mcuReadAttempted=true;auto *old=r.observeWaits(&trial.mcuWait);
  const auto exit=r.pagedRead(0xb80,0x10);r.observeWaits(old);
  trial.mcuReadStatus=exit.status;trial.mcuReadValid=exit.status==AccessStatus::Ok&&exit.value!=0xffff;
  if(exit.status==AccessStatus::Ok&&exit.value==0xffff)trial.mcuReadStatus=AccessStatus::IOFailure;
  if(trial.mcuReadValid)trial.mcuValue=exit.value;
  trial.mcuExitConfirmed=trial.mcuReadValid&&!(exit.value&0x40);
  trial.disposition="REFUSED_MCU_EXIT_UNCONFIRMED";
  if(!trial.mcuExitConfirmed)return d.fail("advanced-eee",advancedStatus);
  // Archive the legacy error, then resume normal checks with a fresh primary
  // diagnostic. The original PATCH-LEAVE receipt remains Result=Timeout.
  trial.continued=true;trial.disposition="ACK_TIMEOUT_UNCONFIRMED";
  const auto savedTrial=trial;const auto savedPatch=d.patch;const auto savedMCU=d.mcu;
  d=PHYConfigDiagnostic{};d.trial=savedTrial;d.patch=savedPatch;d.mcu=savedMCU;
 }

 PHYOP("pause-advertisement-clear","PHY-OCP",0xa408,r.pagedModify(0,4,0x0c00,0));
 d.begin("eee-advertisement-read","PHY-OCP",0xa5d0);auto adv=r.phyRead(0xa5d0);if(adv.status!=AccessStatus::Ok)return d.fail("read",adv.status);d.read(adv.value);
 d.begin("eee-mode-read","PHY-OCP",0xa432);auto mode=r.pagedRead(0xa43,0x11);if(mode.status!=AccessStatus::Ok)return d.fail("read",mode.status);d.read(mode.value);
 d.begin("advanced-eee-read","PHY-OCP",0xa42c);auto advanced=r.pagedRead(0xa42,0x16);if(advanced.status!=AccessStatus::Ok)return d.fail("read",advanced.status);d.read(advanced.value);
 d.begin("pause-advertisement-read","PHY-OCP",0xa408);auto pause=r.pagedRead(0,4);if(pause.status!=AccessStatus::Ok)return d.fail("read",pause.status);d.read(pause.value);
 // Keep P8's four reads before checking their values; preserve failure priority.
 if(adv.value!=0){d.begin("eee-advertisement-verify","PHY-OCP",0xa5d0,0xffff,0,true);d.read(adv.value);return d.fail("compare",AccessStatus::VerificationFailed);}
 if(mode.value&0x10){d.begin("eee-mode-verify","PHY-OCP",0xa432,0x10,0,true);d.read(mode.value);return d.fail("compare",AccessStatus::VerificationFailed);}
 if(advanced.value&2){d.begin("advanced-eee-verify","PHY-OCP",0xa42c,2,0,true);d.read(advanced.value);return d.fail("compare",AccessStatus::VerificationFailed);}
 if(pause.value&0x0c00){d.begin("pause-advertisement-verify","PHY-OCP",0xa408,0x0c00,0,true);d.read(pause.value);return d.fail("compare",AccessStatus::VerificationFailed);}
 d.complete=true;d.operation="complete";d.trial.configurationComplete=d.trial.continued;return AccessStatus::Ok;
#undef PHYOP
}
AccessStatus configureMACGU25(RegisterAccess &r,PCIConfig &pci,MACConfigDiagnostic *diagnostic){
 MACConfigDiagnostic local;auto &d=diagnostic?*diagnostic:local;d=MACConfigDiagnostic{};
 // Preserve P7 operations except the unsupported C140 retention readback.
 auto verifyStopped=[&](const char *name)->AccessStatus{
  d.begin(name,"MMIO",0x37,8,0x0c);d.expect(0);
  uint8_t v=0xff;if(!r.mmio().read8(0x37,v))return d.fail("read8",AccessStatus::IOFailure);
  d.read(v);if(v==0xff)return d.fail("invalid-read",AccessStatus::IOFailure);
  if(v&0x0c)return d.fail("compare",AccessStatus::VerificationFailed);
  d.operation="compare";d.operationOK=true;return AccessStatus::Ok;
 };
 auto change8=[&](const char *name,uint16_t reg,uint8_t clear,uint8_t set)->AccessStatus{
  d.begin(name,"MMIO",reg,8,0xff);d.targetMask=uint8_t(clear|set);
  uint8_t v=0;if(!r.mmio().read8(reg,v))return d.fail("read-before",AccessStatus::IOFailure);
  d.before=v;d.beforeValid=true;d.read(v);
  if(v==0xff)return d.fail("invalid-read-before",AccessStatus::IOFailure);
  const auto wanted=uint8_t((v&~clear)|set);d.expect(wanted);
  if(!r.mmio().write8(reg,wanted))return d.fail("write8",AccessStatus::IOFailure);
  d.write(wanted);d.readValid=false;
  if(!r.mmio().read8(reg,v))return d.fail("readback8",AccessStatus::IOFailure);
  d.read(v);if(v!=wanted)return d.fail("compare",AccessStatus::VerificationFailed);
  d.operation="compare";d.operationOK=true;return AccessStatus::Ok;
 };
 // Mieze a3dad34, CFG_METHOD_25: emit C140=FFFF without a retention readback.
 // macWrite validates the address and propagates the MMIO backend result;
 // it has no completion ACK or timeout. This is not a claim about W1C semantics.
 auto writeC140=[&]()->AccessStatus{
  d.begin("ocp-c140","MAC-OCP",0xc140,16,0);d.targetMask=0xffff;
  d.c140Attempted=true;d.c140Status=r.macWrite(0xc140,0xffff);
  if(d.c140Status!=AccessStatus::Ok)return d.fail("mac-write",d.c140Status);
  d.c140WriteIssued=true;d.write(0xffff);
  d.operation="mac-write-no-readback";d.operationOK=true;return AccessStatus::Ok;
 };
 auto s=verifyStopped("command-before");if(s!=AccessStatus::Ok)return s;
 d.begin("cfg-unlock","MMIO",0x50,8,0xff);d.expect(0xc0);d.unlockAttempted=true;
 d.unlockOK=r.mmio().write8(0x50,0xc0);
 if(!d.unlockOK)return d.fail("write8",AccessStatus::IOFailure);
 d.write(0xc0);d.operation="write8";d.operationOK=true;
 auto body=[&]()->AccessStatus{
#define CFG8(name,reg,clear,set) do{auto s=change8(name,reg,clear,set);if(s!=AccessStatus::Ok)return s;}while(0)
#define CFGOP(name,bank,reg,bits,effectiveMask,wanted,call) do{d.begin(name,bank,reg,bits,effectiveMask);d.expect(wanted);const auto s=(call);if(s!=AccessStatus::Ok)return d.fail("register-operation",s);d.operation="register-operation";d.operationOK=true;}while(0)
  CFG8("config3-clear",0x54,0x03,0);CFG8("config5-clear",0x56,0x81,0);
  CFG8("config2-clear",0x53,0xa0,0);CFG8("aspm-clear",0xf1,0x80,0);
  CFGOP("eri-dc-magic-clear","ERI",0xdc,32,0x10000,0,r.eriClearBits(0xdc,0x10000));
  CFGOP("eri-c8-fifo","ERI",0xc8,32,0xffffffff,0x00080002,r.eriWrite(0xc8,15,0x00080002));
  CFGOP("eri-cc-fifo","ERI",0xcc,32,0xff,0x38,r.eriWrite(0xcc,1,0x38));
  CFGOP("eri-d0-fifo","ERI",0xd0,32,0xff,0x48,r.eriWrite(0xd0,1,0x48));
  CFGOP("eri-e8-fifo","ERI",0xe8,32,0xffffffff,0x00100006,r.eriWrite(0xe8,15,0x00100006));
  d.begin("pci-79","PCI",0x79,8,0xff);uint8_t control=0;
  if(!pci.readConfig8(0x79,control))return d.fail("pci-read8",AccessStatus::IOFailure);
  d.read(control);d.before=control;d.beforeValid=true;
  if(control==0xff)return d.fail("invalid-pci-read",AccessStatus::IOFailure);
  const auto wanted=uint8_t((control&~0x70U)|0x50U);d.expect(wanted);
  if(!pci.writeConfig8(0x79,wanted))return d.fail("pci-write8",AccessStatus::IOFailure);
  d.write(wanted);d.operation="pci-write8";d.operationOK=true;
  d.begin("pci-70f","PCI",0x70f,8,0xff);d.expect(0x27);
  if(!pci.writeConfig8(0x70f,0x27))return d.fail("pci-write8",AccessStatus::IOFailure);
  d.write(0x27);d.operation="pci-write8";d.operationOK=true;
  d.begin("txconfig-bit7","MMIO",0x40,32,0xffffffff);uint32_t tx=0;
  if(!r.mmio().read32(0x40,tx))return d.fail("read32",AccessStatus::IOFailure);
  d.read(tx);d.before=tx;d.beforeValid=true;
  if(tx==0xffffffffU)return d.fail("invalid-read32",AccessStatus::IOFailure);
  d.expect(tx|0x80U);if(!r.mmio().write32(0x40,tx|0x80U))return d.fail("write32",AccessStatus::IOFailure);
  d.write(tx|0x80U);d.operation="write32";d.operationOK=true;
  CFGOP("eri-dc-filter-clear","ERI",0xdc,32,1,0,r.eriClearBits(0xdc,1));
  CFGOP("eri-dc-filter-set","ERI",0xdc,32,1,1,r.eriSetBits(0xdc,1));
  CFG8("led-clear",0x1b,7,0);
  d.begin("tdfnr","MMIO",0x57,8,0xff);d.expect(4);
  if(!r.mmio().write8(0x57,4))return d.fail("write8",AccessStatus::IOFailure);
  d.write(4);d.operation="write8";d.operationOK=true;
  CFG8("dllpr-set",0xd0,0,0xc0);CFG8("misc1-set",0xf2,0,0x40);
  CFGOP("eri-c0-clear","ERI",0xc0,32,0xffff,0,r.eriWrite(0xc0,3,0));
  CFGOP("eri-b8-clear","ERI",0xb8,32,0xffffffff,0,r.eriWrite(0xb8,15,0));
  CFGOP("eri-5f0","ERI",0x5f0,32,0xffff,0x4f87,r.eriWrite(0x5f0,3,0x4f87));
  CFGOP("eri-d4-set","ERI",0xd4,32,0x1f80,0x1f80,r.eriSetBits(0xd4,0x1f80));
  auto checked=writeC140();if(checked!=AccessStatus::Ok)return checked;
  CFGOP("eri-lpi-clear","ERI",0x1b0,32,0x1003,0,r.eriClearBits(0x1b0,0x1003));
  CFGOP("eri-2fc","ERI",0x2fc,32,7,1,r.eriModify(0x2fc,7,1));
  CFGOP("eri-1d0","ERI",0x1d0,32,2,2,r.eriSetBits(0x1d0,2));
  CFG8("rxdv-release",0xf2,8,0);r.delay(2000);
  d.begin("lpi-readback","ERI",0x1b0,32,3);d.expect(0);
  auto lpi=r.eriRead(0x1b0);if(lpi.status!=AccessStatus::Ok)return d.fail("eri-read",lpi.status);
  d.read(lpi.value);if(lpi.value&3)return d.fail("compare",AccessStatus::VerificationFailed);
  d.operation="compare";d.operationOK=true;
  return verifyStopped("command-after");
#undef CFGOP
#undef CFG8
 };
 const auto status=body();
 // Relock has exactly P6's write-only semantics; preserve the primary failure.
 d.relockAttempted=true;const bool locked=r.mmio().write8(0x50,0);d.relockOK=locked;
 if(status!=AccessStatus::Ok)return status;
 if(!locked){d.begin("cfg-relock","MMIO",0x50,8,0xff);d.expect(0);return d.fail("write8",AccessStatus::IOFailure);}
 d.complete=true;return AccessStatus::Ok;
}
#undef TRY
}
