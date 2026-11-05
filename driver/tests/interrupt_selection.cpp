// SPDX-License-Identifier: GPL-2.0-only
#include "../src/InterruptSelection.hpp"
#include <cassert>
#include <cstdio>
using namespace realtek_next;
struct Provider{int types[5]={1,0x10001,0x10001,0x10001,0x10001};unsigned calls=0,invalid=99;};
bool query(void *context,unsigned index,int &type){auto &p=*static_cast<Provider*>(context);++p.calls;assert(index<5);if(index==p.invalid)return false;type=p.types[index];return true;}
int main(){
 Provider p;auto selected=selectMessageInterrupt(&p,5,0x10000,query);
 assert(selected.found&&selected.index==1&&selected.type==0x10001&&selected.count==5&&selected.validIndices==31);
 p.invalid=1;p.calls=0;selected=selectMessageInterrupt(&p,5,0x10000,query);assert(selected.index==2&&selected.count==4&&selected.validIndices==29);
 p.types[0]=0x50001;selected=selectMessageInterrupt(&p,5,0x10000,query);assert(selected.index==0&&selected.type==0x50001); // MSI-X retains message bit.
 for(auto &type:p.types)type=1;selected=selectMessageInterrupt(&p,5,0x10000,query);assert(!selected.found);
 p.calls=0;assert(!selectMessageInterrupt(&p,0,0x10000,query).found);assert(!p.calls);
 assert(!selectMessageInterrupt(&p,33,0x10000,query).found);assert(!p.calls);
 assert(!selectMessageInterrupt(&p,5,0x10000,nullptr).found);assert(!p.calls);
 std::puts("PASS: provider-declared PCI message index selection, legacy exclusion, query holes, MSI-X flags, bounds and absent MSI");}
