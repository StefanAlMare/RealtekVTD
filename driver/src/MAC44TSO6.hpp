// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "MAC44TSO4.hpp"
#include "TSO6.hpp"
namespace realtek_next {
// Resource geometry and exact identity are shared, preferences are independent.
// AF_INET6 maximum includes the fixed40-byte IPv6 header and TCP/options;
// TSO6Plan independently bounds IP bytes to9000. Never advertise payload9000.
struct MAC44TSO6State:MAC44TSO4State {
 TSO4AssistReceipt disableFallback;uint64_t ifnetFailures=0;
 void reset(){MAC44TSO4State::reset();disableFallback={};}
 void configure(bool on,bool identity,bool initialized,bool mapper,bool checksum,bool prepared,bool jumbo,unsigned ring,unsigned count,unsigned stride,bool native64=false){
  MAC44TSO4State::configure(on,identity,initialized,mapper,checksum,prepared,jumbo,ring,count,stride,native64);
  if(limit>kTSO6MaxIP)limit=kTSO6MaxIP;
 }
 bool accepts(const TSO6Plan&p)const{
  if(!p.valid())return false;
  if(p.softwareVLANAggregate())return ready()&&native64Ready&&!mapperReady&&p.ipBytes()<=kMAC44VLAN6SoftwareIPMax&&p.segmentBytes(0)<=1518;
  return p.frameBytes()<=size_t(limit)+64;
 }
};
}
