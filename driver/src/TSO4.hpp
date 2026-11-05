// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "TXChecksum.hpp"
#include "TXSnapshot.hpp"
#include "VlanTSOFailure.hpp"
namespace realtek_next {
constexpr unsigned kTSO4MaxIP=9000,kTSO4HeaderBytes=192,kTSO4PumpBudget=64;
// Input-domain exception is explicit and MAC44/controller-authorized. Hardware
// limits remain9K; only single-tag IPv4 software segmentation can read more.
enum class TSO4InputProfile:uint8_t {Conservative,MAC44VLANSoftware,MAC34Software,MAC46Software};
constexpr unsigned kMAC44VLANSoftwareIPMax=65535;
inline unsigned softwareTSOLink(const uint8_t*b,size_t n,bool six){
 if(!b||n<14)return 0;const uint16_t type=txcsum::get(b+12),wanted=six?0x86dd:0x800;
 if(type==wanted)return 14;
 return n>=18&&type==0x8100&&txcsum::get(b+16)==wanted?18:0;
}
using TXRangeCopy=bool(*)(void*,size_t,void*,size_t);
enum class TSO4Error:uint32_t {None,Metadata,Bounds,Protocol,Fragment,Headers,ControlForm,Resource};
// OPT1 observations are attempt counters. They are not wire measurements.
enum class TSO4HardwareReason:unsigned {
 Eligible,InvalidPlan,MTU,IPv4Layout,TCPOptions,MSSRange,MSSExceedsMTU,
 SingleSegment,FrameLength,TCPFlags,ReservedBits,ECN,IPChecksum,TCPSeed,Count
};
inline const char *tso4HardwareReasonName(TSO4HardwareReason r){
 switch(r){
 case TSO4HardwareReason::Eligible:return "Eligible";
 case TSO4HardwareReason::InvalidPlan:return "InvalidPlan";
 case TSO4HardwareReason::MTU:return "MTU";
 case TSO4HardwareReason::IPv4Layout:return "IPv4Layout";
 case TSO4HardwareReason::TCPOptions:return "TCPOptions";
 case TSO4HardwareReason::MSSRange:return "MSSRange";
 case TSO4HardwareReason::MSSExceedsMTU:return "MSSExceedsMTU";
 case TSO4HardwareReason::SingleSegment:return "SingleSegment";
 case TSO4HardwareReason::FrameLength:return "FrameLength";
 case TSO4HardwareReason::TCPFlags:return "TCPFlags";
 case TSO4HardwareReason::ReservedBits:return "ReservedBits";
 case TSO4HardwareReason::ECN:return "ECN";
 case TSO4HardwareReason::IPChecksum:return "IPChecksum";
 case TSO4HardwareReason::TCPSeed:return "TCPSeed";
 default:return "Unknown";
 }
}
struct TSO4Stats {
 VlanTSOFailure vlanFailure;
 uint64_t requests=0,hardwareSubmitted=0,softwareAccepted=0,softwareSegments=0,softwareCompleted=0,metadataErrors=0,requestErrors=0,retries=0,partialFailures=0,cancelled=0;
 uint64_t hardwareEvaluations=0,hardwarePolicyOff=0,regularPacketWaits=0;
 uint64_t hardwareWithTCPOptions=0,hardwareWithPSH=0;
 uint64_t hardwareReasons[unsigned(TSO4HardwareReason::Count)]{};
 // ECN1: disjoint IP-codepoint observations of validated plans/consumed HW.
 // Caller serializes these counters with the existing controller lock.
 uint64_t ecnEvaluations[4]{},ecnHardwareSubmitted[4]{};
};
// A bounded immutable header and geometry; original mbuf bytes never change.
class TSO4Plan {
 uint8_t header[kTSO4HeaderBytes]{};size_t captured=0;
 txcsum::Packet ip;size_t tcpHeader=0,payload=0,chunk=0,totalFrame=0,linkBytes=0;
 bool vlanSoftwareOnly=false,mac34SoftwareOnly=false,mac46SoftwareOnly=false;
 unsigned requestedMSS=0;int snapLength=-1,pppLength=-1;TSO4Error fault=TSO4Error::Metadata;
public:
 TSO4Error error()const{return fault;}bool valid()const{return fault==TSO4Error::None;}
 size_t headerBytes()const{return captured;}size_t payloadBytes()const{return payload;}
 bool mac34SoftwareAggregate()const{return valid()&&mac34SoftwareOnly;}
 bool mac46SoftwareAggregate()const{return valid()&&mac46SoftwareOnly;}
 bool softwareVLANAggregate()const{return valid()&&vlanSoftwareOnly;}
 size_t ipBytes()const{return valid()?ip.ipBytes:0;}
 size_t frameBytes()const{return totalFrame;}unsigned mss()const{return requestedMSS;}
 size_t segmentPayload(unsigned i)const{const size_t off=size_t(i)*chunk;return off<payload?(payload-off<chunk?payload-off:chunk):0;}
 unsigned segments()const{return valid()?unsigned(payload?(payload+chunk-1)/chunk:1):0;}
 size_t segmentBytes(unsigned i)const{const size_t n=captured+segmentPayload(i);return n<60?60:n;}
 static TSO4Plan parse(const uint8_t*b,size_t available,size_t frame,unsigned mss,unsigned mtu,TSO4InputProfile profile=TSO4InputProfile::Conservative){
  TSO4Plan p;p.totalFrame=frame;p.requestedMSS=mss;
  if(!b||!mss||mss>65535||mtu<1500||mtu>9000){p.fault=TSO4Error::Metadata;return p;}
  const bool vlanInput=profile==TSO4InputProfile::MAC44VLANSoftware&&mtu==1500&&available>=18&&
   txcsum::get(b+12)==0x8100&&txcsum::get(b+16)==0x800;
  const bool mac34Input=profile==TSO4InputProfile::MAC34Software&&mtu==1500&&softwareTSOLink(b,available,false);
  if(profile==TSO4InputProfile::MAC34Software&&!mac34Input){p.fault=TSO4Error::Bounds;return p;}
  const bool mac46Input=profile==TSO4InputProfile::MAC46Software&&softwareTSOLink(b,available,false);
  const size_t inputIPMax=(vlanInput||mac34Input||mac46Input)?kMAC44VLANSoftwareIPMax:kTSO4MaxIP;
  const size_t inputFrameMax=(vlanInput||mac34Input||mac46Input)?inputIPMax+18:kTSO4MaxIP+64;
  if(available<14||frame<14||frame>inputFrameMax){p.fault=TSO4Error::Bounds;return p;}
  size_t off=14;uint16_t type=txcsum::get(b+12);p.linkBytes=14;
  unsigned tags=0;while(type==0x8100||type==0x88a8){if(++tags>2||available<off+4){p.fault=TSO4Error::Bounds;return p;}type=txcsum::get(b+off+2);off+=4;}p.linkBytes=off;
  if(type<=1500){if(available<off+8){p.fault=TSO4Error::Bounds;return p;}p.snapLength=int(off-2);type=txcsum::get(b+off+6);off+=8;}
  if(type==0x8864){if(available<off+8){p.fault=TSO4Error::Bounds;return p;}p.pppLength=int(off+4);type=txcsum::get(b+off+6)==0x21?0x800:0;off+=8;}
  // Reject IPv6 before entering the shared parser: its extension walk is not
  // bounded by this fixed IPv4 header capture. No TSO6 capability is offered.
  if(type!=0x800){p.fault=TSO4Error::Protocol;return p;}
  if(available<off+20||available<off+size_t(b[off]&15)*4+20){p.fault=TSO4Error::Bounds;return p;}
  auto e=txcsum::parse(b,frame,TXIP|TXTCP,p.ip);
  if(e!=TXChecksumError::None){p.fault=e==TXChecksumError::FragmentRequest?TSO4Error::Fragment:TSO4Error::Headers;return p;}
  if(p.ip.ipv6||p.ip.protocol!=6||p.ip.ipBytes>inputIPMax){p.fault=TSO4Error::Protocol;return p;}
  if(frame>kTSO4MaxIP+64||p.ip.ipBytes>kTSO4MaxIP){
   // No enlarged aggregate snapshot or descriptor. The original mbuf is kept
   // by the existing software parent; only its <=1518-byte children get DMA.
   if(frame!=p.ip.ip+p.ip.ipBytes||(!mac46Input&&((!vlanInput&&!mac34Input)||(mac34Input?p.ip.ip!=softwareTSOLink(b,available,false):p.ip.ip!=18)))){p.fault=TSO4Error::Bounds;return p;}
   p.vlanSoftwareOnly=vlanInput&&!mac34Input;
   p.mac46SoftwareOnly=mac46Input;
  }
  p.tcpHeader=size_t(b[p.ip.transport+12]>>4)*4;p.captured=p.ip.transport+p.tcpHeader;
  if(p.captured>available||p.captured>kTSO4HeaderBytes||p.captured<p.linkBytes||p.captured-p.linkBytes>=mtu){p.fault=TSO4Error::Bounds;return p;}
  p.payload=p.ip.transportBytes-p.tcpHeader;const size_t max=mtu-(p.captured-p.linkBytes);p.chunk=mss<max?mss:max;
  // TCP control packets can be TSO-marked even with no payload. Multi-segment
  // SYN/RST is not a form generated by the audited XNU TSO path.
  if((b[p.ip.transport+13]&0x06)&&p.payload>p.chunk){p.fault=TSO4Error::ControlForm;return p;}
  for(size_t k=20;k<p.tcpHeader;){uint8_t opt=b[p.ip.transport+k];if(!opt)break;if(opt==1){++k;continue;}if(k+2>p.tcpHeader||b[p.ip.transport+k+1]<2||b[p.ip.transport+k+1]>p.tcpHeader-k){p.fault=TSO4Error::Headers;return p;}k+=b[p.ip.transport+k+1];}
  if(mac34Input&&(p.payload?(p.payload+p.chunk-1)/p.chunk:1)>64){p.fault=TSO4Error::Bounds;return p;}
  p.mac34SoftwareOnly=mac34Input;
  for(size_t k=0;k<p.captured;++k)p.header[k]=b[k];p.fault=TSO4Error::None;return p;
 }
 // OPT1: only protocol-defined, segment-invariant TCP options are added.
 // Unknown, duplicated timestamp, or nonzero EOL padding stays software.
 bool hardwareTCPOptionsValid()const{
  if(!valid()||tcpHeader<20||tcpHeader>60||ip.transport+tcpHeader>captured)return false;
  bool timestamp=false;
  for(size_t k=20;k<tcpHeader;){
   const uint8_t option=header[ip.transport+k];
   if(option==0){for(;k<tcpHeader;++k)if(header[ip.transport+k]!=0)return false;return true;}
   if(option==1){++k;continue;}
   if(option!=8||timestamp||k+10>tcpHeader||header[ip.transport+k+1]!=10)return false;
   timestamp=true;k+=10;
  }
  return true;
 }
 // First unmet condition, not a reconstruction of the historical capture.
 // The decision is pure: no flags, timestamps, seeds or payload bytes change.
 TSO4HardwareReason hardwareReason(unsigned mtu)const{
  if(!valid())return TSO4HardwareReason::InvalidPlan;
  if(mac46SoftwareOnly)return TSO4HardwareReason::FrameLength;
  if(mtu!=1500)return TSO4HardwareReason::MTU;
  if(!ip.bare||ip.options||ip.ip!=14||ip.header!=20)return TSO4HardwareReason::IPv4Layout;
  if(!hardwareTCPOptionsValid())return TSO4HardwareReason::TCPOptions;
  if(requestedMSS<64||requestedMSS>2047)return TSO4HardwareReason::MSSRange;
  if(chunk!=requestedMSS)return TSO4HardwareReason::MSSExceedsMTU;
  if(payload<=chunk)return TSO4HardwareReason::SingleSegment;
  if(totalFrame!=ip.ip+ip.ipBytes)return TSO4HardwareReason::FrameLength;
  const uint8_t flags=header[ip.transport+13];
  // Add ACK|PSH only. FIN, URG, SYN, RST, ECE and CWR remain software.
  if(flags!=0x10&&flags!=0x18)return TSO4HardwareReason::TCPFlags;
  if(header[ip.transport+12]&15)return TSO4HardwareReason::ReservedBits;
  // ECN1: preserve the IP ECN/DSCP byte. ECT(0)/ECT(1) alone are not
  // TCP ECE/CWR. The original TCP flag/option/seed/MSS guards still apply.
  // CE (codepoint 3) stays on the unchanged software path in this candidate.
  if((header[ip.ip+1]&3U)==3U)return TSO4HardwareReason::ECN;
  if(txcsum::get(header+ip.ip+10)!=0)return TSO4HardwareReason::IPChecksum;
  if(txcsum::get(header+ip.transport+16)!=txcsum::fold(ip.pseudo))return TSO4HardwareReason::TCPSeed;
  return TSO4HardwareReason::Eligible;
 }
 bool hardwareEligible(unsigned mtu)const{return hardwareReason(mtu)==TSO4HardwareReason::Eligible;}
 // 0=NotECT, 1=ECT(1), 2=ECT(0), 3=CE, 4=unavailable (invalid plan).
 unsigned ipv4ECNCodepoint()const{return valid()?unsigned(header[ip.ip+1]&3U):4U;}
 size_t tcpHeaderBytes()const{return valid()?tcpHeader:0;}
 bool tcpPSH()const{return valid()&&bool(header[ip.transport+13]&8U);}
 bool matches(const uint8_t*b,size_t n)const{if(!valid()||!b||n!=totalFrame)return false;for(size_t k=0;k<captured;++k)if(header[k]!=b[k])return false;return true;}
 bool emit(unsigned index,void*destination,size_t n,TXRangeCopy copy,void*context)const{
  if(!valid()||index>=segments()||!destination||!copy||n!=segmentBytes(index))return false;
  auto d=static_cast<uint8_t*>(destination);for(size_t k=0;k<n;++k)d[k]=k<captured?header[k]:0;
  const size_t bytes=segmentPayload(index),offset=size_t(index)*chunk;
  if(bytes&&!copy(context,captured+offset,d+captured,bytes))return false;
  const size_t ipSize=ip.header+tcpHeader+bytes;
  txcsum::put(d+ip.ip+2,uint16_t(ipSize));txcsum::put(d+ip.ip+4,uint16_t(txcsum::get(header+ip.ip+4)+index));
  const uint32_t seq=uint32_t(txcsum::get(header+ip.transport+4))<<16|txcsum::get(header+ip.transport+6);
  const uint32_t next=seq+uint32_t(offset);txcsum::put(d+ip.transport+4,uint16_t(next>>16));txcsum::put(d+ip.transport+6,uint16_t(next));
  if(index+1<segments())d[ip.transport+13]&=uint8_t(~0x09U); // FIN/PSH only on final segment
  if(index)d[ip.transport+13]&=uint8_t(~0x80U); // CWR only on first; ECE unchanged
  if(header[ip.transport+13]&0x20){unsigned urgent=txcsum::get(header+ip.transport+18);if(offset>=urgent){d[ip.transport+13]&=uint8_t(~0x20U);urgent=0;}else urgent-=unsigned(offset);txcsum::put(d+ip.transport+18,uint16_t(urgent));}
  if(pppLength>=0)txcsum::put(d+pppLength,uint16_t(ipSize+2));
  if(snapLength>=0)txcsum::put(d+snapLength,uint16_t(captured+bytes-linkBytes));
  txcsum::put(d+ip.ip+10,0);txcsum::put(d+ip.ip+10,uint16_t(~txcsum::fold(txcsum::sum(d+ip.ip,ip.header))));
  txcsum::put(d+ip.transport+16,0);const uint32_t addresses=ip.pseudo-uint32_t(ip.transportBytes)-6;
  txcsum::put(d+ip.transport+16,uint16_t(~txcsum::fold(addresses+6+uint32_t(tcpHeader+bytes)+txcsum::sum(d+ip.transport,tcpHeader+bytes))));return true;
 }
};
enum class TSO4HardwareProfile:uint8_t {GU25,MAC44DeviceMapper,MAC46DeviceMapper};
class TSO4HardwarePlan {
 const uint8_t *data=nullptr;const TSO4Plan *plan=nullptr;TSO4HardwareProfile profile=TSO4HardwareProfile::GU25;
public:
 bool enabled()const{return plan!=nullptr;}
 static TSO4HardwarePlan bind(const uint8_t*b,size_t n,const TSO4Plan&p,bool eligible,unsigned mtu,TSO4HardwareProfile profile=TSO4HardwareProfile::GU25){TSO4HardwarePlan h;if(eligible&&p.hardwareEligible(mtu)&&p.matches(b,n)&&((profile!=TSO4HardwareProfile::MAC44DeviceMapper&&profile!=TSO4HardwareProfile::MAC46DeviceMapper)||p.segments()<=64)){h.data=b;h.plan=&p;h.profile=profile;}return h;}
 bool mac44()const{return enabled()&&profile==TSO4HardwareProfile::MAC44DeviceMapper;}
 bool mac46()const{return enabled()&&profile==TSO4HardwareProfile::MAC46DeviceMapper;}
 bool validFor(const uint8_t*b,size_t n,unsigned mac,unsigned mtu)const{return plan&&((profile==TSO4HardwareProfile::GU25&&mac==42)||(profile==TSO4HardwareProfile::MAC44DeviceMapper&&mac==44&&plan->segments()<=64)||(profile==TSO4HardwareProfile::MAC46DeviceMapper&&mac==46&&plan->segments()<=64))&&b==data&&plan->hardwareEligible(mtu)&&plan->matches(b,n);}
 uint32_t options1()const{return plan?(1U<<26)|(34U<<18):0;}
 uint32_t options2()const{return plan?uint32_t(plan->mss())<<18:0;}
};
struct TSO4SoftwareState {
 void *original=nullptr;TSO4Plan plan;unsigned next=0,outstanding=0;bool owned=false,cancelled=false;
 bool active()const{return original!=nullptr;}bool remaining()const{return active()&&!cancelled&&next<plan.segments();}
};
struct TSO4CopyContext{const TSO4Plan *plan;unsigned index;TXRangeCopy read;void *original;};
inline bool copyTSO4Segment(void *c,void*d,size_t n){auto &v=*static_cast<TSO4CopyContext*>(c);return v.plan->emit(v.index,d,n,v.read,v.original);}
}
