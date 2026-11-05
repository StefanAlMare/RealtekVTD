// SPDX-License-Identifier: GPL-2.0-only
#include "PhyInit.hpp"
namespace realtek_next {
#define TRY(x) do{auto operationStatus=(x);if(operationStatus!=AccessStatus::Ok)return operationStatus;}while(0)
AccessStatus PhyInit::enforceEEEOff() {
 // Internal Realtek PHY MMD AN/EEE_ADV maps to OCP 0xa5d0.
 TRY(r.phyWrite(0xa5d0,0));
 auto adv=r.phyRead(0xa5d0);if(adv.status!=AccessStatus::Ok)return adv.status;
 if(adv.value!=0)return AccessStatus::VerificationFailed;
 TRY(r.eriClearBits(0x1b0,3));
 auto lpi=r.eriRead(0x1b0);if(lpi.status!=AccessStatus::Ok)return lpi.status;
 return (lpi.value&3)?AccessStatus::VerificationFailed:AccessStatus::Ok;
}
AccessStatus PhyInit::configure(Chip chip) {
 struct Restore {RegisterAccess &r;~Restore(){r.restorePHYBase();}} restore{r};
 if(chip==Chip::Unknown)return AccessStatus::Unsupported;
 if(chip==Chip::RTL8168EVL){
  // Linux rtl8168e_2_hw_phy_config and its page-7/page-5 helpers.
  auto ext=[&](uint16_t page,unsigned reg,uint16_t clear,uint16_t set)->AccessStatus{
   TRY(r.mdioWrite(31,7));TRY(r.mdioWrite(0x1e,page));
   auto value=r.mdioRead(reg);if(value.status!=AccessStatus::Ok)return value.status;
   TRY(r.mdioWrite(reg,uint16_t((value.value&~clear)|set)));return r.mdioWrite(31,0);
  };
  auto parameter=[&](uint16_t number,uint16_t clear,uint16_t set)->AccessStatus{
   TRY(r.mdioWrite(31,5));TRY(r.mdioWrite(5,number));
   auto value=r.mdioRead(6);if(value.status!=AccessStatus::Ok)return value.status;
   TRY(r.mdioWrite(6,uint16_t((value.value&~clear)|set)));return r.mdioWrite(31,0);
  };
  TRY(ext(0x00ac,0x18,0xffff,0x0006));
  TRY(r.pagedWrite(3,9,0xa20f));
  TRY(parameter(0x8b5b,0xffff,0x9222));TRY(parameter(0x8b6d,0xffff,0x8000));
  TRY(parameter(0x8b76,0xffff,0x8000));
  TRY(r.mdioWrite(31,5));TRY(r.mdioWrite(5,0x8b80));
  auto corner=r.mdioRead(0x17);if(corner.status!=AccessStatus::Ok)return corner.status;
  TRY(r.mdioWrite(0x17,uint16_t(corner.value|6)));TRY(r.mdioWrite(31,0));
  TRY(ext(0x002d,0x18,0,0x10));TRY(r.pagedModify(0,0x14,0,0x8000));
  TRY(parameter(0x8b86,0,1));TRY(parameter(0x8b85,0,0x4000));
  // Required internal PHY setup is distinct from advertising EEE.
  TRY(ext(0x0020,0x15,0,0x0100));TRY(parameter(0x8b85,0,0x2000));
  TRY(r.pagedModify(3,0x19,0,1));TRY(r.pagedModify(3,0x10,0,0x0400));
  TRY(r.pagedModify(5,1,0,0x0100));
  return enforceEEEOff();
 }
 if(chip==Chip::RTL8411B) return r.pagedModify(0xa43,0x11,0,0x10);
 if(chip!=Chip::RTL8168H&&chip!=Chip::RTL8168M)return AccessStatus::Unsupported;
 TRY(r.phyParameter(0x808a,0x003f,0x000a));
 TRY(r.phyParameter(0x0811,0,0x0800));
 TRY(r.pagedModify(0xa42,0x16,0,2));
 TRY(r.pagedModify(0xa44,0x11,0,0x0800));
 TRY(r.macWrite(0xdd02,0x807d));
 auto d1=r.macRead(0xdd02);if(d1.status!=AccessStatus::Ok)return d1.status;
 auto d2=r.macRead(0xdd00);if(d2.status!=AccessStatus::Ok)return d2.status;
 uint16_t bias=uint16_t(((d2.value>>1)&0x7ff8)|(d2.value&7)|((d1.value&0x80)?0x8000:0));
 if(bias!=0xffff)TRY(r.pagedWrite(0xbcf,0x16,bias));
 auto corner=r.pagedRead(0xbcd,0x16);if(corner.status!=AccessStatus::Ok)return corner.status;
 uint16_t level=corner.value&15;level=level>3?uint16_t(level-3):0;
 TRY(r.pagedWrite(0xbcd,0x17,uint16_t(level*0x1111)));
 TRY(r.pagedModify(0xa44,0x11,0x80,0));
 TRY(r.pagedModify(0xa43,0x10,5,0)); // 10M PLL and ALDPS disabled.
 TRY(r.pagedModify(0xa43,0x11,0,0x10)); // Retain EEE-capable internal setup.
 return enforceEEEOff();
}
AccessStatus PhyInit::wake() {
 r.restorePHYBase();auto bmcr=r.mdioRead(0);if(bmcr.status!=AccessStatus::Ok)return bmcr.status;
 TRY(r.mdioWrite(0,uint16_t(bmcr.value&~0x0800)));
 r.delay(20000); // Internal PHY resume settling time from rtlgen_resume.
 return AccessStatus::Ok;
}
AccessStatus PhyInit::softReset() {
 r.restorePHYBase(); auto bmcr=r.mdioRead(0);if(bmcr.status!=AccessStatus::Ok)return bmcr.status;
 TRY(r.mdioWrite(0,uint16_t(bmcr.value|0x8000)));
 for(unsigned n=0;n<600;++n){r.delay(1000);auto v=r.mdioRead(0);if(v.status!=AccessStatus::Ok)return v.status;
  if(!(v.value&0x8000)){r.delay(1000);return AccessStatus::Ok;}}
 return AccessStatus::Timeout;
}
AccessStatus PhyInit::readState(PHYState &state) {
 r.restorePHYBase(); uint16_t *fields[]={&state.id1,&state.id2,&state.control,&state.bmsrFirst};
 const unsigned regs[]={2,3,0,1};
 for(unsigned i=0;i<4;++i){auto v=r.mdioRead(regs[i]);if(v.status!=AccessStatus::Ok)return v.status;*fields[i]=v.value;}
 auto status=r.mdioRead(1);if(status.status!=AccessStatus::Ok)return status.status;
 state.bmsrSecond=status.value;state.status=state.bmsrSecond;state.link=bool(state.bmsrSecond&4);
 auto eee=r.phyRead(0xa5d0);if(eee.status!=AccessStatus::Ok)return eee.status;
 state.eeeAdvertisement=eee.value;return AccessStatus::Ok;
}
#undef TRY
}
