// SPDX-License-Identifier: GPL-2.0-only
// Register sequences: Mieze a3dad34ed4f1e72805ce42f6d506f6917fdf79a0.
#include "GU24Program.hpp"
namespace realtek_next {
#define TRY(x) do { const auto s=(x); if(s!=AccessStatus::Ok)return s; } while(0)
namespace {
AccessStatus modify8(RegisterAccess &r,uint16_t off,uint8_t clear,uint8_t set){
 uint8_t v=0;if(!r.mmio().read8(off,v)||v==0xff)return AccessStatus::IOFailure;
 const auto wanted=uint8_t((v&~clear)|set);
 if(!r.mmio().write8(off,wanted)||!r.mmio().read8(off,v))return AccessStatus::IOFailure;
 return v==wanted?AccessStatus::Ok:AccessStatus::VerificationFailed;
}
AccessStatus wait8(RegisterAccess &r,uint16_t off,uint8_t mask,uint8_t wanted){
 for(unsigned n=0;n<100;++n){uint8_t v=0;
  if(!r.mmio().read8(off,v)||v==0xff)return AccessStatus::IOFailure;
  if((v&mask)==wanted)return AccessStatus::Ok;r.delay(100);}
 return AccessStatus::Timeout;
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
}
AccessStatus quiesceGU24(RegisterAccess &r){
 if(!r.mmio().write16(0x3c,0))return AccessStatus::IOFailure;
 TRY(unlocked(r,[&]()->AccessStatus{
  TRY(modify8(r,0x56,1,0));return modify8(r,0x53,0x80,0);
 }));
 TRY(modify8(r,0xf1,0x80,0)); // Disable silicon ASPM/CLKREQ handling.
 TRY(r.eriWrite(0x174,3,0xff));TRY(r.macWrite(0xe428,0x10)); // exitOOB/RealWoW off.
 TRY(modify8(r,0xf2,0,8));r.delay(2000); // RXDV gate.
 TRY(r.wait32(0x40,1U<<11,true,100,100));TRY(wait8(r,0xd3,0x30,0x30));
 if(!r.mmio().write8(0x37,0))return AccessStatus::IOFailure;
 return stopped(r);
}
AccessStatus exitOOBGU24(RegisterAccess &r){
 TRY(stopped(r));TRY(modify8(r,0xd3,0x80,0));
 TRY(r.macModify(0xe8de,0x4000,0));TRY(wait8(r,0xd3,2,2));
 TRY(r.macModify(0xe8de,0,0x8000));return wait8(r,0xd3,2,2);
}
AccessStatus applyMACPatchGU24(RegisterAccess &r){
 TRY(stopped(r));
 const auto result=unlocked(r,[&]()->AccessStatus{
  TRY(modify8(r,0x56,1,0));TRY(modify8(r,0x53,0x80,0));
  for(uint16_t a=0xfc28;a<=0xfc36;a+=2)TRY(checkedMAC(r,a,0));
  r.delay(3000);TRY(checkedMAC(r,0xfc26,0));
  struct Word{uint16_t address,value;};
  static const Word words[]={
#include "GU24MACPatch.inc"
  };
  // Verify the entire RAM payload before activating any breakpoint vector.
  for(const auto &w:words){if(w.address>=0xfc00)break;TRY(checkedMAC(r,w.address,w.value));}
  for(const auto &w:words)if(w.address>=0xfc00)TRY(checkedMAC(r,w.address,w.value));
  return AccessStatus::Ok;
 });
 if(result!=AccessStatus::Ok){
  // Disable patch dispatch on partial failure. Traffic and BME remain disabled by caller.
  for(uint16_t a=0xfc28;a<=0xfc36;a+=2)r.macWrite(a,0);
  r.macWrite(0xfc26,0);
 }
 return result;
}
AccessStatus configurePHYGU24(RegisterAccess &r){
 struct Restore{RegisterAccess &r;~Restore(){r.restorePHYBase();}}restore{r};
 // rtl8168_hw_phy_config's method 24 branch. ASPM remains disabled.
 TRY(r.pagedModify(0xbcc,0x14,0x100,0));
 TRY(r.pagedModify(0xa44,0x11,0,0x80));TRY(r.pagedModify(0xa44,0x11,0,0x40));
 TRY(r.phyParameter(0x8084,0x6000,0));
 TRY(r.pagedModify(0xa43,0x10,0,0x1000));TRY(r.pagedModify(0xa43,0x10,0,2));
 TRY(r.pagedModify(0xa43,0x10,0,1));TRY(r.phyParameter(0x8012,0,0x8000));
 TRY(r.pagedModify(0xc42,0x11,0x2000,0x4000));
 // Common method 21/22/24/25/26 EthPhyPPSW disable sequence; order is significant.
 TRY(r.pagedWrite(0xbcd,0x14,0x5065));TRY(r.pagedWrite(0xbcd,0x14,0xd065));
 TRY(r.pagedWrite(0xbc8,0x11,0x5655));TRY(r.pagedWrite(0xbcd,0x14,0x1065));
 TRY(r.pagedWrite(0xbcd,0x14,0x9065));TRY(r.pagedWrite(0xbcd,0x14,0x1065));
 // rtl8168_disable_EEE's GU24 branch; no GU25/H advanced EEE payload is reused.
 TRY(r.eriClearBits(0x1b0,3));TRY(r.pagedModify(0xa43,0x11,0x10,0));
 TRY(r.phyWrite(0xa5d0,0));
 // First common-base GU24 candidate: no unvalidated PAUSE advertisement.
 // Copper autoneg preserves these bits; clear them before its restart.
 TRY(r.pagedModify(0,4,0x0c00,0));
 auto pause=r.pagedRead(0,4);if(pause.status!=AccessStatus::Ok)return pause.status;
 if(pause.value&0x0c00)return AccessStatus::VerificationFailed;
 auto adv=r.phyRead(0xa5d0);if(adv.status!=AccessStatus::Ok)return adv.status;
 auto mode=r.pagedRead(0xa43,0x11);if(mode.status!=AccessStatus::Ok)return mode.status;
 return adv.value==0&&!(mode.value&0x10)?AccessStatus::Ok:AccessStatus::VerificationFailed;
}
AccessStatus configureMACGU24(RegisterAccess &r,PCIConfig &pci){
 TRY(stopped(r));
 return unlocked(r,[&]()->AccessStatus{
  // Mieze setupRTL8111, CFG_METHOD_24, standard MTU and ASPM disabled.
  TRY(modify8(r,0x54,0x03,0));TRY(modify8(r,0x56,0x81,0));
  TRY(modify8(r,0x53,0xa0,0));TRY(modify8(r,0xf1,0x80,0));
  TRY(r.eriClearBits(0xdc,0x10000)); // ERI byte 0xde bit 0: keep magic-packet-only off.
  TRY(r.eriWrite(0xc8,15,0x00080002));TRY(r.eriWrite(0xcc,1,0x38));
  TRY(r.eriWrite(0xd0,1,0x48));TRY(r.eriWrite(0xe8,15,0x00100006));
  uint8_t control=0;if(!pci.readConfig8(0x79,control)||control==0xff||
     !pci.writeConfig8(0x79,uint8_t((control&~0x70U)|0x50U)))return AccessStatus::IOFailure;
  if(!pci.writeConfig8(0x70f,0x27))return AccessStatus::IOFailure;
  TRY(r.eriClearBits(0xdc,1));TRY(r.eriSetBits(0xdc,1));
  TRY(modify8(r,0x1b,7,0));
  if(!r.mmio().write8(0x57,4))return AccessStatus::IOFailure; // TDFNR.
  TRY(modify8(r,0xd0,0,0xc0));TRY(modify8(r,0xf2,0,0x40));
  TRY(r.eriWrite(0xc0,3,0));TRY(r.eriWrite(0xb8,15,0));TRY(r.eriWrite(0x5f0,3,0x4f87));
  TRY(r.eriSetBits(0xd4,0x1f80));TRY(checkedMAC(r,0xc140,0xffff));
  TRY(r.eriClearBits(0x1b0,0x1003));TRY(r.eriModify(0x2fc,7,1));TRY(r.eriSetBits(0x1d0,2));
  // Explicitly drain inherited OOB/EEE state and release RXDV only after initialization.
  TRY(r.eriSetBits(0x1a8,0xfc000000)); // rtl8168_hw_init: ERI 0x1ab bits 2..7.
  TRY(modify8(r,0xf2,8,0));r.delay(2000);
  auto lpi=r.eriRead(0x1b0);if(lpi.status!=AccessStatus::Ok)return lpi.status;
  return (lpi.value&3)?AccessStatus::VerificationFailed:stopped(r);
 });
}
#undef TRY
}
