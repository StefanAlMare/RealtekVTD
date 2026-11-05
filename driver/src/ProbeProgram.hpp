// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "HardwareInit.hpp"
#include "FirmwareProgram.hpp"
#include "PHYDiagnostic.hpp"
#include "MACConfigDiagnostic.hpp"
#include "PHYConfigDiagnostic.hpp"
namespace realtek_next {
struct ProbeResult {
 AccessStatus status=AccessStatus::Unsupported;
 const char *stage="identify";
 Identity identity{ProbeStatus::WrongPCI,Chip::Unknown,0,0};
 bool firmwareApplied=false,complete=false;
 bool hardwareTouched=false,gu24MACPatchVerified=false,gu25MACPatchVerified=false,gu25PHYMCUApplied=false;
 PHYState phy;
 AutonegResult autoneg;
 PHYDiagnostic phyInitDiag,phyResetDiag;
 MACConfigDiagnostic macConfig;
 PHYConfigDiagnostic phyConfig;
 uint32_t txConfig=0;bool txConfigValid=false;
 uint32_t eri1b0=0;
};
// Caller holds the device's serialization gate for the entire run.
ProbeResult runProbe(RegisterIO &io,PCIConfig &pci,uint8_t function,
                     uint16_t vendor,uint16_t device,const uint8_t *firmware,size_t firmwareSize);
const char *firmwareName(Chip chip);
}
