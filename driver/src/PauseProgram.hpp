// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "RegisterAccess.hpp"
namespace realtek_next {
constexpr uint16_t kPauseAdvertisementMask=0x0c00;
class PauseProgram {
 RegisterAccess &r;
public:
 explicit PauseProgram(RegisterAccess &access):r(access){}
 bool read(uint16_t &bits){
  const auto v=r.phyRead(0xa408);
  if(v.status!=AccessStatus::Ok||v.value==0xffff||(v.value&0x1f)!=1)return false;
  bits=v.value&kPauseAdvertisementMask;return true;
 }
 bool apply(uint16_t bits,bool forceRestart,bool &restarted,bool &transitionSeen){
  restarted=transitionSeen=false;
  if(bits&~kPauseAdvertisementMask)return false;
  const auto adv=r.phyRead(0xa408),control=r.phyRead(0xa400);
  if(adv.status!=AccessStatus::Ok||control.status!=AccessStatus::Ok||adv.value==0xffff||
     (adv.value&0x1f)!=1||control.value==0xffff||(control.value&0xcc00)||!(control.value&0x1000))return false;
  const auto desired=uint16_t((adv.value&~kPauseAdvertisementMask)|bits);
  const bool changed=desired!=adv.value;
  if(changed&&r.phyWrite(0xa408,desired)!=AccessStatus::Ok)return false;
  const auto check=r.phyRead(0xa408);
  if(check.status!=AccessStatus::Ok||check.value!=desired)return false;
  if(!changed&&!forceRestart)return true;
  const auto restart=uint16_t(control.value|0x1200);
  if(r.phyWrite(0xa400,restart)!=AccessStatus::Ok)return false;
  restarted=true;
  const auto bmcr=r.phyRead(0xa400),bmsr=r.phyRead(0xa402);
  if(bmcr.status!=AccessStatus::Ok||bmsr.status!=AccessStatus::Ok||bmcr.value==0xffff||bmsr.value==0xffff||
     (bmcr.value&~0x0200U)!=(restart&~0x0200U))return false;
  // Witness the new AN epoch; a later completed/agreeing observation may report PAUSE.
  transitionSeen=bool(bmcr.value&0x0200)||!(bmsr.value&0x0020)||!(bmsr.value&4);
  return true;
 }
};
}
