// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "RegisterAccess.hpp"
namespace realtek_next {
// Dedicated CFG_METHOD_24 path, derived from pinned Mieze a3dad34/r8168 8.047.04.
// No external PHY firmware and no RTL8411B or RTL8168H initialization is used.
AccessStatus quiesceGU24(RegisterAccess &);
AccessStatus exitOOBGU24(RegisterAccess &);
AccessStatus applyMACPatchGU24(RegisterAccess &);
AccessStatus configureMACGU24(RegisterAccess &, PCIConfig &);
AccessStatus configurePHYGU24(RegisterAccess &);
}
