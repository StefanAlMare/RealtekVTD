// SPDX-License-Identifier: GPL-2.0-only
#include "HardwareInit.hpp"
namespace realtek_next {
#define TRY(x) do{auto operationStatus=(x);if(operationStatus!=AccessStatus::Ok)return operationStatus;}while(0)
AccessStatus HardwareInit::modify8(uint16_t off,uint8_t clear,uint8_t set) {
 uint8_t v;if(!r.mmio().read8(off,v)||v==255)return AccessStatus::IOFailure;
 return r.mmio().write8(off,uint8_t((v&~clear)|set))?AccessStatus::Ok:AccessStatus::IOFailure;
}
AccessStatus HardwareInit::modify32(uint16_t off,uint32_t clear,uint32_t set) {
 uint32_t v;if(!r.mmio().read32(off,v)||v==0xffffffffU)return AccessStatus::IOFailure;
 return r.mmio().write32(off,(v&~clear)|set)?AccessStatus::Ok:AccessStatus::IOFailure;
}
AccessStatus HardwareInit::wait8(uint16_t off,uint8_t mask,uint8_t expected,unsigned attempts,unsigned interval) {
 for(unsigned n=0;n<attempts;++n){uint8_t v;
  if(!r.mmio().read8(off,v)||v==255)return AccessStatus::IOFailure;
  if((v&mask)==expected)return AccessStatus::Ok;r.delay(interval);}
 return AccessStatus::Timeout;
}
AccessStatus HardwareInit::verifyStopped() {uint8_t cmd;
 if(!r.mmio().read8(0x37,cmd)||cmd==255)return AccessStatus::IOFailure;
 return (cmd&0x0c)?AccessStatus::VerificationFailed:AccessStatus::Ok;
}
AccessStatus HardwareInit::preInitialize(Chip chip) {
 if(isGUProfile(chip))return AccessStatus::Unsupported;
 // Mask traffic interrupts before any hardware mutation.
 if(!r.mmio().write16(0x3c,0))return AccessStatus::IOFailure;
 if(chip==Chip::RTL8168EVL){
  // rtl_hw_initialize has no 8168g MCU/RXDV sequence for MAC34.
  if(!r.mmio().write8(0x37,0))return AccessStatus::IOFailure;
  return verifyStopped();
 }
 TRY(modify32(0xf0,0,1U<<19));r.delay(2000);
 TRY(r.wait32(0x40,1U<<11,true,100,42));
 TRY(wait8(0xd3,0x30,0x30,42,100));
 TRY(modify8(0x37,0x0c,0));r.delay(1000);
 TRY(modify8(0xd3,0x80,0));
 TRY(r.macModify(0xe8de,0x4000,0));
 TRY(wait8(0xd3,2,2,42,100));
 TRY(r.macModify(0xe8de,0,0x8000));
 TRY(wait8(0xd3,2,2,42,100));
 return verifyStopped();
}
AccessStatus HardwareInit::reset() {
 if(!r.mmio().write8(0x37,0x10))return AccessStatus::IOFailure;
 TRY(wait8(0x37,0x10,0,100,100));
 if(!r.mmio().write16(0x3c,0))return AccessStatus::IOFailure;
 return verifyStopped();
}
AccessStatus HardwareInit::entryLatency() {
 uint8_t v;
 if(pci.writeConfig8(0x70f,0x27)&&pci.readConfig8(0x70f,v)&&v==0x27)return AccessStatus::Ok;
 // Hardware CSI fallback for extended config, preserving the other bytes.
 if(function>7)return AccessStatus::InvalidAddress;
 const uint32_t command=0xf000U|(uint32_t(function)<<16)|0x70c;
 if(!r.mmio().write32(0x68,command))return AccessStatus::IOFailure;
 TRY(r.wait32(0x68,0x80000000U,true,10,100));
 uint32_t data;if(!r.mmio().read32(0x64,data))return AccessStatus::IOFailure;
 if(!r.mmio().write32(0x64,(data&0x00ffffffU)|0x27000000U)||
    !r.mmio().write32(0x68,command|0x80000000U))return AccessStatus::IOFailure;
 return r.wait32(0x68,0x80000000U,false,10,100);
}
AccessStatus HardwareInit::fifoAndFilter() {
 struct Setting{uint16_t address;uint8_t mask;uint32_t data;};
 const Setting settings[]={{0xc8,15,0x00080002},{0xe8,15,0x00100006},{0xcc,1,0x38},{0xd0,1,0x48}};
 for(const auto &s:settings)TRY(r.eriWrite(s.address,s.mask,s.data));
 TRY(entryLatency());TRY(r.eriClearBits(0xdc,1));return r.eriSetBits(0xdc,1);
}
AccessStatus HardwareInit::patch8411B() {
 for(unsigned addr=0xfc28;addr<=0xfc36;addr+=2)TRY(r.macWrite(addr,0));
 r.delay(3000);TRY(r.macWrite(0xfc26,0));
 static const uint16_t program[]={
#include "RTL8411BPatch.inc"
 };
 for(unsigned i=0;i<sizeof(program)/sizeof(program[0]);++i)TRY(r.macWrite(0xf800+2*i,program[i]));
 TRY(r.macWrite(0xfc26,0x8000));
 const uint16_t vectors[]={0x0743,0x0801,0x0be9,0x02fd,0x0c25,0x00a9,0x012d};
 for(unsigned i=0;i<7;++i)TRY(r.macWrite(0xfc2a+2*i,vectors[i]));return AccessStatus::Ok;
}
AccessStatus HardwareInit::configureMAC(Chip chip) {
 if(chip==Chip::Unknown||isGUProfile(chip))return AccessStatus::Unsupported;
 if(chip==Chip::RTL8168EVL)return configure8168EVL();
 TRY(verifyStopped());
 if(!r.mmio().write8(0x50,0xc0))return AccessStatus::IOFailure;
 // Relock on success and failure; a failed relock is reported on success.
 auto body=[&]() -> AccessStatus {
  struct Ephy{unsigned reg;uint16_t clear,set;};
  const Ephy g[]={{0,8,0},{12,0x37d0,0x820},{30,0,1},{25,0x8021,0},{30,0,0x2000},
                 {13,0x100,0x200},{0,0,0x80},{6,0,0x10},{4,0,0x10},{29,0,0x4000}};
  const Ephy h[]={{30,0x800,1},{29,0,0x800},{5,0xffff,0x2089},{6,0xffff,0x5881},
                 {4,0xffff,0x854a},{1,0xffff,0x68b}};
  if(chip==Chip::RTL8411B){
   TRY(fifoAndFilter());TRY(r.eriWrite(0x2f8,3,0x1d8f));
   TRY(modify32(0xf0,1U<<19,0));
   TRY(r.eriWrite(0xc0,3,0));TRY(r.eriWrite(0xb8,3,0));
   TRY(r.eriModify(0x2fc,6,1));TRY(r.eriClearBits(0x1b0,0x1000));TRY(modify8(0x54,2,0));
   for(const auto &e:g)TRY(r.ephyModify(e.reg,e.clear,e.set));
   return patch8411B();
  }
  for(const auto &e:h)TRY(r.ephyModify(e.reg,e.clear,e.set));
  TRY(fifoAndFilter());TRY(r.eriSetBits(0xdc,0x1c));TRY(r.eriWrite(0x5f0,3,0x4f87));
  TRY(modify32(0xf0,1U<<19,0));TRY(r.eriWrite(0xc0,3,0));TRY(r.eriWrite(0xb8,3,0));
  TRY(modify8(0xd0,0x40,0));TRY(modify8(0xf2,0x40,0));TRY(modify8(0xd0,0x80,0));
  TRY(r.eriClearBits(0x1b0,0x1000));TRY(modify8(0x54,2,0));
  auto saw=r.pagedRead(0xc42,0x13);if(saw.status!=AccessStatus::Ok)return saw.status;
  const auto divisor=saw.value&0x3fff;
  if(divisor)TRY(r.macModify(0xd412,0x0fff,uint16_t((16000000/divisor)&0xfff)));
  struct Ocp{uint16_t address,clear,set;};
  const Ocp trim[]={{0xe056,0xf0,0},{0xe052,0x6000,0x8008},{0xe0d6,0x1ff,0x17f},{0xd420,0xfff,0x47f}};
  for(const auto &o:trim)TRY(r.macModify(o.address,o.clear,o.set));
  TRY(r.macWrite(0xe63e,1));TRY(r.macWrite(0xe63e,0));TRY(r.macWrite(0xc094,0));TRY(r.macWrite(0xc09e,0));
  // No obsolete EEE MAC enable sequence. Also sanitize a warm-boot TX-LPI state.
  return updateLinkEEE(chip,false);
 };
 auto status=body(); const bool locked=r.mmio().write8(0x50,0);
 if(status!=AccessStatus::Ok)return status;
 if(!locked)return AccessStatus::IOFailure;
 return verifyStopped();
}
AccessStatus HardwareInit::updateLinkEEE(Chip chip,bool active) {
 if(chip==Chip::Unknown||isGUProfile(chip))return AccessStatus::Unsupported;
 const bool enable=chip==Chip::RTL8411B&&active;
 TRY(modify8(0x1b,7,0));
 TRY(r.eriModify(0x1b0,3,enable?3:0));
 auto state=r.eriRead(0x1b0);if(state.status!=AccessStatus::Ok)return state.status;
 return (state.value&3)==(enable?3U:0U)?AccessStatus::Ok:AccessStatus::VerificationFailed;
}
AccessStatus HardwareInit::disableClockRequest(){
 uint8_t next=0;if(!pci.readConfig8(0x34,next))return AccessStatus::IOFailure;
 uint64_t visited=0;
 for(unsigned count=0;next&&count<48;++count){
  if(next<0x40||(next&3))return AccessStatus::InvalidAddress;
  const uint64_t bit=uint64_t(1)<<(next/4);
  if(visited&bit)return AccessStatus::InvalidAddress;visited|=bit;
  uint8_t id=0,following=0;
  if(!pci.readConfig8(next,id)||!pci.readConfig8(uint16_t(next+1),following))return AccessStatus::IOFailure;
  if(id==0x10){
   uint16_t before=0,after=0;const uint16_t offset=uint16_t(next+0x10);
   if(!pci.readConfig16(offset,before)||before==0xffff)return AccessStatus::IOFailure;
   const uint16_t desired=uint16_t(before&~0x0100U);
   if(!pci.writeConfig16(offset,desired)||!pci.readConfig16(offset,after))return AccessStatus::IOFailure;
   return after==desired?AccessStatus::Ok:AccessStatus::VerificationFailed;
  }
  next=following;
 }
 return AccessStatus::Unsupported;
}
AccessStatus HardwareInit::configureLegacyReceiveAddress(){
 // rtl_rar_exgmac_set is required by Linux specifically for MAC34.
 // Mirror the current hardware MAC; it is never used to match the controller.
 uint32_t low=0;uint16_t high=0;
 if(!r.mmio().read32(0,low)||!r.mmio().read16(4,high))return AccessStatus::IOFailure;
 if((!low&&!high)||(low&1))return AccessStatus::VerificationFailed;
 const uint16_t offsets[]={0xe0,0xe4,0xf0,0xf4};
 const uint32_t values[]={low,uint32_t(high),(low&0xffffU)<<16,(low>>16)|(uint32_t(high)<<16)};
 for(unsigned i=0;i<4;++i){
  TRY(r.eriWrite(offsets[i],15,values[i]));
  const auto observed=r.eriRead(offsets[i]);if(observed.status!=AccessStatus::Ok)return observed.status;
  if(observed.value!=values[i])return AccessStatus::VerificationFailed;
 }
 return AccessStatus::Ok;
}
AccessStatus HardwareInit::configure8168EVL(){
 TRY(verifyStopped());
 if(!r.mmio().write8(0x50,0xc0))return AccessStatus::IOFailure;
 auto body=[&]()->AccessStatus{
  // Linux rtl_hw_aspm_clkreq_enable(false), with no unproven re-enable.
  TRY(modify8(0x53,0x80,0));TRY(modify8(0x56,1,0));
  TRY(entryLatency());
  struct Ephy{unsigned reg;uint16_t clear,set;};
  const Ephy settings[]={{0x09,0,0x0080},{0x19,0,0x0224},{0x00,0,0x0004},{0x0c,0x3df0,0x0200}};
  for(const auto &e:settings)TRY(r.ephyModify(e.reg,e.clear,e.set));
  TRY(r.eriWrite(0xc0,3,0));TRY(r.eriWrite(0xb8,15,0));
  TRY(r.eriWrite(0xc8,15,0x00100002));TRY(r.eriWrite(0xe8,15,0x00100006));
  TRY(r.eriSetBits(0x1d0,2));
  TRY(r.eriClearBits(0xdc,1));TRY(r.eriSetBits(0xdc,1));
  TRY(r.eriSetBits(0x1b0,0x10));
  TRY(r.eriWrite(0xcc,15,0x50));TRY(r.eriWrite(0xd0,15,0x07ff0060));
  TRY(disableClockRequest());
  TRY(modify8(0xd3,0x80,0));TRY(modify8(0xd0,0,0x40));
  TRY(modify32(0xf0,0,1U<<22));TRY(modify8(0x56,8,0));
  // No EEE TX-LPI or RX offload is enabled by this profile.
  TRY(r.eriClearBits(0x1b0,3));
  TRY(configureLegacyReceiveAddress());
  return verifyStopped();
 };
 const auto status=body();const bool locked=r.mmio().write8(0x50,0);
 if(status!=AccessStatus::Ok)return status;
 return locked?AccessStatus::Ok:AccessStatus::IOFailure;
}
#undef TRY
}
