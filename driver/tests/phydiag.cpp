// SPDX-License-Identifier: GPL-2.0-only
#include "TraceBackend.hpp"
#include "../src/PHYDiagnostic.hpp"
#include <cassert>
#include <cstdio>
using namespace realtek_next;
class LatchedBackend:public TraceBackend {
 unsigned bmsrReads=0;
public:
 bool write32(uint16_t off,uint32_t v) override {
  if(off==0xb8&&!(v&0x80000000U)&&((v>>15)&0xfffe)==0xa402)
   phy[0xa402]=++bmsrReads==1?0:0x24;
  return TraceBackend::write32(off,v);
 }
};
int main(){
 for(unsigned speed=0;speed<3;++speed){
  LatchedBackend io;RegisterAccess r(io);r.setFirmwareOCPBase(0xbcd0);
  io.phy[0xa400]=0x1000;io.phy[0xa408]=0x1e0;io.phy[0xa40a]=0x1e0;
  io.phy[0xa412]=0x300;io.phy[0xa414]=0xc00;io.phy[0xa5d0]=6;
  io.mmio[0x6c]=3|(4U<<speed);const auto before=io.phy;
  auto d=capturePHYDiagnostic(r);
  assert(r.currentPHYBase()==0xbcd0&&d.valid==0x1ff&&d.failures==0);
  assert(d.bmcr==0x1000&&d.bmsrFirst==0&&d.bmsrSecond==0x24);
  assert(d.anar==0x1e0&&d.anlpar==0x1e0&&d.ctrl1000==0x300&&d.stat1000==0xc00&&d.eee==6);
  assert(d.link()&&d.fullDuplex()&&d.speedMbps()==(speed==0?10:speed==1?100:1000));
  assert(d.autonegEnabled()&&d.autonegComplete());
  for(unsigned i=0;i<6;++i)assert(d.advertise(i)&&d.partner(i));
  for(auto &e:io.events){assert(e.kind!="phy"&&e.kind!="mac"&&e.kind!="pci");
   if(e.kind=="w32")assert(e.address==0xb8&&!(e.value&0x80000000U));}
  for(auto &v:before)if(v.first!=0xa402)assert(io.phy[v.first]==v.second);
 }
 for(unsigned i=0;i<6;++i){TraceBackend io;RegisterAccess r(io);
  io.phy[0xa408]=i<4?0x20U<<i:0;io.phy[0xa40a]=io.phy[0xa408];
  io.phy[0xa412]=i>=4?0x100U<<(i-4):0;io.phy[0xa414]=i>=4?0x400U<<(i-4):0;
  auto d=capturePHYDiagnostic(r);for(unsigned j=0;j<6;++j){assert(d.advertise(j)==(i==j));assert(d.partner(j)==(i==j));}}
 TraceBackend io;RegisterAccess r(io);io.phy[0xa400]=0;io.phy[0xa402]=0;io.mmio[0x6c]=0;
 auto d=capturePHYDiagnostic(r);assert(!d.autonegEnabled()&&!d.autonegComplete()&&!d.link()&&!d.fullDuplex()&&d.speedMbps()==0);
 io.mmio[0x6c]=0x1f;d=capturePHYDiagnostic(r);assert(d.speedMbps()==0); // conflicting speed bits
 io.stuckPort=0xb8;io.mmio[0x6c]=0xff;d=capturePHYDiagnostic(r);assert(d.valid==0&&d.failures==9);
 assert(io.elapsed<=8*10*25); // finite transaction budgets
 puts("PASS: PHYDIAG fake MDIO raw/decode, 10/100/1000 full, autoneg on/off and complete/incomplete, isolated capability bits, latch-low double read, invalid reads, page preservation and no register writes");
}
