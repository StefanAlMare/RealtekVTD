// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <stdint.h>
namespace realtek_next {
// Copies only bounded identity strings; never retain an owner pointer beyond
// the framework iterator lifetime. Missing and truncated strings stay explicit.
inline unsigned ownerCopyText(char *out,unsigned capacity,const char *text){
 if(!capacity)return 0;out[0]=0;if(!text)return 0;
 unsigned n=0;while(n+1<capacity&&text[n]){out[n]=text[n];++n;}out[n]=0;
 return text[n]?2:1;
}
struct OpenOwnerIdentity {
 uint64_t registryID=0;bool isSelf=false,inactive=false;
 unsigned textPresentMask=0,textTruncatedMask=0;
 char className[64]{},name[96]{},bundleID[128]{},driverVersion[96]{},build[96]{};
 void text(unsigned bit,char *out,unsigned capacity,const char *value){
  const unsigned status=ownerCopyText(out,capacity,value);
  if(status)textPresentMask|=bit;if(status==2)textTruncatedMask|=bit;
 }
};
struct OpenOwnerSnapshot {
 static constexpr unsigned limit=4;
 bool attempted=false,iteratorAvailable=false,iteratorValid=false,exhausted=false,truncated=false,nonServiceSeen=false;
 unsigned count=0;OpenOwnerIdentity owners[limit];
 // Flat keys allow the existing optional publisher to remain one immutable
 // dictionary. Prefixes are internal constants, maximum 31 bytes.
 static void key(char *out,const char *prefix,int index,const char *field){
  unsigned n=0;for(unsigned i=0;prefix[i]&&n<31;++i)out[n++]=prefix[i];
  if(index>=0)out[n++]=char('0'+index);
  for(unsigned i=0;field[i]&&n<79;++i)out[n++]=field[i];out[n]=0;
 }
 template<class Sink> bool write(Sink &s,const char *prefix)const{
  if(!attempted)return true;bool ok=true;char k[80];
#define ON(field,value) do{key(k,prefix,-1,field);if(!s.number(k,uint64_t(value)))ok=false;}while(0)
  ON("Attempted",1);ON("IteratorAvailable",iteratorAvailable);ON("IteratorValid",iteratorValid);
  ON("Exhausted",exhausted);ON("Truncated",truncated);ON("NonServiceSeen",nonServiceSeen);
  ON("Complete",iteratorAvailable&&iteratorValid&&exhausted&&!truncated&&!nonServiceSeen);
  ON("Count",count);ON("Limit",limit);
#undef ON
  for(unsigned i=0;i<count&&i<limit;++i){const auto &o=owners[i];
#define OIN(field,value) do{key(k,prefix,int(i),field);if(!s.number(k,uint64_t(value)))ok=false;}while(0)
#define OIT(field,value) do{key(k,prefix,int(i),field);if(!s.text(k,value))ok=false;}while(0)
   OIN("RegistryID",o.registryID);OIN("IsSelf",o.isSelf);OIN("Inactive",o.inactive);
   OIN("TextPresentMask",o.textPresentMask);OIN("TextTruncatedMask",o.textTruncatedMask);
   OIT("Class",o.className);OIT("Name",o.name);OIT("BundleID",o.bundleID);
   OIT("DriverVersion",o.driverVersion);OIT("Build",o.build);
#undef OIN
#undef OIT
  }return ok;
 }
};
}
