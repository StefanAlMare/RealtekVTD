// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <stdint.h>
namespace realtek_next {
// Filter executes in interrupt context: lock-free atomic counters only.
struct IRQAudit {
 uint64_t filters=0,qualified=0,readFailures=0,unqualified=0;
 uint64_t filterBits[16]{};
 uint64_t actions=0,disabledActions=0,emptyActions=0,passes=0,ackFailures=0,budgetExhausted=0;
 uint64_t serviceBits[4]{};
 static void add(uint64_t &value){__atomic_fetch_add(&value,uint64_t(1),__ATOMIC_RELAXED);}
 static uint64_t read(const uint64_t &value){return __atomic_load_n(&value,__ATOMIC_RELAXED);}
 void filter(uint16_t status,bool success,uint16_t mask){
  add(filters);
  if(!success||status==0xffff){add(readFailures);return;}
  for(unsigned i=0;i<16;++i)if(status&(1U<<i))add(filterBits[i]);
  if(status&mask)add(qualified);else add(unqualified);
 }
 void action(bool enabled){add(actions);if(!enabled)add(disabledActions);}
 void service(uint16_t status){add(passes);for(unsigned i=0;i<4;++i)if(status&(1U<<i))add(serviceBits[i]);}
};
}
