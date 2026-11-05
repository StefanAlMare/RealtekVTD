// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "TSO4.hpp"
#include "JumboProfile.hpp"
#include "TSO4Assist.hpp"
namespace realtek_next {
constexpr unsigned kMAC44TSO4HardwareSegments=64,kMAC44TSO4HardwareFrame=64000;
// XNU's family TSO limit includes IP/TCP/options (tcp_output subtracts hdrlen).
// Reserve the parser's full64-byte L2/encapsulation allowance for every limit.
// No allocation or ring/pool growth: standard-resource fallback declares OFF.
inline uint32_t mac44TSO4Limit(bool prepared,bool jumbo,unsigned ring,unsigned count,unsigned stride){
 if(!prepared||!jumbo||ring!=64||count!=65||stride!=kJumboTXStride)return 0;
 unsigned cap=stride<0x3fffU?stride:0x3fffU; // one snapshot, one TX length field
 if(cap>kMAC44TSO4HardwareFrame)cap=kMAC44TSO4HardwareFrame;
 if(cap<=64)return 0;cap-=64;
 return cap<kTSO4MaxIP?cap:kTSO4MaxIP; // bounded shared parser/emitter is the final cap
}
struct MAC44TSO4State {
 TXChecksumPolicy readyPolicy,hardwarePolicy;
 bool requested=false,identityEligible=false,hardwareInitialized=false,mapperReady=false,native64Ready=false,dmaReady=false,resourcesReady=false,managed=false;
 uint32_t limit=0;TSO4AssistReceipt assist;
 uint64_t aggregateRejected=0,segmentLimitFallback=0,postOwnErrors=0;
 uint64_t vlanSoftwareAccepted=0,vlanSoftwareMaxIPBytes=0;
 void reset(){readyPolicy.configure(false);hardwarePolicy.configure(false);requested=identityEligible=hardwareInitialized=mapperReady=native64Ready=dmaReady=resourcesReady=false;limit=0;assist={};}
 void configure(bool on,bool identity,bool initialized,bool mapper,bool txChecksum,bool prepared,bool jumbo,unsigned ring,unsigned count,unsigned stride,bool native64=false){
  hardwarePolicy.configure(false);readyPolicy.configure(false);
  requested=on;identityEligible=identity;hardwareInitialized=initialized;mapperReady=mapper;native64Ready=!mapper&&native64;dmaReady=mapperReady||native64Ready;
  limit=mac44TSO4Limit(prepared,jumbo,ring,count,stride);resourcesReady=limit>1500;
  readyPolicy.configure(requested&&identityEligible&&hardwareInitialized&&dmaReady&&txChecksum&&resourcesReady);
 }
 bool ready()const{return readyPolicy.enabled();}bool enabled()const{return hardwarePolicy.enabled();}
 bool accepts(const TSO4Plan&p)const{
  if(!p.valid())return false;
  if(p.softwareVLANAggregate())return ready()&&p.ipBytes()<=kMAC44VLANSoftwareIPMax&&p.segmentBytes(0)<=1518;
  return p.frameBytes()<=size_t(limit)+64;
 }
};
}
