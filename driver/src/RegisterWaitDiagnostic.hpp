// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <stdint.h>
namespace realtek_next {
// Scalar observation of existing waits. No I/O, allocation or clock calls.
struct RegisterWaitDiagnostic {
 bool available=false,readAvailable=false,readValid=false,commandAvailable=false;
 unsigned attempts=0,limit=0,intervalUs=0,status=0,sampleAttempt=0;
 uint32_t logical=0,port=0,mask=0,expected=0,value=0,command=0;
 const char *kind="none";
 void begin(const char *k,uint32_t a,uint32_t p,uint32_t m,uint32_t e,unsigned n,unsigned us){
  *this=RegisterWaitDiagnostic{};available=true;kind=k;logical=a;port=p;mask=m;expected=e;limit=n;intervalUs=us;
 }
 void sample(uint32_t v){value=v;readAvailable=true;readValid=v!=0xffffffffU;sampleAttempt=attempts;}
};
}
