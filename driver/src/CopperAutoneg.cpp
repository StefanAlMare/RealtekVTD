// SPDX-License-Identifier: GPL-2.0-only
#include "CopperAutoneg.hpp"
namespace realtek_next {
AccessStatus readCopperCapabilities(RegisterAccess &r,AutonegResult &out){
 out=AutonegResult{};r.restorePHYBase();out.stage="bmsr-first";
 auto first=r.mdioRead(1);out.status=first.status;if(first.status!=AccessStatus::Ok)return out.status;
 out.capabilities.bmsrFirst=first.value;out.valid|=1;
 out.stage="bmsr-second";auto second=r.mdioRead(1);out.status=second.status;
 if(second.status!=AccessStatus::Ok)return out.status;
 out.capabilities.bmsr=second.value;out.valid|=2;
 if(second.value&0x0100){out.stage="estatus";auto ext=r.mdioRead(15);out.status=ext.status;
  if(ext.status!=AccessStatus::Ok)return out.status;
  out.capabilities.estatus=ext.value;out.capabilities.estatusRead=true;out.valid|=4;}
 out.capabilities.valid=true;out.stage="capabilities-ready";
 return out.status=AccessStatus::Ok;
}
AccessStatus configureCopperAutoneg(RegisterAccess &r,AutonegResult &out,bool allow1000Half){
 r.restorePHYBase();out.configured=false;
 if(!out.capabilities.valid||!out.capabilities.autoneg()||
    !(out.capabilities.baseAdvertisement()|out.capabilities.gigabitAdvertisement(allow1000Half))){
  out.stage="unsupported-capabilities";return out.status=AccessStatus::Unsupported;}
#define READ(reg,field,bit,phase) do{out.stage=phase;auto v=r.mdioRead(reg);out.status=v.status; if(v.status!=AccessStatus::Ok)return out.status;out.field=v.value;out.valid|=bit;}while(0)
#define WRITE(reg,value,phase) do{out.stage=phase;out.status=r.mdioWrite(reg,value); if(out.status!=AccessStatus::Ok)return out.status;}while(0)
 READ(4,anarBefore,8,"anar-read");
 // A Fast Ethernet PHY without ESTATEN need not implement reg 9.
 // With extended status, replace gigabit bits even when no T mode is reported.
 out.ctrl1000Accessed=out.capabilities.estatusRead;
 if(out.ctrl1000Accessed)READ(9,ctrl1000Before,32,"ctrl1000-read");
 const auto adv=CopperAdvertisement::construct(out.capabilities,out.anarBefore,out.ctrl1000Before,allow1000Half);
 WRITE(4,adv.anar,"anar-write");
 if(out.ctrl1000Accessed)WRITE(9,adv.ctrl1000,"ctrl1000-write");
 READ(4,anarAfter,16,"anar-readback");
 if((out.anarAfter&0x01e0)!=(adv.anar&0x01e0)){
  out.stage="anar-verify";return out.status=AccessStatus::VerificationFailed;}
 if(out.ctrl1000Accessed){
  READ(9,ctrl1000After,64,"ctrl1000-readback");
  if((out.ctrl1000After&0x0300)!=(adv.ctrl1000&0x0300)){
   out.stage="ctrl1000-verify";return out.status=AccessStatus::VerificationFailed;}}
 READ(0,bmcrBefore,128,"bmcr-read");
 // Never submit a second reset, even if hardware unexpectedly reports one.
 if(out.bmcrBefore&0x8000){out.stage="bmcr-reset-active";return out.status=AccessStatus::VerificationFailed;}
 out.bmcrRestart=uint16_t(out.bmcrBefore|0x1200);
 WRITE(0,out.bmcrRestart,"bmcr-restart");out.valid|=256;
 out.configured=true;out.stage="configured";return out.status=AccessStatus::Ok;
#undef READ
#undef WRITE
}
}
