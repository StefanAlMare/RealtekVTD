// SPDX-License-Identifier: GPL-2.0-only
#include "../src/DMAMode.hpp"
#include <cassert>
#include <cstdio>
using namespace realtek_next;
int main(){
 for(unsigned bits=0;bits<16;++bits){bool device=bits&1,system=bits&2,parent=bits&4,vtd=bits&8;
  const auto mode=selectDMAMode(device,system,parent,vtd);
  assert(mode==(device?DMAMode::DeviceMapper:(system||parent||vtd)?DMAMode::Unavailable:DMAMode::NoIOMMU));
 }
 puts("PASS: device mapper wins; mapper pending/required, orphan parent and AppleVTD without device mapper fail closed; no-IOMMU mode explicit");}
