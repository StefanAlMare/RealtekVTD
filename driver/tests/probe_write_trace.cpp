// SPDX-License-Identifier: GPL-2.0-only
#include "tests/TraceBackend.hpp"
#include "src/ProbeProgram.hpp"
#include <cassert>
#include <cstdio>
using namespace realtek_next;
int main(){for(unsigned xid:{0x5c8U,0x541U,0x6c0U}){
 TraceBackend io(xid);io.phy[0xa402]=0x79ed;io.phy[0xa41e]=0x3000;io.phy[0xa408]=0xc61;io.phy[0xa400]=0x1040;
 const uint8_t firmware[]={0,0,31,128}; // existing minimal firmware page restore opcode
 auto result=runProbe(io,io,0,0x10ec,0x8168,firmware,sizeof(firmware));
 assert(result.complete&&result.status==AccessStatus::Ok);
 printf("chip %x\n",xid);
 for(auto &e:io.events)
  if(e.kind=="phy"||e.kind=="mac"||e.kind=="pci"||e.kind=="eri"||e.kind=="ephy-write"||e.kind=="w8"||e.kind=="w16"||
     (e.kind=="w32"&&e.address!=0xb0&&e.address!=0xb8&&e.address!=0x74&&e.address!=0x80))printf("%s %x %x\n",e.kind.c_str(),e.address,e.value);
}}
