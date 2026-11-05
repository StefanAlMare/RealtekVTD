// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "ChipIdentity.hpp"
namespace realtek_next {
// Deliberately normalized protocol requests, translated from symbolic SDK flags.
enum TXChecksumRequest:uint32_t { TXIP=1,TXTCP=2,TXUDP=4,TXTCP6=8,TXUDP6=16 };
enum class TXChecksumMode:uint8_t { None,HardwareIPv4,Software,Error,HardwareIPv6 };
// Numeric identities preserve the existing prepareTXChecksum unsigned ABI.
enum class TXChecksumHardwareProfile:uint8_t {MAC34RevisionC=34,GU25RevisionC=42,MAC44RevisionC=44,MAC46RevisionC=46};
enum class TXChecksumError:uint8_t { None,RequestFlags,Bounds,Header,Protocol,FragmentRequest,Encapsulation };
enum class TXChecksumFallback:uint8_t { None,Policy,Encapsulation,Jumbo,Options,Fragment,Padding,Seed,Other };
inline bool txChecksumIdentity(const Identity &id){return id.status==ProbeStatus::Supported&&id.chip==Chip::RTL8168GU_Method25&&id.xid==0x509&&id.linuxMacVersion==42&&cfgMethod(id.chip)==25;}
// Queried by the framework without taking the output lock. No non-atomic policy state.
class TXChecksumPolicy {
 uint32_t value=0;
public:
 void configure(bool enabled){__atomic_store_n(&value,enabled?1U:0U,__ATOMIC_RELEASE);}
 bool enabled()const{return __atomic_load_n(&value,__ATOMIC_ACQUIRE)!=0;}
};
struct TXChecksumStats {
 uint64_t requestAttempts=0,hardwareScheduled=0,softwareCompletedAttempts=0,softwareSubmitted=0,requestErrors=0,metadataErrors=0;
};
namespace txcsum {
inline uint16_t get(const uint8_t *p){return uint16_t(uint16_t(p[0])<<8|p[1]);}
inline void put(uint8_t *p,uint16_t v){p[0]=uint8_t(v>>8);p[1]=uint8_t(v);}
inline uint32_t sum(const uint8_t *p,size_t n){uint32_t s=0;while(n>=2){s+=get(p);p+=2;n-=2;}if(n)s+=uint32_t(*p)<<8;return s;}
inline uint16_t fold(uint32_t v){while(v>>16)v=(v&65535)+(v>>16);return uint16_t(v);}
struct Packet {
 size_t ip=0,header=0,transport=0,transportBytes=0,checksum=0,ipBytes=0;
 uint32_t pseudo=0;uint16_t seed=0;uint8_t protocol=0;
 bool ipv6=false,fragment=false,bare=false,options=false,udp=false;
};
// Only bounded reads. No checksum field is modified until all requested work is validated.
inline TXChecksumError parse(const uint8_t *b,size_t n,uint32_t request,Packet &p){
 if(!b||n<14)return TXChecksumError::Bounds;
 size_t off=14,limit=n;uint16_t type=get(b+12);unsigned tags=0;
 while(type==0x8100||type==0x88a8){if(++tags>2)return TXChecksumError::Encapsulation;if(off+4>limit)return TXChecksumError::Bounds;type=get(b+off+2);off+=4;}
 p.bare=off==14;
 if(type<=1500){ // IEEE 802.3 LLC/SNAP, no invented metadata offset.
  if(type<8||type>limit-off)return TXChecksumError::Bounds;limit=off+type;
  if(b[off]!=0xaa||b[off+1]!=0xaa||b[off+2]!=3||b[off+3]||b[off+4]||b[off+5])return TXChecksumError::Encapsulation;
  type=get(b+off+6);off+=8;p.bare=false;
 }
 if(type==0x8864){ // PPPoE session carrying IPv4 or IPv6.
  if(off+8>limit)return TXChecksumError::Bounds;size_t bytes=get(b+off+4);
  if(b[off]!=0x11||b[off+1]||!get(b+off+2)||bytes<2||bytes>limit-off-6)return TXChecksumError::Header;
  limit=off+6+bytes;const uint16_t proto=get(b+off+6);off+=8;type=proto==0x21?0x800:proto==0x57?0x86dd:0;p.bare=false;
 }
 p.ip=off;
 if(type==0x800){
  if(off+20>limit)return TXChecksumError::Bounds;if((b[off]>>4)!=4)return TXChecksumError::Header;
  p.header=size_t(b[off]&15)*4;p.ipBytes=get(b+off+2);
  if(p.header<20||p.header>limit-off||p.ipBytes<p.header||p.ipBytes>limit-off)return TXChecksumError::Bounds;
  uint16_t fragments=get(b+off+6);if(fragments&0x8000)return TXChecksumError::Header;p.fragment=(fragments&0x3fff)!=0;
  p.protocol=b[off+9];p.transport=off+p.header;p.transportBytes=p.ipBytes-p.header;p.options=p.header!=20;
  const uint8_t *destination=b+off+16;
  for(size_t k=20;k<p.header;){uint8_t option=b[off+k];if(!option)break;if(option==1){++k;continue;}
   if(k+2>p.header)return TXChecksumError::Header;size_t len=b[off+k+1];if(len<2||len>p.header-k)return TXChecksumError::Header;
   if(option==131||option==137){if(len<7||(len-3)%4)return TXChecksumError::Header;unsigned ptr=b[off+k+2];if(ptr<4||(ptr-4)%4||ptr>len+1)return TXChecksumError::Header;if(ptr<=len)destination=b+off+k+len-4;}
   k+=len;
  }
  if(request&(TXTCP6|TXUDP6))return TXChecksumError::Protocol;
  if(p.fragment&&(request&(TXTCP|TXUDP)))return TXChecksumError::FragmentRequest;
  p.pseudo=sum(b+off+12,4)+sum(destination,4);
 }else if(type==0x86dd){
  if(off+40>limit)return TXChecksumError::Bounds;if((b[off]>>4)!=6)return TXChecksumError::Header;
  const size_t payload=get(b+off+4);if(!payload||payload>limit-off-40)return TXChecksumError::Bounds;
  p.ipv6=true;p.header=40;p.ipBytes=40+payload;p.transport=off+40;p.transportBytes=payload;p.protocol=b[off+6];
  const uint8_t *destination=b+off+24;unsigned depth=0;
  while(p.protocol==0||p.protocol==43||p.protocol==60||p.protocol==51||p.protocol==44){
   if(++depth>16||p.transportBytes<8)return TXChecksumError::Header;size_t size=8;const size_t x=p.transport;const uint8_t protocol=p.protocol;
   if(protocol==44){if(get(b+x+2)&0xfff9)p.fragment=true;}
   else size=protocol==51?size_t(b[x+1]+2)*4:size_t(b[x+1]+1)*8;
   if(size<8||size>p.transportBytes)return TXChecksumError::Bounds;
   if(protocol==43&&b[x+3]){ // Final pseudo-header destination for standard routing forms.
    if(b[x+2]==0||b[x+2]==2){if(size<24||(size-8)%16||b[x+3]>(size-8)/16)return TXChecksumError::Header;destination=b+x+size-16;}
    else if(b[x+2]==4){if(size<24||8+size_t(b[x+4]+1)*16>size||b[x+3]>b[x+4]+1)return TXChecksumError::Header;destination=b+x+8;}
    else return TXChecksumError::Encapsulation;
   }
   p.protocol=b[x];p.transport+=size;p.transportBytes-=size;p.header+=size;p.options=true;
  }
  if(request&(TXIP|TXTCP|TXUDP))return TXChecksumError::Protocol;
  if(p.fragment)return TXChecksumError::FragmentRequest;
  p.pseudo=sum(b+off+8,16)+sum(destination,16);
 }else return TXChecksumError::Encapsulation;
 const uint32_t tcp=request&(TXTCP|TXTCP6),udp=request&(TXUDP|TXUDP6);
 if(!tcp&&!udp)return TXChecksumError::None;
 if(tcp&&udp)return TXChecksumError::RequestFlags;
 if((tcp&&p.protocol!=6)||(udp&&p.protocol!=17))return TXChecksumError::Protocol;
 if(tcp){if(p.transportBytes<20)return TXChecksumError::Bounds;const size_t hdr=size_t(b[p.transport+12]>>4)*4;if(hdr<20||hdr>p.transportBytes)return TXChecksumError::Header;p.checksum=p.transport+16;}
 else {if(p.transportBytes<8)return TXChecksumError::Bounds;const size_t bytes=get(b+p.transport+4);if(bytes<8||bytes>p.transportBytes)return TXChecksumError::Bounds;p.transportBytes=bytes;p.checksum=p.transport+6;p.udp=true;}
 p.pseudo+=uint32_t(p.transportBytes)+p.protocol;p.seed=get(b+p.checksum);
 return TXChecksumError::None;
}
inline bool hardware(const Packet&p,size_t length,uint32_t request,bool eligible){
 if(!eligible||p.ipv6||!p.bare||p.options||p.fragment||p.ip!=14||length>1514||length<60||length!=p.ip+p.ipBytes)return false;
 if(request&(TXTCP|TXUDP)){
  if(p.transportBytes!=p.ipBytes-20)return false;
  const uint16_t expected=fold(p.pseudo);if(p.seed!=expected&&!((p.seed==0||p.seed==65535)&&(expected==0||expected==65535)))return false;
 }
 return true;
}
// Revision-C checksum-only opts2, not the TSO MSS/GSO command. The offset
// identifies the start of TCP/UDP. Hardware consumes the validated folded seed.
inline bool hardware6(const Packet&p,size_t length,uint32_t request,bool eligible){
 if(!eligible||!p.ipv6||!p.bare||p.options||p.fragment||p.ip!=14||p.transport!=54||
    length>1514||length!=p.ip+p.ipBytes||p.transportBytes!=p.ipBytes-40||
    (request!=TXTCP6&&request!=TXUDP6))return false;
 const uint16_t expected=fold(p.pseudo);
 return p.seed==expected||((p.seed==0||p.seed==65535)&&(expected==0||expected==65535));
}
}
class TXChecksumPlan;
inline TXChecksumPlan prepareTXChecksum(uint8_t*,size_t,uint32_t,bool,bool=false,unsigned=42);
class TXChecksumPlan {
 TXChecksumMode kind=TXChecksumMode::None;TXChecksumError fault=TXChecksumError::None;
 const uint8_t *data=nullptr;size_t bytes=0;uint32_t requests=0;
 unsigned hardwareMAC=42;TXChecksumFallback fallback=TXChecksumFallback::None;
 friend TXChecksumPlan prepareTXChecksum(uint8_t*,size_t,uint32_t,bool,bool,unsigned);
public:
 TXChecksumMode mode()const{return kind;}TXChecksumError error()const{return fault;}
 TXChecksumFallback fallbackReason()const{return fallback;}
 bool validFor(const uint8_t *p,size_t size,unsigned mac)const{
  if(kind==TXChecksumMode::None)return !requests;
  if(kind==TXChecksumMode::Error||data!=p||bytes!=size)return false;
  if(kind==TXChecksumMode::Software)return true;
  txcsum::Packet packet;return mac==hardwareMAC&&(mac==42||mac==44||mac==34||mac==46)&&txcsum::parse(p,size,requests,packet)==TXChecksumError::None&&(kind==TXChecksumMode::HardwareIPv6?txcsum::hardware6(packet,size,requests,true):txcsum::hardware(packet,size,requests,true));
 }
 uint32_t descriptorOptions()const{
  // Revision-C descriptor offset4 bits from pinned Mieze CFG25. IPv4 normal
  // headers use its default L4 position; no TSO, RX bit, VLAN tag or IPv6 bit.
  if(kind==TXChecksumMode::HardwareIPv6)return 0x10000000U|(54U<<18)|((requests&TXTCP6)?0x40000000U:0x80000000U);
  return kind==TXChecksumMode::HardwareIPv4?0x20000000U|((requests&TXTCP)?0x40000000U:0)|((requests&TXUDP)?0x80000000U:0)|
   (((hardwareMAC==44||hardwareMAC==34||hardwareMAC==46)&&(requests&(TXTCP|TXUDP)))?(34U<<18):0):0;
 }
};
inline TXChecksumPlan prepareTXChecksum(uint8_t *data,size_t bytes,uint32_t request,bool eligible,bool ipv6Eligible,unsigned hardwareMAC){
 TXChecksumPlan plan;if(!request)return plan;plan.data=data;plan.bytes=bytes;plan.requests=request;plan.kind=TXChecksumMode::Error;
 if(request&~uint32_t(TXIP|TXTCP|TXUDP|TXTCP6|TXUDP6)){plan.fault=TXChecksumError::RequestFlags;return plan;}
 txcsum::Packet p;plan.fault=txcsum::parse(data,bytes,request,p);if(plan.fault!=TXChecksumError::None)return plan;
 plan.hardwareMAC=hardwareMAC;
 if(hardwareMAC!=42&&hardwareMAC!=44&&hardwareMAC!=34&&hardwareMAC!=46){eligible=false;ipv6Eligible=false;}
 // MAC44 revision-C uses the existing validated parser and software finalizer.
 // Keep its initial HW domain narrower: no TCP options, encapsulations or Jumbo.
 if(hardwareMAC==44||hardwareMAC==34||hardwareMAC==46){
  if(!eligible&&!ipv6Eligible)plan.fallback=TXChecksumFallback::Policy;
  else if(!p.bare)plan.fallback=TXChecksumFallback::Encapsulation;
  else if(p.ipBytes>1500||bytes>1514)plan.fallback=TXChecksumFallback::Jumbo;
  else if(p.options||((request&(TXTCP|TXTCP6))&&size_t(data[p.transport+12]>>4)*4!=20))plan.fallback=TXChecksumFallback::Options;
  else if(p.fragment)plan.fallback=TXChecksumFallback::Fragment;
  else if(bytes!=p.ip+p.ipBytes)plan.fallback=TXChecksumFallback::Padding;
  else plan.fallback=TXChecksumFallback::Seed;
  if(plan.fallback!=TXChecksumFallback::Seed){eligible=false;ipv6Eligible=false;}
 }

 if(hardwareMAC==34&&(request&(TXUDP|TXUDP6))){eligible=ipv6Eligible=false;plan.fallback=TXChecksumFallback::Policy;}
 if(txcsum::hardware6(p,bytes,request,ipv6Eligible)){plan.kind=TXChecksumMode::HardwareIPv6;plan.fallback=TXChecksumFallback::None;return plan;}
 if(txcsum::hardware(p,bytes,request,eligible)){plan.kind=TXChecksumMode::HardwareIPv4;plan.fallback=TXChecksumFallback::None;return plan;}
 // Recompute from packet/pseudo-header, never add a possibly arbitrary seed twice.
 if(request&TXIP){txcsum::put(data+p.ip+10,0);txcsum::put(data+p.ip+10,uint16_t(~txcsum::fold(txcsum::sum(data+p.ip,p.header))));}
 if(request&(TXTCP|TXUDP|TXTCP6|TXUDP6)){
  txcsum::put(data+p.checksum,0);uint16_t c=uint16_t(~txcsum::fold(p.pseudo+txcsum::sum(data+p.transport,p.transportBytes)));
  if(p.udp&&!c)c=65535;txcsum::put(data+p.checksum,c);
 }
 plan.kind=TXChecksumMode::Software;return plan;
}
}
