// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "PhyInit.hpp"
namespace realtek_next {
class HardwareInit {
 RegisterAccess &r; PCIConfig &pci; uint8_t function;
 AccessStatus fifoAndFilter();
 AccessStatus entryLatency();
 AccessStatus wait8(uint16_t off,uint8_t mask,uint8_t expected,unsigned attempts,unsigned interval);
 AccessStatus patch8411B();
 AccessStatus configure8168EVL();
 AccessStatus disableClockRequest();
 AccessStatus configureLegacyReceiveAddress();
public:
 HardwareInit(RegisterAccess &access,PCIConfig &config,uint8_t pciFunction=0):r(access),pci(config),function(pciFunction){}
 AccessStatus preInitialize(Chip chip=Chip::RTL8411B);
 AccessStatus reset();
 AccessStatus configureMAC(Chip chip); // Descriptor-independent configuration only.
 AccessStatus updateLinkEEE(Chip chip,bool eeeActive);
 AccessStatus modify8(uint16_t off,uint8_t clear,uint8_t set);
 AccessStatus modify32(uint16_t off,uint32_t clear,uint32_t set);
 AccessStatus verifyStopped();
};
}
