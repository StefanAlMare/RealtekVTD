// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "RegisterAccess.hpp"
#include "MACConfigDiagnostic.hpp"
#include "PHYConfigDiagnostic.hpp"
namespace realtek_next {
// CFG_METHOD_25, XID 0x509 / ICVerID 1. Never uses the GU24 RAM payload.
AccessStatus applyMACPatchGU25(RegisterAccess &);
AccessStatus disableOCPPowerSavingGU25(RegisterAccess &,PHYConfigDiagnostic *diagnostic=nullptr);
AccessStatus configureEPHYGU25(RegisterAccess &);
AccessStatus applyPHYMCUGU25(RegisterAccess &,PHYConfigDiagnostic *diagnostic=nullptr,const Identity *identity=nullptr);
AccessStatus configurePHYGU25(RegisterAccess &,PHYConfigDiagnostic *diagnostic=nullptr,const Identity *identity=nullptr,bool criticalPHYMCUConfirmed=false);
AccessStatus configureMACGU25(RegisterAccess &,PCIConfig &,MACConfigDiagnostic *diagnostic=nullptr);
}
