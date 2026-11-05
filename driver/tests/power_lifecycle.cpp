// SPDX-License-Identifier: GPL-2.0-only
#include "../src/PowerLifecycle.hpp"
#include <cassert>
#include <cstdio>
using namespace realtek_next;
int main(){PowerLifecycle p;assert(!p.shouldActivate());p.request(true);assert(p.shouldActivate());
 assert(p.suspend());assert(!p.shouldActivate()&&p.requested);assert(!p.suspend()&&p.sleeps==1);
 assert(p.resume()&&p.shouldActivate());assert(!p.resume()&&p.wakes==1);
 p.suspend();p.request(false);p.resume();assert(!p.shouldActivate()); // User disable during sleep survives wake.
 p.suspend();p.request(true);assert(!p.shouldActivate());p.resume();assert(p.shouldActivate());
 p.result(1);p.result(0);assert(p.failures==1&&p.lastStatus==0);
 for(unsigned i=0;i<100;++i){assert(p.suspend());assert(p.resume());assert(p.shouldActivate());}
 assert(p.sleeps==103&&p.wakes==103);
 std::puts("PASS: requested enable survives sleep, duplicate PM states, admin disable/enable while asleep, failure accounting and repeated cycles");}
