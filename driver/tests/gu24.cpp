// SPDX-License-Identifier: GPL-2.0-only
#include "TraceBackend.hpp"
#include "../src/ProbeProgram.hpp"
#include "../src/GU24Program.hpp"
#include "../src/MulticastFilter.hpp"
#include "../src/JumboProfile.hpp"
#include <cassert>
#include <cstdio>
using namespace realtek_next;
static TraceBackend target(){
 TraceBackend b(0x508);b.pci[0x2c]=0x1043;b.pci[0x2e]=0x200f;b.pci[8]=0x10;
 b.phy[0xa402]=0x790c;b.phy[0xa41e]=0x3000;b.phy[0xa408]=0x0001;return b;
}
static ProbeResult run(TraceBackend &b){return runProbe(b,b,0,0x10ec,0x8168,nullptr,0);}
static bool writes(const TraceBackend &b){
 for(const auto &e:b.events)if(e.kind[0]=='w'||e.kind=="mac"||e.kind=="phy"||e.kind=="pci")return true;
 return false;
}
static void checkStopped(const TraceBackend &b){
 auto cmd=b.mmio.find(0x37),mask=b.mmio.find(0x3c);
 if(cmd!=b.mmio.end())assert(!(cmd->second&12));
 if(mask!=b.mmio.end())assert(mask->second==0);
 for(const auto &e:b.events){
  assert(!(e.kind=="w8"&&e.address==0x37&&(e.value&12)));
  assert(!(e.kind=="w32"&&(e.address==0x20||e.address==0x24||e.address==0xe4||e.address==0xe8)));
 }
}
static void identity(){
 constexpr auto id=identify(0x10ec,0x8168,0x50800000,true);
 static_assert(id.status==ProbeStatus::Supported&&id.chip==Chip::RTL8168GU_Method24&&id.linuxMacVersion==0,"GU24 identity");
 static_assert(unsigned(Chip::RTL8168EVL)==4,"preserve existing enum identity");
 for(unsigned ic=2;ic<8;++ic)assert(identify(0x10ec,0x8168,0x50800000U|(ic<<20),true).status!=ProbeStatus::Supported);
 assert(identify(0x10ec,0x8168,0xffffffff,true).status==ProbeStatus::ReadFailed);
 auto b=target();b.phy[0xa408]|=0x0c00;auto good=run(b);
 assert(good.complete&&!good.firmwareApplied&&good.gu24MACPatchVerified&&good.status==AccessStatus::Ok);
 assert((b.phy.at(0xa408)&0x0c00)==0&&b.phy.at(0xa412)==0x200);
 assert(!jumboIdentityValid(good.identity,good.autoneg.capabilities));
 checkStopped(b);
 assert(b.mac.at(0xf802)==0xe011&&b.mac.at(0xf8dc)==0&&b.mac.at(0xfc28)==0x493);
 assert(b.mac.at(0xfc34)==0x285&&b.mac.at(0xfc26)==0x8000);
 assert(!b.mac.count(0xd412)&&!b.mac.count(0xe63e)); // No H trim/aggregation.
 assert(b.ephy.empty()); // Mieze has no CFG24 EPHY branch; GU25 is different.
 assert(b.phy.at(0xa5d0)==0&&(b.eri.at(0x1b0)&3)==0);
 assert(b.phy.at(0xc422)==0x4000&&b.phy.at(0xbcd8)==0x1065);
 RegisterAccess r(b);HardwareInit legacy(r,b);const auto before=b.calls;
 assert(legacy.configureMAC(Chip::RTL8168GU_Method24)==AccessStatus::Unsupported&&b.calls==before);
 assert(legacy.updateLinkEEE(Chip::RTL8168GU_Method24,true)==AccessStatus::Unsupported&&b.calls==before);
 MulticastFilter f;assert(!f.apply(b,false,0));assert(f.apply(b,false,0,true));
 const uint8_t group[]={1,0,0x5e,0,0,1};assert(f.list(group,1));f.enabled=true;
 assert(f.apply(b,false,0,true)&&f.valid&&!f.fallback);
 printf("PASS: GU24 identity, board-independent acceptance, dedicated MAC/PHY, no external firmware, EEE/jumbo off\n");
}
static void failures(){
 auto b=target();const auto success=run(b);assert(success.complete);const auto total=b.calls;
 for(size_t i=1;i<=total;++i){auto f=target();f.failAt=i;const auto result=run(f);checkStopped(f);
  if(result.complete)assert(result.phyInitDiag.failures||result.phyResetDiag.failures);
 }
 for(unsigned reg:{8U,0x2cU,0x2eU}){auto f=target();f.pci[reg]^=1;assert(run(f).complete);}
 auto f=target();uint8_t firmware[4]={};auto result=runProbe(f,f,0,0x10ec,0x8168,firmware,4);
 assert(!result.complete&&result.status==AccessStatus::InvalidFirmware&&!writes(f));
 for(unsigned xid:{0x50aU,0x50fU,0x123U}){auto t=target();t.mmio[0x40]=xid<<20;assert(!run(t).complete&&!writes(t));}
 auto stuck=target();stuck.resetStuck=true;assert(!run(stuck).complete);checkStopped(stuck);
 auto absent=target();absent.phy[0xa41e]=0;assert(!run(absent).complete);checkStopped(absent);
 auto invalid=target();invalid.phy[0xa404]=0xffff;assert(!run(invalid).complete);checkStopped(invalid);
 printf("PASS: %zu transport-failure injections, unsupported identities/targets/firmware/PHY/reset fail closed\n",total);
}
struct CorruptPatch:TraceBackend{
 CorruptPatch():TraceBackend(0x508){}
 bool write32(uint16_t o,uint32_t v) override{
  const bool ok=TraceBackend::write32(o,v);
  if(o==0xb0&&(v&0x80000000U)&&((v>>15)&0xfffe)==0xf802)mac[0xf802]^=1;
  return ok;
 }
};
int main(){
 identity();failures();CorruptPatch b;RegisterAccess r(b);
 assert(applyMACPatchGU24(r)==AccessStatus::VerificationFailed);
 assert(b.mac[0xfc26]==0&&b.mac[0xfc28]==0&&b.mmio[0x50]==0);
 puts("PASS: corrupted embedded MAC patch cannot activate; configuration relocked");
}
