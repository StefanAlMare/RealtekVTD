// SPDX-License-Identifier: GPL-2.0-only
#include "TraceBackend.hpp"
#include "../src/ProbeProgram.hpp"
#include <cassert>
#include <cstdio>
using namespace realtek_next;
static uint32_t op(unsigned code,unsigned reg=0,unsigned data=0){return(code<<28)|(reg<<16)|data;}
static std::vector<uint8_t> image(const std::vector<uint32_t>& words){std::vector<uint8_t> b;
 for(auto w:words)for(unsigned i=0;i<4;++i)b.push_back(uint8_t(w>>(8*i)));return b;}
struct Target : FirmwareTarget {
 std::map<unsigned,uint16_t> registers;unsigned reads=0,restores=0;bool fail=false,ramp=false;
 Read16 read(unsigned reg) override {++reads;return{fail?AccessStatus::Timeout:AccessStatus::Ok,ramp?uint16_t(reads):registers[reg]};}
 AccessStatus write(unsigned reg,uint16_t v) override {if(fail)return AccessStatus::IOFailure;registers[reg]=v;return AccessStatus::Ok;}
 void restore() override {++restores;}
};
static AccessStatus execute(const std::vector<uint32_t>& words,Target &phy,Target &mac,TraceBackend &clock,size_t steps=10000){
 auto bytes=image(words);FirmwareProgram p;auto s=p.parse(bytes.data(),bytes.size());
 return s==AccessStatus::Ok?p.execute(phy,mac,clock,steps):s;
}
static void registers(){
 TraceBackend b;RegisterAccess r(b);
 assert(r.ephyWrite(5,0x2089)==AccessStatus::Ok && b.ephy[5]==0x2089 && b.elapsed==10);
 assert(r.ephyRead(5).value==0x2089);
 assert(b.events.front().address==0x80&&b.events.front().value==0x80052089U);

 assert(r.eriWrite(0x1b0,1,0x45)==AccessStatus::Ok&&b.eri[0x1b0]==0x1045);
 assert(r.eriWrite(0x1b0,2,0xaa00)==AccessStatus::Ok&&b.eri[0x1b0]==0xaa45);
 assert(r.eriClearBits(0x1b0,3)==AccessStatus::Ok&&r.eriRead(0x1b0).value==0xaa44);
 assert(r.eriSetBits(0x1b0,2)==AccessStatus::Ok&&r.eriRead(0x1b0).value==0xaa46);
 const auto n=b.calls;
 assert(r.eriRead(0x1b1).status==AccessStatus::InvalidAddress);
 assert(r.eriWrite(0x1b0,0,0)==AccessStatus::InvalidAddress);
 assert(r.eriWrite(0x1b0,16,0)==AccessStatus::InvalidAddress&&b.calls==n);
 b.stuckPort=0x80;b.stuckWrites=true;
 assert(r.ephyWrite(5,0)==AccessStatus::Timeout);
 b.stuckWrites=false;assert(r.ephyRead(5).status==AccessStatus::Timeout);
 b.stuckPort=0x74;b.stuckWrites=true;assert(r.eriWrite(0,15,0)==AccessStatus::Timeout);
 b.stuckWrites=false;assert(r.eriRead(0).status==AccessStatus::Timeout);
 b.stuckPort=0;b.failAt=b.calls+1;
 assert(r.pagedRead(0xbcd,0x16).status==AccessStatus::IOFailure&&r.currentPHYBase()==0xa400);
 MACFirmwareTarget mac(r);assert(mac.write(31,0xf80)==AccessStatus::Ok&&mac.currentBase()==0xf800);
 assert(mac.write(2,0xbeef)==AccessStatus::Ok&&b.mac[0xf802]==0xbeef);
 assert(mac.read(2).value==0xbeef);mac.restore();assert(mac.currentBase()==0xa400);
 assert(r.mdioWrite(31,0xbcd)==AccessStatus::Ok);
 assert(mac.write(2,0x77)==AccessStatus::Ok&&b.mac[0xbcd2]==0x77);
 assert(mac.write(31,0xa43)==AccessStatus::Ok);
 assert(r.mdioWrite(16,0x66)==AccessStatus::Ok&&b.phy[0xa430]==0x66);
 mac.restore();
}
static void firmware(){
 TraceBackend b;Target p,m;p.registers[1]=0x12;
 assert(execute({op(0,1),op(1,0,0x80),op(2,0,0xf0),op(12,2),op(8,3,0x77),op(14,0,2)},p,m,b)==AccessStatus::Ok);
 assert(p.registers[2]==0x90&&p.registers[3]==0x77&&b.elapsed==2000);
 assert(execute({op(4,0,1),op(8,4,0x55),op(4),op(8,5,0x66)},p,m,b)==AccessStatus::Ok&&m.registers[4]==0x55&&p.registers[5]==0x66);
 assert(execute({op(0,1),op(9,0,1),op(8,6,1),op(7),op(9,0,0),op(8,7,1),op(8,8,2)},p,m,b)==AccessStatus::Ok);
 assert(!p.registers.count(6)&&!p.registers.count(7)&&p.registers[8]==2);
 assert(execute({op(0,1),op(10,1,0x12),op(8,9,1),op(11,1,0x13),op(8,10,1),op(13,1),op(8,11,1),op(8,12,2)},p,m,b)==AccessStatus::Ok);
 assert(!p.registers.count(9)&&!p.registers.count(10)&&!p.registers.count(11)&&p.registers[12]==2);
 p.ramp=true;p.reads=0;
 assert(execute({op(0,1),op(10,1,3),op(3,2),op(8,13,3)},p,m,b)==AccessStatus::Ok&&p.reads==3);
 p.ramp=false;
 assert(execute({op(3,0)},p,m,b,20)==AccessStatus::BudgetExceeded);
 assert(execute({op(3,1)},p,m,b)==AccessStatus::InvalidFirmware);
 assert(execute({op(4,0,2)},p,m,b)==AccessStatus::InvalidFirmware);
 assert(execute({op(13,2),op(8)},p,m,b)==AccessStatus::InvalidFirmware);
 assert(execute({op(6)},p,m,b)==AccessStatus::InvalidFirmware);
 p.fail=true;assert(execute({op(0,1)},p,m,b)==AccessStatus::Timeout);
 assert(execute({op(8,1)},p,m,b)==AccessStatus::IOFailure);assert(p.restores&&m.restores);
 FirmwareProgram f;auto raw=image({op(8,1,0x55)});raw.pop_back();assert(f.parse(raw.data(),raw.size())==AccessStatus::InvalidFirmware);
 std::vector<uint8_t> header(48,0);header[36]=48;header[40]=1;
 auto code=image({op(8,1,0x1234)});header.insert(header.end(),code.begin(),code.end());
 unsigned sum=0;for(auto byte:header)sum+=byte;header[44]=uint8_t(0-sum);
 assert(f.parse(header.data(),header.size())==AccessStatus::Ok&&f.instructionCount()==1);
 header.back()^=1;assert(f.parse(header.data(),header.size())==AccessStatus::InvalidFirmware);
 header.back()^=1;header[40]=2;header[44]=0;sum=0;for(auto byte:header)sum+=byte;header[44]=uint8_t(0-sum);
 assert(f.parse(header.data(),header.size())==AccessStatus::InvalidFirmware);
 uint8_t truncated[4]={0,0,0,0};assert(f.parse(truncated,4)==AccessStatus::InvalidFirmware);
 assert(execute({op(9,0,1),op(8)},p,m,b)==AccessStatus::InvalidFirmware);
 auto excessive=image({op(14,0,65535)});assert(f.parse(excessive.data(),excessive.size())==AccessStatus::Ok);
 assert(f.execute(p,m,b,10,1000)==AccessStatus::BudgetExceeded);
 raw=image({op(8,31,0xbcd),op(0,16)});TraceBackend t;RegisterAccess a(t);PHYFirmwareTarget phy(a);MACFirmwareTarget mac(a);
 t.failAt=1;assert(f.parse(raw.data(),raw.size())==AccessStatus::Ok);
 assert(f.execute(phy,mac,t)==AccessStatus::IOFailure&&a.currentPHYBase()==0xa400);
 t.failAt=0;t.stuckPort=0xb8;t.stuckWrites=false;
 assert(f.execute(phy,mac,t)==AccessStatus::Timeout&&a.currentPHYBase()==0xa400);
}
static size_t eventIndex(const TraceBackend &b,const char *kind,unsigned addr,uint32_t value){
 for(size_t i=0;i<b.events.size();++i)if(b.events[i].kind==kind&&b.events[i].address==addr&&b.events[i].value==value)return i;
 return b.events.size();
}
static void programs(){
 for(unsigned xid:{0x5c8U,0x541U,0x6c0U}){
  TraceBackend b(xid);b.phy[0xa402]=0x790c;b.phy[0xa41e]=0x3000;auto fw=image({op(8,31,0)});
  auto result=runProbe(b,b,0,0x10ec,0x8168,fw.data(),fw.size());
  if(result.status!=AccessStatus::Ok)std::fprintf(stderr,"stage %s status %u xid %x\n",result.stage,unsigned(result.status),xid);
  assert(result.status==AccessStatus::Ok&&result.complete&&result.firmwareApplied&&result.phy.link);
  assert(b.mmio[0x37]==0&&b.mmio[0x3c]==0);
  for(auto &e:b.events){assert(!(e.kind=="w8"&&e.address==0x37&&(e.value&12)));
   assert(!(e.kind=="w32"&&(e.address==0x20||e.address==0x24||e.address==0xe4||e.address==0xe8)));}
  const auto stop=eventIndex(b,"w8",0x37,0),reset=eventIndex(b,"w8",0x37,0x10);
  assert(stop<reset);
  if(xid==0x5c8){assert(b.mac[0xf800]==0xe008&&b.mac[0xf8dc]==0x132&&b.mac[0xfc26]==0x8000);
   assert(b.phy[0xa5d0]==6);assert(eventIndex(b,"eri",0xc8,0x80002)<eventIndex(b,"ephy-write",0,0));}
  else{assert(result.phy.eeeAdvertisement==0&&(result.eri1b0&3)==0);
   assert(eventIndex(b,"ephy-write",30,1)<eventIndex(b,"eri",0xc8,0x80002));
   assert(eventIndex(b,"mac",0xe63e,1)<eventIndex(b,"mac",0xe63e,0));}
  // Inject a transport failure at each access. Native PCI latency can recover
  // via CSI; that fallback is deliberately allowed and tested separately.
  const size_t total=b.calls;
  for(size_t i=1;i<=total;++i){TraceBackend fail(xid);fail.phy[0xa402]=0x790c;fail.phy[0xa41e]=0x3000;fail.failAt=i;
   auto broken=runProbe(fail,fail,0,0x10ec,0x8168,fw.data(),fw.size());
   if(broken.status==AccessStatus::Ok){
    // Diagnostic transport failures are reported in the sample and must not
    // fail networking initialization; CSI recovery retains the old check.
    assert(broken.phyInitDiag.failures||broken.phyResetDiag.failures||fail.events.size()>b.events.size());
   }
   assert(!(fail.mmio[0x37]&12));}
 }
 TraceBackend t;RegisterAccess r(t);PhyInit p(r);HardwareInit h(r,t);
 t.resetStuck=true;assert(h.reset()==AccessStatus::Timeout);
 assert(p.softReset()==AccessStatus::Timeout&&r.currentPHYBase()==0xa400);
 TraceBackend f;RegisterAccess fr(f);PhyInit fp(fr);f.failAt=1;
 assert(fp.configure(Chip::RTL8168H)==AccessStatus::IOFailure&&fr.currentPHYBase()==0xa400);
 TraceBackend g(0x5c8);RegisterAccess gr(g);HardwareInit gh(gr,g);
 assert(gh.updateLinkEEE(Chip::RTL8411B,true)==AccessStatus::Ok&&(g.eri[0x1b0]&3)==3);
 assert(gh.updateLinkEEE(Chip::RTL8168H,true)==AccessStatus::Ok&&(g.eri[0x1b0]&3)==0);
}
int main(){registers();firmware();programs();puts("PASS: STAGE1 register/firmware/PHY/MAC/EEE tests and per-access failure injection");}
