// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "ChipIdentity.hpp"
#include "TXChecksum.hpp"
#include "TXChecksumAssist.hpp"
#include "MAC44RXChecksum.hpp"
#include "TSO4Assist.hpp"
#include "TSO4.hpp"
#include "TSO6.hpp"
#include "JumboProfile.hpp"
#include "FramePolicy.hpp"
namespace realtek_next {
// One coherent profile. These are hardware/DMA facts, never user-supplied
// substitutes for MapperReady. No GU25/MAC44 identity is reused for MAC46.
inline bool mac46FeatureIdentity(const Identity &id,uint16_t vendor,uint16_t device,uint8_t revision){
 return id.status==ProbeStatus::Supported&&id.chip==Chip::RTL8168H&&id.linuxMacVersion==46&&
  id.xid==0x541&&cfgMethod(id.chip)==0&&vendor==0x10ec&&device==0x8168&&revision==0x15;
}
constexpr uint32_t kMAC46TSOIfnetIPLimit=9000;
constexpr uint32_t kMAC46SoftwareIPLimit=65535;
inline uint32_t mac46TSOLimit(bool prepared,bool jumbo,unsigned ring,unsigned pool,unsigned stride,unsigned mtu){
 if(!prepared||ring!=64||pool!=65||(stride!=2048&&stride!=kJumboTXStride)||mtu<1500||mtu>kJumboMTU)return 0;
 if(mtu>1500&&(!jumbo||stride!=kJumboTXStride))return 0;
 // A standard arena can still emit MTU1500 children; IPv6 aggregate input is
 // then CPU-only. This does not enlarge an arena, a descriptor or a HW limit.
 return kMAC46TSOIfnetIPLimit;
}
struct MAC46Features {
 TXChecksumPolicy tx,rx,tso4,tso6;
 bool identityEligible=false,hardwareInitialized=false,mapperReady=false,native64Ready=false,dmaReady=false,resourcesReady=false,resources=false;
 bool jumboReady=false,declarationReady=false,managed=false,txManaged=false,rxManaged=false,tso4Managed=false,tso6Managed=false;
 unsigned mtu=1500,stride=0;uint32_t limit=0;
 TXChecksumAssistReceipt txAssist;
 MAC44RXAssistReceipt rxAssist,rxDisableFallback;
 TSO4AssistReceipt assist4,assist6;
 uint64_t txHardware[2]{},txSoftware[2]{},rxPublished[2]{},rxProtocol[5]{},txRequests[2]{},txErrors=0,metadataErrors=0,ifnetFailures=0;
 void reset(){tx.configure(false);rx.configure(false);tso4.configure(false);tso6.configure(false);
  identityEligible=hardwareInitialized=mapperReady=native64Ready=dmaReady=resourcesReady=resources=jumboReady=declarationReady=false;
  limit=0;stride=0;txAssist={};rxAssist={};rxDisableFallback={};assist4={};assist6={};}
 void configure(const Identity &id,uint16_t vendor,uint16_t device,uint8_t revision,
   bool initialized,bool ready,bool mapper,bool prepared,bool jumbo,unsigned ring,unsigned pool,unsigned slotStride,unsigned selectedMTU,bool native64=false){
  identityEligible=mac46FeatureIdentity(id,vendor,device,revision);hardwareInitialized=initialized;
  mapperReady=ready&&mapper;native64Ready=ready&&!mapper&&native64;dmaReady=mapperReady||native64Ready;
  mtu=selectedMTU;stride=slotStride;jumboReady=jumbo&&stride==kJumboTXStride;
  resourcesReady=prepared&&ring==64&&pool==65&&(stride==2048||stride==kJumboTXStride);resources=resourcesReady;
  declarationReady=identityEligible&&hardwareInitialized&&dmaReady&&resourcesReady;
  limit=mac46TSOLimit(prepared,jumbo,ring,pool,stride,mtu);
  tx.configure(declarationReady);rx.configure(declarationReady);
  tso4.configure(declarationReady&&limit!=0);tso6.configure(declarationReady&&limit!=0);
 }
 bool ready()const{return declarationReady;}
 bool accepts(const TSO4Plan&p,unsigned selectedMTU)const{
  return tso4.enabled()&&p.valid()&&selectedMTU==mtu&&p.ipBytes()<=kMAC46SoftwareIPLimit&&
   p.frameBytes()<=size_t(kMAC46SoftwareIPLimit)+18&&p.segmentBytes(0)<=maxFrameBytes(selectedMTU)&&p.segmentBytes(0)<=stride;
 }
 bool accepts(const TSO6Plan&p,unsigned selectedMTU)const{
  return tso6.enabled()&&p.valid()&&selectedMTU==mtu&&p.ipBytes()<=kMAC46SoftwareIPLimit&&
   p.frameBytes()<=size_t(kMAC46SoftwareIPLimit)+18&&p.segmentBytes(0)<=maxFrameBytes(selectedMTU)&&p.segmentBytes(0)<=stride;
 }
 bool accepts(const TSO4Plan&p)const{return accepts(p,mtu);}bool accepts(const TSO6Plan&p)const{return accepts(p,mtu);}
 void received(const RXChecksumMetadata&m){if(!(m.checked&m.valid))return;++rxPublished[m.ipv6?1:0];
  for(unsigned j=0;j<5;++j)if((m.checked&m.valid)&(1U<<j))++rxProtocol[j];}
};
}
