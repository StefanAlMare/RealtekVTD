// SPDX-License-Identifier: GPL-2.0-only
#include "TraceBackend.hpp"
#include "../src/PauseObservation.hpp"
#include <cassert>
#include <cstdio>
using namespace realtek_next;
static PHYDiagnostic sample(unsigned local=3,unsigned peer=3,uint8_t raw=0x73){
 PHYDiagnostic d;d.valid=0x11f;d.raw=raw;d.bmcr=0x1000;d.bmsrFirst=0x20;d.bmsrSecond=0x24;
 d.anar=uint16_t(1|(local<<10));d.anlpar=uint16_t(0x4001|(peer<<10));return d;
}
static void prepare(TraceBackend &io){
 io.phy[0xa400]=0x1000;io.phy[0xa402]=0x24;io.phy[0xa408]=0xde1;io.phy[0xa40a]=0xcde1;io.mmio[0x6c]=0x73;
}
static void readOnly(const TraceBackend &io){
 for(const auto &e:io.events){
  assert(e.kind!="phy"&&e.kind!="mac"&&e.kind!="pci"&&e.kind!="w8"&&e.kind!="w16");
  if(e.kind=="w32")assert(e.address==0xb8&&!(e.value&0x80000000U));
 }
}
class ChangingLink:public TraceBackend {
public:bool read8(uint16_t off,uint8_t &v) override {if(!TraceBackend::read8(off,v))return false;if(off==0x6c)v^=2;return true;}
};
int main(){
 // Independent 4x4 IEEE table: row/column = none, P, A, P+A;
 // result bits = TX 1, RX 2, symmetric 3, disabled 0.
 const unsigned expected[4][4]={{0,0,0,0},{0,3,0,3},{0,0,0,1},{0,3,2,3}};
 for(unsigned l=0;l<4;++l)for(unsigned r=0;r<4;++r)for(unsigned status=0;status<4;++status){
  auto d=sample(l,r,uint8_t(0x13|((status&1)?0x40:0)|((status&2)?0x20:0)));
  auto p=resolvePause(d);assert(p.valid);
  assert((unsigned(p.tx)|(unsigned(p.rx)<<1))==expected[l][r]);
  assert(p.agreement==(status==expected[l][r]));assert(p.mediumFlow==(expected[l][r]==3&&status==3));
 }
 for(unsigned bit: {0U,1U,2U,3U,4U,8U}){auto d=sample();d.valid&=~(1U<<bit);auto p=resolvePause(d);assert(!p.valid&&!p.tx&&!p.rx&&!p.mediumFlow);}
 for(unsigned reg=0;reg<5;++reg){auto d=sample();uint16_t *v[]={&d.bmcr,&d.bmsrFirst,&d.bmsrSecond,&d.anar,&d.anlpar};*v[reg]=0xffff;assert(!resolvePause(d).valid);}
 for(unsigned mask: {0x8000U,0x4000U,0x800U,0x400U,0x200U}){auto d=sample();d.bmcr|=mask;assert(!resolvePause(d).valid);}
 for(unsigned test=0;test<9;++test){auto d=sample();
  switch(test){case 0:d.raw&=~2U;break;case 1:d.raw&=~1U;break;case 2:d.bmcr=0;break;
   case 3:d.bmsrSecond&=~0x20U;break;case 4:d.bmsrSecond&=~4U;break;
   case 5:d.anlpar&=~0x4000U;break;case 6:d.anlpar|=0x2000;break;case 7:d.anar&=~0x1fU;break;case 8:d.anlpar&=~0x1fU;break;}
  assert(!resolvePause(d).valid&&!resolvePause(d).mediumFlow);
 }
 for(unsigned phy=0;phy<256;++phy)for(bool flow: {false,true}){
  unsigned expectedIndex=0;unsigned speed=phy&0x1c;
  if(phy!=255&&(phy&2)&&(speed==4||speed==8||speed==16)){
   unsigned ordinal=speed==4?0:speed==8?1:2;
   expectedIndex=(phy&1)?(flow?7+ordinal:2+2*ordinal):1+2*ordinal;
  }
  assert(pauseMediumIndex(uint8_t(phy),flow)==expectedIndex);
 }
 // Completion at unchanged speed must be observed on the next existing tick.
 auto d=sample();d.bmsrSecond=4;assert(!resolvePause(d).mediumFlow);d.bmsrSecond=0x24;assert(resolvePause(d).mediumFlow);
 d.valid=0;assert(!resolvePause(d).mediumFlow);d=sample();assert(resolvePause(d).mediumFlow);
 d.anlpar=0x4001;assert(resolvePause(d).valid&&!resolvePause(d).mediumFlow);
 TraceBackend io;prepare(io);RegisterAccess access(io);access.setFirmwareOCPBase(0xbcd0);
 const auto before=io.phy;auto observed=capturePauseObservation(access,0x73);
 assert(observed.valid==0x11f&&!observed.failures&&resolvePause(observed).mediumFlow);
 assert(io.phy==before&&access.currentPHYBase()==0xbcd0);readOnly(io);
 const unsigned calls=unsigned(io.calls);
 for(unsigned failure=1;failure<=calls;++failure){TraceBackend bad;prepare(bad);bad.failAt=failure;RegisterAccess a(bad);
  auto x=capturePauseObservation(a,0x73);assert(!resolvePause(x).valid&&!resolvePause(x).mediumFlow);readOnly(bad);
 }
 for(unsigned reg: {0xa400U,0xa402U,0xa408U,0xa40aU}){TraceBackend bad;prepare(bad);bad.phy[reg]=0xffff;RegisterAccess a(bad);assert(!resolvePause(capturePauseObservation(a,0x73)).valid);}
 ChangingLink changing;prepare(changing);RegisterAccess changingAccess(changing);assert(!(capturePauseObservation(changingAccess,0x73).valid&0x100));
 TraceBackend stuck;prepare(stuck);stuck.stuckPort=0xb8;RegisterAccess stuckAccess(stuck);
 auto invalid=capturePauseObservation(stuckAccess,0x73);assert(invalid.failures==5&&!resolvePause(invalid).valid&&stuck.elapsed<=5*10*25);readOnly(stuck);
 puts("PASS: all 16 PAUSE outcomes, 64 PHYstatus comparisons, all media indices, invalid/forced/half-duplex/restart/link transitions, complete failure injection, finite reads, preserved page and zero configuration writes");
}
