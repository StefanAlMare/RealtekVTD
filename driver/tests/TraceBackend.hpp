// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "../src/RegisterAccess.hpp"
#include <map>
#include <vector>
#include <string>
namespace realtek_next {
struct Event {std::string kind;unsigned address;uint32_t value;};
class TraceBackend : public RegisterIO,public PCIConfig {
public:
 std::map<unsigned,uint32_t> mmio,mac,phy,ephy,eri,pci;
 std::vector<Event> events;
 size_t calls=0,failAt=0;
 uint16_t failWriteOffset=0xffff;
 unsigned stuckPort=0;bool stuckWrites=false,resetStuck=false;
 unsigned linkUpAfterDelayCount=0,delayCalls=0;
 uint64_t elapsed=0;
 explicit TraceBackend(unsigned xid=0x541) {
  mmio[0x40]=(xid<<20)|0x800;mmio[0xd3]=0x32;phy[0xa404]=0x001c;phy[0xa406]=0xc916;
  phy[0xa402]=4;phy[0xa5d0]=6;eri[0x1b0]=0x1003;
 }
 bool ok(){return ++calls!=failAt;}
 bool read8(uint16_t off,uint8_t &v) override {if(!ok())return false;v=uint8_t(mmio[off]);events.push_back({"r8",off,v});return true;}
 bool read16(uint16_t off,uint16_t &v) override {if(!ok())return false;v=uint16_t(mmio[off]);events.push_back({"r16",off,v});return true;}
 bool read32(uint16_t off,uint32_t &v) override {if(!ok())return false;v=mmio[off];events.push_back({"r32",off,v});return true;}
 bool write8(uint16_t off,uint8_t v) override {if(off==failWriteOffset||!ok())return false;events.push_back({"w8",off,v});mmio[off]=(off==0x37&&(v&0x10)&&!resetStuck)?0:v;return true;}
 bool write16(uint16_t off,uint16_t v) override {if(off==failWriteOffset||!ok())return false;events.push_back({"w16",off,v});mmio[off]=v;return true;}
 bool write32(uint16_t off,uint32_t v) override {
  if(off==failWriteOffset||!ok())return false;events.push_back({"w32",off,v});const bool write=v&0x80000000U;
  if(stuckPort==off){mmio[off]=stuckWrites?0x80000000U:0;return true;}
  if(off==0xb0||off==0xb8){auto &bank=off==0xb0?mac:phy;unsigned addr=(v>>15)&0xfffe;
   if(write){bank[addr]=v&65535; if(off==0xb8&&addr==0xa400&&(v&0x8000)&&!resetStuck)bank[addr]&=~0x8000U;
    mmio[off]=bank[addr];events.push_back({off==0xb0?"mac":"phy",addr,bank[addr]});}
   else mmio[off]=(off==0xb8?0x80000000U:0)|bank[addr];
  }else if(off==0x80){unsigned addr=(v>>16)&31;if(write)ephy[addr]=v&65535;
   mmio[off]=(write?0:0x80000000U)|ephy[addr];events.push_back({write?"ephy-write":"ephy-read",addr,ephy[addr]});
  }else if(off==0x74){unsigned addr=v&0xfff;if(write){uint32_t mask=(v>>12)&15;
    for(unsigned i=0;i<4;++i)if(mask&(1<<i))eri[addr]=(eri[addr]&~(255U<<(i*8)))|(mmio[0x70]&(255U<<(i*8)));
    events.push_back({"eri",addr,eri[addr]});}else mmio[0x70]=eri[addr];mmio[off]=write?0:0x80000000U;
  }else mmio[off]=v;return true;
 }
 void delayUs(unsigned us) override {
  elapsed+=us;events.push_back({"delay",0,us});
  if(linkUpAfterDelayCount&&++delayCalls>=linkUpAfterDelayCount)phy[0xa402]|=4;
 }
 bool readConfig8(uint16_t off,uint8_t &v) override {if(!ok())return false;v=uint8_t(pci[off]);return true;}
 bool readConfig16(uint16_t off,uint16_t &v) override {if(!ok())return false;v=uint16_t(pci[off]);return true;}
 bool readConfig32(uint16_t off,uint32_t &v) override {if(!ok())return false;v=pci[off];return true;}
 bool writeConfig8(uint16_t off,uint8_t v) override {if(!ok())return false;pci[off]=v;events.push_back({"pci",off,v});return true;}
 bool writeConfig16(uint16_t off,uint16_t v) override {if(!ok())return false;pci[off]=v;return true;}
 bool writeConfig32(uint16_t off,uint32_t v) override {if(!ok())return false;pci[off]=v;return true;}
};
}
