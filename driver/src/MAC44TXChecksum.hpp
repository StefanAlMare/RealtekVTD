// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "TXChecksum.hpp"
#include "TXChecksumAssist.hpp"
namespace realtek_next {
inline bool mac44TXChecksumIdentity(const Identity &id,uint16_t vendor,uint16_t device,
 uint8_t revision,uint16_t subsystemVendor,uint16_t subsystem){
 return id.status==ProbeStatus::Supported&&id.chip==Chip::RTL8411B&&id.linuxMacVersion==44&&
  id.xid==0x5c8&&cfgMethod(id.chip)==0&&vendor==0x10ec&&device==0x8168&&
  revision==0x12&&subsystemVendor==0x1043&&subsystem==0x200f;
}
struct MAC44TXChecksumState {
 TXChecksumPolicy support;
 bool requested=false,identityEligible=false,hardwareInitialized=false,mapperReady=false,native64Ready=false,dmaReady=false;
 TXChecksumAssistReceipt assist;
 uint64_t requests[2]{},hardware[2]{},software[2]{},errors=0,metadataErrors=0,mappingErrors=0;
 uint64_t fallback[9]{};
 struct Slot {unsigned epoch=0;uint32_t request=0;TXChecksumMode mode=TXChecksumMode::None;bool valid=false;} slots[64];
 void configure(const Identity &id,uint16_t vendor,uint16_t device,uint8_t revision,
  uint16_t subVendor,uint16_t subDevice,bool on,bool initialized,bool ready,bool mapper,bool native64=false){
  requested=on;identityEligible=mac44TXChecksumIdentity(id,vendor,device,revision,subVendor,subDevice);
  hardwareInitialized=initialized;mapperReady=ready&&mapper;native64Ready=ready&&!mapper&&native64;dmaReady=mapperReady||native64Ready;
  support.configure(requested&&identityEligible&&hardwareInitialized&&dmaReady);
 }
 bool enabled()const{return support.enabled();}
 void observe(uint32_t request,const TXChecksumPlan &plan){
  if(!request)return;
  if(request&(TXIP|TXTCP|TXUDP))++requests[0];
  if(request&(TXTCP6|TXUDP6))++requests[1];
  if(plan.mode()==TXChecksumMode::Error)++errors;
  if(plan.mode()==TXChecksumMode::Software)++fallback[unsigned(plan.fallbackReason())];
 }
 void submitted(unsigned index,unsigned epoch,uint32_t request,const TXChecksumPlan &plan){
  if(index>=64)return;slots[index]={epoch,request,plan.mode(),true};if(!request)return;
  const unsigned family=(request&(TXTCP6|TXUDP6))?1:0;
  if(plan.mode()==TXChecksumMode::HardwareIPv4||plan.mode()==TXChecksumMode::HardwareIPv6)++hardware[family];
  if(plan.mode()==TXChecksumMode::Software)++software[family];
 }
};
}
