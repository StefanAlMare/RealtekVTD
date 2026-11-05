// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "ChipIdentity.hpp"
#include "MAC44TXChecksum.hpp"
#include "MAC44RXChecksum.hpp"
#include "TSO4Assist.hpp"
#include "TSO6.hpp"
#include "FramePolicy.hpp"
namespace realtek_next {
// Independent identity/mode publication. Never relabel MAC34 as MAC44/GU25.
struct MAC34OffloadState {
 TXChecksumPolicy tx,rx,tso4,tso6;
 bool identity=false,initialized=false,nativeReady=false,resources=false;
 bool txRequested=false,rxRequested=false,tso4Requested=false,tso6Requested=false;
 bool txManaged=false,rxManaged=false,tso4Managed=false,tso6Managed=false;
 TXChecksumAssistReceipt txAssist;MAC44RXAssistReceipt rxAssist;
 TSO4AssistReceipt assist4,assist6;
 uint64_t rxPublished[5]{},txHardware[2]{},txSoftware[2]{},txErrors=0;
 void configure(const Identity&id,bool init,bool dma,bool mapper,bool native64,bool prepared,bool jumbo,unsigned ring,unsigned pool,unsigned stride,
                bool txOn,bool rxOn,bool four,bool six,unsigned mtu){
  identity=id.status==ProbeStatus::Supported&&id.chip==Chip::RTL8168EVL&&id.linuxMacVersion==34&&id.xid==0x2c9&&cfgMethod(id.chip)==0;
  initialized=init;nativeReady=identity&&init&&dma&&!mapper&&native64;
  resources=prepared&&jumbo&&ring==64&&pool==65&&stride==16384;
  txRequested=txOn;rxRequested=rxOn;tso4Requested=four;tso6Requested=six;
  tx.configure(nativeReady&&prepared&&txOn);rx.configure(nativeReady&&prepared&&rxOn);
  tso4.configure(nativeReady&&txOn&&four&&resources&&mtu==1500);
  tso6.configure(nativeReady&&txOn&&six&&resources&&mtu==1500);
 }
 bool accepts(const TSO4Plan&p,unsigned mtu)const{return p.valid()&&p.segments()<=64&&p.segmentBytes(0)<=maxFrameBytes(mtu);}
 bool accepts(const TSO6Plan&p,unsigned mtu)const{return p.valid()&&p.segments()<=64&&p.segmentBytes(0)<=maxFrameBytes(mtu);}
 void received(const RXChecksumMetadata&m){for(unsigned j=0;j<5;++j)if((m.checked&m.valid)&(1U<<j))++rxPublished[j];}
};
}
