// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "TSO4.hpp"
#include "DMAProgram.hpp"
namespace realtek_next {
constexpr unsigned kTSO6MaxIP=9000,kTSO6PumpBudget=64;
// Private CPU input only. No ifnet/hardware limit or TXPool stride is enlarged.
enum class TSO6InputProfile:uint8_t {Conservative,MAC44VLANSoftware,MAC34Software,MAC46Software};
constexpr unsigned kMAC44VLAN6SoftwareIPMax=65535;
inline bool tso6VLANSoftwareInput(const uint8_t*b,size_t available,size_t frame,unsigned mtu){
 return b&&available>=18&&frame>kTSO6MaxIP+64&&frame<=kMAC44VLAN6SoftwareIPMax+18&&mtu==1500&&
  txcsum::get(b+12)==0x8100&&txcsum::get(b+16)==0x86dd;
}
// Only bounded pre-classification for selecting a private CPU aggregate. The
// full parser still validates every byte before emission. This supports mapper
// MAC46 independently of the historical native-only MAC44 VLAN exception.
inline bool mac46TSO6SoftwareInput(const uint8_t*b,size_t available,size_t frame,unsigned mtu){
 return b&&mtu>=1500&&mtu<=9000&&available>=14&&frame>=14&&frame<=65535U+18U&&softwareTSOLink(b,available,true)!=0;
}
enum class TSO6HardwareReason:unsigned {Eligible,InvalidPlan,MTU,IPv6Layout,TCPOptions,MSSRange,MSSExceedsMTU,SingleSegment,FrameLength,TCPFlags,ReservedBits,ECN,TCPSeed,Count};
inline const char *tso6HardwareReasonName(TSO6HardwareReason r){
 switch(r){
 case TSO6HardwareReason::Eligible:return "Eligible";case TSO6HardwareReason::InvalidPlan:return "InvalidPlan";case TSO6HardwareReason::MTU:return "MTU";
 case TSO6HardwareReason::IPv6Layout:return "IPv6Layout";case TSO6HardwareReason::TCPOptions:return "TCPOptions";case TSO6HardwareReason::MSSRange:return "MSSRange";
 case TSO6HardwareReason::MSSExceedsMTU:return "MSSExceedsMTU";case TSO6HardwareReason::SingleSegment:return "SingleSegment";case TSO6HardwareReason::FrameLength:return "FrameLength";
 case TSO6HardwareReason::TCPFlags:return "TCPFlags";case TSO6HardwareReason::ReservedBits:return "ReservedBits";case TSO6HardwareReason::ECN:return "ECN";case TSO6HardwareReason::TCPSeed:return "TCPSeed";default:return "Unknown";
 }
}
struct TSO6Stats {
 VlanTSOFailure vlanFailure;
 uint64_t requests=0,hardwareSubmitted=0,softwareAccepted=0,softwareSegments=0,softwareCompleted=0,metadataErrors=0,requestErrors=0,retries=0,partialFailures=0,cancelled=0;
 uint64_t hardwareEvaluations=0,hardwarePolicyOff=0,regularPacketWaits=0,hardwareWithTCPOptions=0,hardwareWithPSH=0;
 uint64_t hardwareReasons[unsigned(TSO6HardwareReason::Count)]{},ecnEvaluations[4]{},ecnHardwareSubmitted[4]{};
};
// raw refers to a fully captured private aggregate (legacy pool lease, or the
// bounded MAC44 VLAN CPU copy). Software state retains it until safe completion.
// No 9K array on the kernel stack; only the small HW header is copied here.
class TSO6Plan {
 const uint8_t *raw=nullptr;uint8_t header[kTSO4HeaderBytes]{};
 txcsum::Packet ip;size_t captured=0,tcpHeader=0,payload=0,chunk=0,totalFrame=0,linkBytes=0;
 bool vlanSoftwareOnly=false,mac46SoftwareOnly=false;
 unsigned requestedMSS=0;int snapLength=-1,pppLength=-1;TSO4Error fault=TSO4Error::Metadata;
public:
 TSO4Error error()const{return fault;}bool valid()const{return fault==TSO4Error::None;}
 size_t frameBytes()const{return totalFrame;}size_t headerBytes()const{return captured;}size_t tcpHeaderBytes()const{return tcpHeader;}
 unsigned mss()const{return requestedMSS;}size_t payloadBytes()const{return payload;}
 bool softwareVLANAggregate()const{return valid()&&vlanSoftwareOnly;}
 bool mac46SoftwareAggregate()const{return valid()&&mac46SoftwareOnly;}
 size_t ipBytes()const{return valid()?ip.ipBytes:0;}
 unsigned segments()const{return valid()?unsigned(payload?(payload+chunk-1)/chunk:1):0;}
 size_t segmentPayload(unsigned i)const{size_t off=size_t(i)*chunk;return off<payload?(payload-off<chunk?payload-off:chunk):0;}
 size_t segmentBytes(unsigned i)const{const size_t n=captured+segmentPayload(i);return n<60?60:n;}
 unsigned ipv6ECNCodepoint()const{return valid()?unsigned((header[ip.ip+1]>>4)&3U):4U;}
 bool tcpPSH()const{return valid()&&bool(raw[ip.transport+13]&8U);}
 static TSO6Plan parse(const uint8_t*b,size_t available,size_t frame,unsigned mss,unsigned mtu,TSO6InputProfile profile=TSO6InputProfile::Conservative){
  TSO6Plan p;p.totalFrame=frame;p.requestedMSS=mss;
  if(!b||!mss||mss>65535||mtu<1500||mtu>9000){p.fault=TSO4Error::Metadata;return p;}
  // Unlike TSO4's short capture, every byte traversed by the shared IPv6 parser
  // is present in this private snapshot. Never pass frame as an invented bound.
  const bool mac34Input=profile==TSO6InputProfile::MAC34Software&&mtu==1500&&softwareTSOLink(b,available,true);
  if(profile==TSO6InputProfile::MAC34Software&&!mac34Input){p.fault=TSO4Error::Bounds;return p;}
  const bool vlanInput=profile==TSO6InputProfile::MAC44VLANSoftware&&tso6VLANSoftwareInput(b,available,frame,mtu);
  const bool mac46Input=profile==TSO6InputProfile::MAC46Software&&mac46TSO6SoftwareInput(b,available,frame,mtu);
  if(frame<14||frame>((vlanInput||mac34Input||mac46Input)?kMAC44VLAN6SoftwareIPMax+18:kTSO6MaxIP+64)||available<frame){p.fault=TSO4Error::Bounds;return p;}
  size_t off=14;uint16_t type=txcsum::get(b+12);unsigned tags=0;
  while(type==0x8100||type==0x88a8){if(++tags>2||off+4>frame){p.fault=TSO4Error::Bounds;return p;}type=txcsum::get(b+off+2);off+=4;}
  p.linkBytes=off;
  if(type<=1500){if(off+8>frame){p.fault=TSO4Error::Bounds;return p;}p.snapLength=int(off-2);type=txcsum::get(b+off+6);off+=8;}
  if(type==0x8864){if(off+8>frame){p.fault=TSO4Error::Bounds;return p;}p.pppLength=int(off+4);type=txcsum::get(b+off+6)==0x57?0x86dd:0;off+=8;}
  if(type!=0x86dd){p.fault=TSO4Error::Protocol;return p;}
  auto e=txcsum::parse(b,frame,TXTCP6,p.ip);
  if(e!=TXChecksumError::None){p.fault=e==TXChecksumError::FragmentRequest?TSO4Error::Fragment:TSO4Error::Headers;return p;}
  if(!p.ip.ipv6||p.ip.protocol!=6||p.ip.ipBytes>((vlanInput||mac34Input||mac46Input)?kMAC44VLAN6SoftwareIPMax:kTSO6MaxIP)){p.fault=TSO4Error::Protocol;return p;}
  if(vlanInput){
   // Enlarged input is single-tag, simple IPv6/TCP only: no extension chains,
   // fragments, jumbograms, trailing bytes or a hardware aggregate descriptor.
   if(p.ip.ip!=18||p.ip.header!=40||p.ip.fragment||b[24]!=6||frame!=18+p.ip.ipBytes){p.fault=TSO4Error::Bounds;return p;}
   p.vlanSoftwareOnly=true;
  }
  p.mac46SoftwareOnly=mac46Input&&(p.ip.ipBytes>kTSO6MaxIP||frame>kTSO6MaxIP+64);
  if(p.mac46SoftwareOnly&&frame!=p.ip.ip+p.ip.ipBytes){p.fault=TSO4Error::Bounds;return p;}
  if(mac34Input&&(p.ip.fragment||(p.ip.ipBytes>9000&&(p.ip.header!=40||b[p.ip.ip+6]!=6||frame!=p.ip.ip+p.ip.ipBytes)))){p.fault=TSO4Error::Bounds;return p;}
  // AH authenticates bytes changed by segmentation; no key/SA exists in a TSO
  // request. Such a crypto request cannot be completed safely in this driver.
  uint8_t next=b[p.ip.ip+6];size_t x=p.ip.ip+40;bool home=false;
  while(x<p.ip.transport){
   if(next==51){p.fault=TSO4Error::ControlForm;return p;}
   size_t bytes=next==44?8:size_t(b[x+1]+1)*8;
   if(next==0||next==60){for(size_t k=2;k<bytes;){uint8_t option=b[x+k];if(!option){++k;continue;}if(k+2>bytes||size_t(b[x+k+1])+2>bytes-k){p.fault=TSO4Error::Headers;return p;}
     const size_t size=size_t(b[x+k+1])+2;
     if(next==60&&option==0xc9){if(home||size!=18){p.fault=TSO4Error::Headers;return p;}p.ip.pseudo=p.ip.pseudo-txcsum::sum(b+p.ip.ip+8,16)+txcsum::sum(b+x+k+2,16);home=true;}
     k+=size;
   }}
   next=b[x];x+=bytes;
  }
  p.tcpHeader=size_t(b[p.ip.transport+12]>>4)*4;p.captured=p.ip.transport+p.tcpHeader;
  if(p.captured>frame||p.captured-p.linkBytes>mtu){p.fault=TSO4Error::Bounds;return p;}
  p.payload=p.ip.transportBytes-p.tcpHeader;const size_t max=mtu-(p.captured-p.linkBytes);
  if(!max&&p.payload){p.fault=TSO4Error::Bounds;return p;}p.chunk=max?(mss<max?mss:max):1;
  if((b[p.ip.transport+13]&0x06)&&p.payload>p.chunk){p.fault=TSO4Error::ControlForm;return p;}
  for(size_t k=20;k<p.tcpHeader;){uint8_t opt=b[p.ip.transport+k];if(!opt)break;if(opt==1){++k;continue;}if(k+2>p.tcpHeader||b[p.ip.transport+k+1]<2||b[p.ip.transport+k+1]>p.tcpHeader-k){p.fault=TSO4Error::Headers;return p;}k+=b[p.ip.transport+k+1];}
  if((vlanInput||mac34Input)&&(p.payload?(p.payload+p.chunk-1)/p.chunk:1)>64){p.fault=TSO4Error::Bounds;return p;}
  for(size_t k=0;k<p.captured&&k<sizeof(p.header);++k)p.header[k]=b[k];p.raw=b;p.fault=TSO4Error::None;return p;
 }
 bool hardwareTCPOptionsValid()const{
  if(!valid()||captured>sizeof(header)||tcpHeader<20||tcpHeader>60)return false;bool timestamp=false;
  for(size_t k=20;k<tcpHeader;){uint8_t o=header[ip.transport+k];if(!o){for(;k<tcpHeader;++k)if(header[ip.transport+k])return false;return true;}if(o==1){++k;continue;}if(o!=8||timestamp||k+10>tcpHeader||header[ip.transport+k+1]!=10)return false;timestamp=true;k+=10;}return true;
 }
 TSO6HardwareReason hardwareReason(unsigned mtu)const{
  if(!valid())return TSO6HardwareReason::InvalidPlan;if(mac46SoftwareOnly)return TSO6HardwareReason::FrameLength;if(mtu!=1500)return TSO6HardwareReason::MTU;
  if(!ip.bare||ip.options||ip.fragment||ip.ip!=14||ip.header!=40||ip.transport!=54)return TSO6HardwareReason::IPv6Layout;
  if(!hardwareTCPOptionsValid())return TSO6HardwareReason::TCPOptions;
  if(requestedMSS<64||requestedMSS>2047)return TSO6HardwareReason::MSSRange;if(chunk!=requestedMSS)return TSO6HardwareReason::MSSExceedsMTU;
  if(payload<=chunk)return TSO6HardwareReason::SingleSegment;if(totalFrame!=ip.ip+ip.ipBytes)return TSO6HardwareReason::FrameLength;
  if(header[ip.transport+13]!=0x10&&header[ip.transport+13]!=0x18)return TSO6HardwareReason::TCPFlags;
  if(header[ip.transport+12]&15)return TSO6HardwareReason::ReservedBits;if(ipv6ECNCodepoint()==3)return TSO6HardwareReason::ECN;
  if(txcsum::get(header+ip.transport+16)!=txcsum::fold(ip.pseudo))return TSO6HardwareReason::TCPSeed;return TSO6HardwareReason::Eligible;
 }
 bool hardwareEligible(unsigned mtu)const{return hardwareReason(mtu)==TSO6HardwareReason::Eligible;}
 uint16_t preparedSeed()const{if(!valid()||captured>sizeof(header))return 0;uint32_t v=uint32_t(txcsum::get(header+ip.transport+16))-txcsum::get(header+ip.ip+4);v+=v>>16;return uint16_t(v);}
 bool matches(const uint8_t*b,size_t n,bool prepared=false)const{
  if(!valid()||!b||b!=raw||n!=totalFrame||captured>sizeof(header))return false;
  for(size_t k=0;k<captured;++k){uint8_t expected=header[k];if(prepared){if(k==ip.ip+4||k==ip.ip+5)expected=0;if(k==ip.transport+16)expected=uint8_t(preparedSeed()>>8);if(k==ip.transport+17)expected=uint8_t(preparedSeed());}if(b[k]!=expected)return false;}return true;
 }
 bool prepareHardware(uint8_t*b,size_t n,unsigned mtu)const{
  if(!hardwareEligible(mtu)||!matches(b,n))return false;
  txcsum::put(b+ip.ip+4,0);txcsum::put(b+ip.transport+16,preparedSeed());return matches(b,n,true);
 }
 bool emit(unsigned index,void *destination,size_t n)const{
  if(!valid()||index>=segments()||!destination||n!=segmentBytes(index))return false;auto d=static_cast<uint8_t*>(destination);
  const size_t bytes=segmentPayload(index),offset=size_t(index)*chunk;
  // Offline EMITCOPY1: the normal caller holds separate source/destination
  // TXPool leases. Keep the legacy forward-copy behavior for any aliasing call.
  const uintptr_t sourceAddress=reinterpret_cast<uintptr_t>(raw),destinationAddress=reinterpret_cast<uintptr_t>(d);
  const bool separate=destinationAddress>=sourceAddress?destinationAddress-sourceAddress>=totalFrame:sourceAddress-destinationAddress>=n;
  if(separate){
   __builtin_memcpy(d,raw,captured);
   if(bytes)__builtin_memcpy(d+captured,raw+captured+offset,bytes);
   if(n>captured+bytes)__builtin_memset(d+captured+bytes,0,n-captured-bytes);
  }else{
   for(size_t k=0;k<n;++k)d[k]=k<captured?raw[k]:k<captured+bytes?raw[captured+offset+k-captured]:0;
  }
  const size_t ipSize=ip.header+tcpHeader+bytes;txcsum::put(d+ip.ip+4,uint16_t(ipSize-40));
  uint32_t seq=(uint32_t(txcsum::get(raw+ip.transport+4))<<16)|txcsum::get(raw+ip.transport+6);seq+=uint32_t(offset);txcsum::put(d+ip.transport+4,uint16_t(seq>>16));txcsum::put(d+ip.transport+6,uint16_t(seq));
  if(index+1<segments())d[ip.transport+13]&=uint8_t(~0x09U);if(index)d[ip.transport+13]&=uint8_t(~0x80U);
  if(raw[ip.transport+13]&0x20){unsigned u=txcsum::get(raw+ip.transport+18);if(offset>=u){d[ip.transport+13]&=uint8_t(~0x20U);u=0;}else u-=unsigned(offset);txcsum::put(d+ip.transport+18,uint16_t(u));}
  if(pppLength>=0)txcsum::put(d+pppLength,uint16_t(ipSize+2));if(snapLength>=0)txcsum::put(d+snapLength,uint16_t(captured+bytes-linkBytes));
  txcsum::put(d+ip.transport+16,0);const uint32_t addresses=ip.pseudo-uint32_t(ip.transportBytes)-6;
  txcsum::put(d+ip.transport+16,uint16_t(~txcsum::fold(addresses+6+uint32_t(tcpHeader+bytes)+txcsum::sum(d+ip.transport,tcpHeader+bytes))));return true;
 }
};
enum class TSO6HardwareProfile:uint8_t {GU25,MAC44DeviceMapper,MAC46DeviceMapper};
class TSO6HardwarePlan {
 const uint8_t *data=nullptr;const TSO6Plan *plan=nullptr;TSO6HardwareProfile profile=TSO6HardwareProfile::GU25;
public:
 bool enabled()const{return plan!=nullptr;}
 static TSO6HardwarePlan bind(uint8_t*b,size_t n,const TSO6Plan&p,bool eligible,unsigned mtu,TSO6HardwareProfile profile=TSO6HardwareProfile::GU25){TSO6HardwarePlan h;if(eligible&&((profile!=TSO6HardwareProfile::MAC44DeviceMapper&&profile!=TSO6HardwareProfile::MAC46DeviceMapper)||p.segments()<=64)&&p.prepareHardware(b,n,mtu)){h.data=b;h.plan=&p;h.profile=profile;}return h;}
 bool mac44()const{return enabled()&&profile==TSO6HardwareProfile::MAC44DeviceMapper;}
 bool mac46()const{return enabled()&&profile==TSO6HardwareProfile::MAC46DeviceMapper;}
 bool validFor(const uint8_t*b,size_t n,unsigned mac,unsigned mtu)const{return plan&&((profile==TSO6HardwareProfile::GU25&&mac==42)||(profile==TSO6HardwareProfile::MAC44DeviceMapper&&mac==44&&plan->segments()<=64)||(profile==TSO6HardwareProfile::MAC46DeviceMapper&&mac==46&&plan->segments()<=64))&&data==b&&plan->hardwareEligible(mtu)&&plan->matches(b,n,true);}
 uint32_t options1()const{return plan?(1U<<25)|(54U<<18):0;}
 uint32_t options2()const{return plan?uint32_t(plan->mss())<<18:0;}
};
struct TSO6SoftwareState {
 void *original=nullptr;TSO6Plan plan;DMABuffer aggregate;void *cpuAggregate=nullptr;size_t cpuBytes=0;unsigned next=0,outstanding=0;bool owned=false,cancelled=false;
 bool active()const{return original!=nullptr;}bool remaining()const{return active()&&!cancelled&&next<plan.segments();}
};
struct TSO6CopyContext{const TSO6Plan *plan;unsigned index;};
inline bool copyTSO6Segment(void*c,void*d,size_t n){auto &v=*static_cast<TSO6CopyContext*>(c);return v.plan->emit(v.index,d,n);}
}
