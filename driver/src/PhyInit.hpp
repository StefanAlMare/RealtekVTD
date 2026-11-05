// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "ChipIdentity.hpp"
#include "RegisterAccess.hpp"
#include "CopperAutoneg.hpp"
namespace realtek_next {
struct PHYState {
 uint16_t id1=0,id2=0,status=0,control=0,eeeAdvertisement=0;
 uint16_t bmsrFirst=0,bmsrSecond=0;
 bool link=false;
};
class PhyInit {
 RegisterAccess &r;
public:
 explicit PhyInit(RegisterAccess &access):r(access){}
 AccessStatus configure(Chip chip); // Firmware must have run first if supplied.
 AccessStatus softReset();
 AccessStatus readCopperCapabilities(AutonegResult &out){return realtek_next::readCopperCapabilities(r,out);}
 AccessStatus configureCopperAutoneg(AutonegResult &out){return realtek_next::configureCopperAutoneg(r,out);}
 AccessStatus readState(PHYState &state);
 AccessStatus enforceEEEOff();
 AccessStatus wake();
};
}
