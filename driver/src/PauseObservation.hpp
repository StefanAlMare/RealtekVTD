// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "PHYDiagnostic.hpp"
namespace realtek_next {
// DUT perspective: TX means sending PAUSE; RX means honoring received PAUSE.
// IEEE 802.3 table 28B-3; Linux include/linux/mii.h mii_resolve_flowctrl_fdx.
// No advertising, MAC configuration, DMA or PHY page writes are performed.
struct PauseResolution {
 bool valid=false,tx=false,rx=false;
 bool phyStatusValid=false,phyTx=false,phyRx=false,agreement=false,mediumFlow=false;
 const char *reason="read-invalid";
};
inline PauseResolution resolvePause(const PHYDiagnostic &d) {
 PauseResolution p;
 p.phyStatusValid=(d.valid&0x100)&&d.raw!=0xff&&d.link()&&d.fullDuplex()&&d.speedMbps()!=0;
 p.phyTx=p.phyStatusValid&&bool(d.raw&0x40);
 p.phyRx=p.phyStatusValid&&bool(d.raw&0x20);
 if((d.valid&0x11f)!=0x11f||d.bmcr==0xffff||d.bmsrFirst==0xffff||d.bmsrSecond==0xffff||d.anar==0xffff||d.anlpar==0xffff)return p;
 if(!d.link()){p.reason="link-down";return p;}
 if(!d.speedMbps()){p.reason="speed-invalid";return p;}
 if(!d.fullDuplex()){p.reason="half-duplex";return p;}
 if(!d.autonegEnabled()){p.reason="autoneg-disabled";return p;}
 // Reset, loopback, power-down, isolate or a restart still in progress.
 if(d.bmcr&0xce00){p.reason="phy-transition";return p;}
 if(!(d.bmsrSecond&4)||!d.autonegComplete()){p.reason="autoneg-incomplete";return p;}
 if((d.bmsrSecond&0x10)||(d.anlpar&0x2000)){p.reason="remote-fault";return p;}
 if((d.anar&0x1f)!=1||(d.anlpar&0x1f)!=1||!(d.anlpar&0x4000)){p.reason="partner-invalid";return p;}
 p.valid=true;
 if(d.anar&d.anlpar&0x400){p.tx=true;p.rx=true;}
 else if(d.anar&d.anlpar&0x800){
  if(d.anar&0x400)p.rx=true;
  else if(d.anlpar&0x400)p.tx=true;
 }
 p.agreement=p.phyStatusValid&&p.phyTx==p.tx&&p.phyRx==p.rx;
 // The SDK has one generic FLOW flag, without distinct TX/RX medium flags.
 // Publish it only for symmetric negotiated PAUSE and agreeing PHYstatus.
 p.mediumFlow=p.tx&&p.rx&&p.agreement;
 p.reason=p.tx?(p.rx?"symmetric":"tx-only"):(p.rx?"rx-only":"none");
 return p;
}
inline PHYDiagnostic capturePauseObservation(RegisterAccess &r,uint8_t before) {
 PHYDiagnostic d;d.raw=before;
 uint16_t *values[]={&d.bmcr,&d.bmsrFirst,&d.bmsrSecond,&d.anar,&d.anlpar};
 const unsigned addresses[]={0xa400,0xa402,0xa402,0xa408,0xa40a};
 for(unsigned i=0;i<5;++i){const auto v=r.phyRead(addresses[i]);
  if(v.status==AccessStatus::Ok&&v.value!=0xffff){*values[i]=v.value;d.valid|=1U<<i;}
  else ++d.failures;
 }
 uint8_t after=0;
 if(before!=0xff&&r.mmio().read8(0x6c,after)&&after==before)d.valid|=0x100;
 else ++d.failures;
 return d;
}
inline unsigned pauseMediumIndex(uint8_t phy,bool flow) {
 if(phy==0xff||!(phy&2))return 0;
 unsigned slot=0;
 switch(phy&0x1c){case 4:slot=1;break;case 8:slot=3;break;case 16:slot=5;break;default:return 0;}
 if(!(phy&1))return slot;
 return flow?7+(slot-1)/2:slot+1;
}
}
