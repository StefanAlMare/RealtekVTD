// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "RegisterAccess.hpp"
namespace realtek_next {
// Linux include/uapi/linux/mii.h and r8169_main.c PHYstatus definitions.
// PHY OCP reads issue read commands only; no BMCR/advertisement/page writes.
struct PHYDiagnostic {
 uint16_t bmcr=0,bmsrFirst=0,bmsrSecond=0,anar=0,anlpar=0,ctrl1000=0,stat1000=0,eee=0;
 uint8_t raw=0;uint32_t valid=0;unsigned failures=0;
 bool autonegEnabled() const {return bmcr&0x1000;}
 bool autonegComplete() const {return bmsrSecond&0x20;}
 bool link() const {return raw&2;}
 bool fullDuplex() const {return raw&1;}
 unsigned speedMbps() const {
  if(!link())return 0;
  switch(raw&0x1c){case 4:return 10;case 8:return 100;case 16:return 1000;default:return 0;}
 }
 bool advertise(unsigned n) const {return n<4?bool(anar&(0x20U<<n)):n<6?bool(ctrl1000&(0x100U<<(n-4))):false;}
 bool partner(unsigned n) const {return n<4?bool(anlpar&(0x20U<<n)):n<6?bool(stat1000&(0x400U<<(n-4))):false;}
};
inline PHYDiagnostic capturePHYDiagnostic(RegisterAccess &r) {
 PHYDiagnostic d;
 // Preserve even a non-default firmware/software MDIO page. Standard registers
 // are read by their fixed OCP addresses, independent of the current page.
 uint16_t *values[]={&d.bmcr,&d.bmsrFirst,&d.bmsrSecond,&d.anar,&d.anlpar,&d.ctrl1000,&d.stat1000,&d.eee};
 const unsigned addresses[]={0xa400,0xa402,0xa402,0xa408,0xa40a,0xa412,0xa414,0xa5d0};
 for(unsigned i=0;i<8;++i){auto v=r.phyRead(addresses[i]);
  if(v.status==AccessStatus::Ok){*values[i]=v.value;d.valid|=1U<<i;}else ++d.failures;}
 if(r.mmio().read8(0x6c,d.raw)&&d.raw!=0xff)d.valid|=0x100;else {d.raw=0;++d.failures;}
 return d;
}
}
