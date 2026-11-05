// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "DMAProgram.hpp"
namespace realtek_next {
using TXCopy = bool (*)(void *context,void *destination,size_t length);
// Own all bytes before mapping. Never publish partially copied or mapped data.
DMAStatus prepareTXSnapshot(DMAProvider &,DMABuffer &,size_t length,TXCopy,void *context);
}
