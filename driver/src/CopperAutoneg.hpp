// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "RegisterAccess.hpp"
namespace realtek_next {
// Standard Clause 22 copper bits; Linux include/uapi/linux/mii.h.
struct CopperCapabilities {
 uint16_t bmsrFirst=0,bmsr=0,estatus=0;
 bool estatusRead=false,valid=false;
 bool autoneg() const {return bmsr&0x0008;}
 uint16_t baseAdvertisement() const {return uint16_t((bmsr&0x7800)>>6);}
 uint16_t gigabitAdvertisement(bool allowHalf=true) const {
  return uint16_t((estatusRead&&allowHalf&&(estatus&0x1000)?0x0100:0)|
                  (estatusRead&&(estatus&0x2000)?0x0200:0));
 }
};
struct CopperAdvertisement {
 uint16_t anar=0,ctrl1000=0;
 static CopperAdvertisement construct(const CopperCapabilities &c,uint16_t oldANAR,uint16_t oldCTRL1000,bool allowHalf=true) {
  return {uint16_t((oldANAR&~0x01e0U)|c.baseAdvertisement()),
          uint16_t((oldCTRL1000&~0x0300U)|c.gigabitAdvertisement(allowHalf))};
 }
};
struct AutonegResult {
 AccessStatus status=AccessStatus::Unsupported;
 const char *stage="not-run";
 bool configured=false,ctrl1000Accessed=false;
 CopperCapabilities capabilities;
 uint16_t anarBefore=0,anarAfter=0,ctrl1000Before=0,ctrl1000After=0,bmcrBefore=0,bmcrRestart=0;
 // Bits: BMSR first, BMSR second, ESTATUS, ANAR before/after,
 // CTRL1000 before/after, BMCR before, submitted BMCR restart.
 uint32_t valid=0;
};
AccessStatus readCopperCapabilities(RegisterAccess &,AutonegResult &);
AccessStatus configureCopperAutoneg(RegisterAccess &,AutonegResult &,bool allow1000Half=true);
}
