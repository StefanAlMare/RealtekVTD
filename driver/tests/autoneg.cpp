// SPDX-License-Identifier: GPL-2.0-only
#include "TraceBackend.hpp"
#include "../src/CopperAutoneg.hpp"
#include "../src/ProbeProgram.hpp"
#include <cassert>
#include <cstdio>
using namespace realtek_next;
static void populate(TraceBackend &io,uint16_t bmsr=0x79ed,uint16_t ext=0x2000){
 io.phy[0xa402]=bmsr;io.phy[0xa41e]=ext;io.phy[0xa408]=0x0c61;io.phy[0xa412]=0;io.phy[0xa400]=0x1040;
}
static AutonegResult run(TraceBackend &io){RegisterAccess r(io);r.setFirmwareOCPBase(0xbcd0);AutonegResult out;
 if(readCopperCapabilities(r,out)==AccessStatus::Ok)configureCopperAutoneg(r,out);
 assert(r.currentPHYBase()==0xa400);return out;}
class Latched:public TraceBackend {
 unsigned count=0;
public:
 bool write32(uint16_t off,uint32_t value)override{
  if(off==0xb8&&!(value&0x80000000U)&&((value>>15)&0xfffe)==0xa402)
   phy[0xa402]=++count==1?0x79e9:0x79ed;
  return TraceBackend::write32(off,value);
 }
};
class FailedWrite:public TraceBackend {
public:
 unsigned reject=0;
 bool write32(uint16_t off,uint32_t v)override{
  if(off==0xb8&&(v&0x80000000U)&&((v>>15)&0xfffe)==reject)return false;
  return TraceBackend::write32(off,v);
 }
};
class BadReadback:public TraceBackend {
public:
 unsigned corrupt=0;
 bool write32(uint16_t off,uint32_t v)override{
  bool ok=TraceBackend::write32(off,v);
  if(ok&&off==0xb8&&(v&0x80000000U)&&((v>>15)&0xfffe)==corrupt)phy[corrupt]=0;
  return ok;
 }
};
int main(){
 Latched asus;populate(asus);auto out=run(asus);
 assert(out.status==AccessStatus::Ok&&out.configured&&out.valid==511);
 assert(out.capabilities.bmsrFirst==0x79e9&&out.capabilities.bmsr==0x79ed&&out.capabilities.estatus==0x2000);
 assert(out.anarBefore==0x0c61&&out.anarAfter==0x0de1&&out.ctrl1000After==0x0200);
 assert(out.bmcrBefore==0x1040&&out.bmcrRestart==0x1240);
 unsigned writes=0;const unsigned addresses[]={0xa408,0xa412,0xa400};
 for(auto &e:asus.events)if(e.kind=="phy")assert(e.address==addresses[writes++]);assert(writes==3);
 for(unsigned n=0;n<4;++n){TraceBackend io;populate(io,uint16_t(8|(0x0800U<<n)),0);out=run(io);
  assert(out.configured&&out.anarAfter==(0x0c01|(0x20U<<n))&&!out.ctrl1000Accessed);
  for(auto &e:io.events)assert(!(e.kind=="w32"&&e.address==0xb8&&(((e.value>>15)&0xfffe)==0xa412||((e.value>>15)&0xfffe)==0xa41e)));}
 // Exhaustive masks retain every unrelated ANAR and CTRL1000 bit.
 for(unsigned caps=0;caps<64;++caps){CopperCapabilities c;c.bmsr=uint16_t((caps&15)<<11);c.estatus=uint16_t(((caps>>4)&3)<<12);c.estatusRead=true;
  auto adv=CopperAdvertisement::construct(c,0xffff,0xffff);
  assert((adv.anar&~0x1e0U)==(0xffffU&~0x1e0U));assert((adv.ctrl1000&~0x300U)==(0xffffU&~0x300U));
  assert((adv.anar&0x1e0)==uint16_t((caps&15)<<5));assert((adv.ctrl1000&0x300)==uint16_t(((caps>>4)&3)<<8));
  assert(!(CopperAdvertisement::construct(c,0,0,false).ctrl1000&0x100));}
 for(unsigned bit:{0x1000U,0x2000U,0x3000U,0xc000U}){TraceBackend io;populate(io,0x790c,uint16_t(bit));io.phy[0xa412]=0xbdff;out=run(io);
  assert(out.configured&&out.ctrl1000After==uint16_t((0xbdff&~0x300)|((bit&0x3000)>>4)));}
 TraceBackend fast;populate(fast,0x780c);fast.phy[0xa412]=0;out=run(fast);
 assert(out.configured&&!out.ctrl1000Accessed&&out.ctrl1000After==0&&!out.capabilities.estatusRead);
 TraceBackend policy;populate(policy,0x790c,0x3000);policy.phy[0xa400]=0x6dff;
 RegisterAccess policyAccess(policy);AutonegResult policyResult;
 assert(readCopperCapabilities(policyAccess,policyResult)==AccessStatus::Ok);
 assert(configureCopperAutoneg(policyAccess,policyResult,false)==AccessStatus::Ok);
 assert(policyResult.ctrl1000After==0x200&&policyResult.bmcrRestart==uint16_t(0x6dff|0x1200));
 TraceBackend noAN;populate(noAN,0x7800);out=run(noAN);assert(out.status==AccessStatus::Unsupported);
 for(auto &e:noAN.events)assert(e.kind!="phy");
 TraceBackend none;populate(none,0);out=run(none);assert(out.status==AccessStatus::Unsupported&&!out.configured);
 for(auto &e:none.events)assert(e.kind!="phy");
 TraceBackend reset;populate(reset);reset.phy[0xa400]=0x8000;out=run(reset);assert(!out.configured&&out.status==AccessStatus::VerificationFailed);
 for(auto &e:reset.events)assert(!(e.kind=="phy"&&e.address==0xa400));
 TraceBackend good;populate(good);out=run(good);assert(out.configured);
 for(size_t i=1;i<=good.calls;++i){TraceBackend io;populate(io);io.failAt=i;out=run(io);assert(!out.configured&&out.status==AccessStatus::IOFailure);}
 for(unsigned reg:{0xa408U,0xa412U,0xa400U}){FailedWrite io;populate(io);io.reject=reg;out=run(io);assert(!out.configured&&out.status==AccessStatus::IOFailure);}
 for(unsigned reg:{0xa408U,0xa412U}){BadReadback io;populate(io);io.corrupt=reg;out=run(io);assert(!out.configured&&out.status==AccessStatus::VerificationFailed);
  for(auto &e:io.events)assert(!(e.kind=="phy"&&e.address==0xa400));}
 TraceBackend timeout;populate(timeout);timeout.stuckPort=0xb8;out=run(timeout);assert(out.status==AccessStatus::Timeout&&timeout.elapsed==250);
 // Integration: all profiles use the same copper layer after the one reset,
 // before MAC configuration. EEE policy stays profile-specific.
 for(unsigned xid:{0x5c8U,0x541U,0x6c0U}){TraceBackend io(xid);populate(io);auto result=runProbe(io,io,0,0x10ec,0x8168,nullptr,0);
  assert(result.status==AccessStatus::Ok&&result.autoneg.configured&&io.mmio[0x37]==0&&io.mmio[0x3c]==0);
  bool sawReset=false,sawAdv=false,sawRestart=false;unsigned resetCount=0;
  for(auto &e:io.events)if(e.kind=="w32"&&e.address==0xb8&&(e.value&0x80000000U)){
   const unsigned addr=(e.value>>15)&0xfffe;
   if(addr==0xa400&&(e.value&0x8000)){assert(!sawAdv);sawReset=true;++resetCount;}
   if(addr==0xa408){assert(sawReset);sawAdv=true;}
   if(addr==0xa400&&(e.value&0x0200)){assert(sawAdv);sawRestart=true;assert(!(e.value&0x8000));}}
  assert(resetCount==1&&sawRestart);assert(io.phy[0xa5d0]==(xid==0x5c8?6U:0U));
 }
 // Probe failure still leaves the traffic engine stopped, never complete.
 for(unsigned reg:{0xa408U,0xa412U}){FailedWrite io;populate(io);io.reject=reg;auto result=runProbe(io,io,0,0x10ec,0x8168,nullptr,0);
  assert(result.status==AccessStatus::IOFailure&&!result.complete&&io.mmio[0x37]==0&&io.mmio[0x3c]==0);}
 puts("PASS: copper capabilities, base-T ESTATUS, computed advertising/preserved policy, Fast Ethernet, unsupported modes, reset/restart order, all transport failures/readback mismatch, three chip profiles and EEE regression");
}
