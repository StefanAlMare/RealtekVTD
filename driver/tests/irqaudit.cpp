// SPDX-License-Identifier: GPL-2.0-only
#include "../src/IRQAudit.hpp"
#include <cassert>
#include <cstdio>
#include <thread>
using namespace realtek_next;
int main(){IRQAudit a;
 a.filter(0,false,15);a.filter(0xffff,true,15);a.filter(0,true,15);a.filter(0x8000,true,15);a.filter(5,true,15);
 assert(a.filters==5&&a.readFailures==2&&a.unqualified==2&&a.qualified==1);
 assert(a.filterBits[15]==1&&a.filterBits[0]==1&&a.filterBits[2]==1&&a.filterBits[1]==0);
 a.action(false);a.action(true);a.service(5);IRQAudit::add(a.emptyActions);IRQAudit::add(a.ackFailures);IRQAudit::add(a.budgetExhausted);
 assert(a.actions==2&&a.disabledActions==1&&a.passes==1&&a.serviceBits[0]==1&&a.serviceBits[2]==1);
 std::thread first([&]{for(unsigned i=0;i<10000;++i)a.filter(1,true,15);});
 std::thread second([&]{for(unsigned i=0;i<10000;++i)a.filter(4,true,15);});first.join();second.join();
 assert(IRQAudit::read(a.qualified)==20001&&IRQAudit::read(a.filters)==20005);
 assert(a.filterBits[0]==10001&&a.filterBits[2]==10001);
 std::puts("PASS: IRQ cause classification, failed/empty/foreign reads, service/action distinction and concurrent filter accounting");}
