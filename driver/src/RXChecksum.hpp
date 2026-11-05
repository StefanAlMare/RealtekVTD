// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <stdint.h>
#include <stddef.h>
namespace realtek_next {
// Revision-C RX descriptor format: Mieze a3dad34, Linux-804704.hpp:1374-1399.
// These are RX status bits; never reuse TX checksum/TSO command bits.
constexpr uint32_t kRXIPv4=1U<<30,kRXIPv6=1U<<31,kRXVlanStripped=1U<<16;
constexpr uint32_t kRXTCP=1U<<17,kRXUDP=1U<<18,kRXIPFail=1U<<16,kRXUDPFail=1U<<15,kRXTCPFail=1U<<14;
enum RXChecksumMask:uint8_t {RXCheckedIP=1,RXCheckedTCP=2,RXCheckedUDP=4,RXCheckedTCP6=8,RXCheckedUDP6=16};
enum class RXChecksumReason:uint8_t {Off,Descriptor,Encapsulation,IPv6,Header,Options,Fragment,Length,Protocol,Contradiction,IPFailed,TransportFailed,UDPZero,Eligible,Count};
struct RXChecksumMetadata {
 uint8_t checked=0,valid=0;RXChecksumReason reason=RXChecksumReason::Off;bool ipv6=false;
 // No pointers to a descriptor, DMA buffer, or packet survive the receive call.
};
struct RXChecksumStats {
 uint64_t observations=0,reasons[unsigned(RXChecksumReason::Count)]{};
 uint64_t ipResults=0,tcpResults=0,udpResults=0;
 uint64_t ipv6Observations=0,ipv6Reasons[unsigned(RXChecksumReason::Count)]{},tcp6Results=0,udp6Results=0;
 void observe(const RXChecksumMetadata &m){if(m.ipv6){++ipv6Observations;++ipv6Reasons[unsigned(m.reason)];if(m.valid&RXCheckedTCP6)++tcp6Results;if(m.valid&RXCheckedUDP6)++udp6Results;}++observations;++reasons[unsigned(m.reason)];if(m.valid&RXCheckedIP)++ipResults;if(m.valid&RXCheckedTCP)++tcpResults;if(m.valid&RXCheckedUDP)++udpResults;}
};
inline uint32_t rxChecksumAppleMask(const RXChecksumMetadata &m,bool ipv4,bool ipv6){
 const uint8_t v=m.checked&m.valid;
 if(m.ipv6)return ipv6?(v==RXCheckedTCP6?0x20U:v==RXCheckedUDP6?0x40U:0U):0U;
 return ipv4&&(v==RXCheckedIP||v==(RXCheckedIP|RXCheckedTCP)||v==(RXCheckedIP|RXCheckedUDP))?v:0U;
}
struct RXChecksumHardwareReceipt {bool attempted=false,verified=false;uint16_t written=0,read=0;bool readValid=false;};
inline RXChecksumMetadata decodeRXChecksum(const uint8_t *b,size_t n,uint32_t opts1,uint32_t opts2,bool active,unsigned mtu,bool ipv6Enabled=false,bool ipv4Enabled=true){
 RXChecksumMetadata m;m.ipv6=b&&n>=14&&b[12]==0x86&&b[13]==0xdd;auto fallback=[&](RXChecksumReason why){m.checked=m.valid=0;m.reason=why;return m;};
 if(!active||mtu!=1500)return m;
 // A completed, single descriptor, no receive errors. Length is FCS-inclusive.
 if((opts1&(1U<<31))||(opts1&((1U<<22)|(1U<<21)|(1U<<20)|(1U<<19)))||
    (opts1&0x30000000U)!=0x30000000U||(opts1&0x3fffU)!=n+4)return fallback(RXChecksumReason::Descriptor);
 if(!b||n<14||n>1514)return fallback(RXChecksumReason::Length);
 const auto get=[](const uint8_t *v){return uint16_t(uint16_t(v[0])<<8|v[1]);};
 // Separate IPv6 branch: no IPv6 header checksum exists. Revision-C uses
 // only the selected transport failure bit here; IPv4 IPFail is not its contract.
 if(m.ipv6){
  if(!ipv6Enabled)return fallback(RXChecksumReason::Off);
  if(opts2&kRXVlanStripped)return fallback(RXChecksumReason::Encapsulation);
  if(!(opts2&kRXIPv6)||(opts2&kRXIPv4))return fallback(RXChecksumReason::Contradiction);
  if(n<54||(b[14]>>4)!=6)return fallback(RXChecksumReason::Header);
  const unsigned bytes=get(b+18);if(!bytes||bytes>n-54||bytes+40>1500)return fallback(RXChecksumReason::Length);
  const uint8_t protocol=b[20];const uint32_t type=opts1&(kRXTCP|kRXUDP);
  if(protocol==44)return fallback(RXChecksumReason::Fragment);
  if(protocol==0||protocol==43||protocol==60||protocol==51)return fallback(RXChecksumReason::Options);
  if((protocol!=6&&protocol!=17)||(protocol==6&&type!=kRXTCP)||(protocol==17&&type!=kRXUDP))return fallback(RXChecksumReason::Protocol);
  if(protocol==6){if(bytes<20)return fallback(RXChecksumReason::Length);const unsigned tcp=(b[66]>>4)*4;if(tcp<20||tcp>bytes)return fallback(RXChecksumReason::Header);}
  else {if(bytes<8||get(b+58)!=bytes)return fallback(RXChecksumReason::Length);if(!get(b+60))return fallback(RXChecksumReason::UDPZero);}
  if(opts1&(protocol==6?kRXTCPFail:kRXUDPFail))return fallback(RXChecksumReason::TransportFailed);
  m.checked=m.valid=protocol==6?RXCheckedTCP6:RXCheckedUDP6;m.reason=RXChecksumReason::Eligible;return m;
 }
 if(!ipv4Enabled)return fallback(RXChecksumReason::Off);
 if(get(b+12)!=0x0800||(opts2&kRXVlanStripped))return fallback(RXChecksumReason::Encapsulation);
 if(!(opts2&kRXIPv4)||(opts2&kRXIPv6))return fallback(RXChecksumReason::IPv6);
 if(n<34||(b[14]>>4)!=4)return fallback(RXChecksumReason::Header);
 const unsigned ihl=(b[14]&15)*4,ipLength=get(b+16);
 if(ihl<20||ipLength<ihl||ipLength>n-14||ipLength>1500)return fallback(RXChecksumReason::Length);
 if(ihl!=20)return fallback(RXChecksumReason::Options);
 if(get(b+20)&0xbfffU)return fallback(RXChecksumReason::Fragment); // DF is permitted; reserved/MF/offset are not.
 const uint8_t protocol=b[23];const unsigned bytes=ipLength-20;const uint32_t type=opts1&(kRXTCP|kRXUDP),fail=opts1&(kRXIPFail|kRXTCPFail|kRXUDPFail);
 if((protocol==6&&type!=kRXTCP)||(protocol==17&&type!=kRXUDP)||((protocol!=6&&protocol!=17)&&type))return fallback(RXChecksumReason::Protocol);
 // RXCSUM2: a TCP descriptor interprets RxTCPF, a UDP descriptor RxUDPF.
 // The other transport flag is not a cross-protocol contradiction. This follows
 // the pinned revision-C Mieze/Realtek r8168 receive paths; IPFail stays mandatory.
 // Keep the existing conservative fallback when no L4 protocol is recognized.
 if(!type&&(fail&(kRXTCPFail|kRXUDPFail)))return fallback(RXChecksumReason::Contradiction);
 const uint32_t transportFail=type==kRXTCP?kRXTCPFail:type==kRXUDP?kRXUDPFail:0U;
 if(protocol==6){if(bytes<20)return fallback(RXChecksumReason::Length);const unsigned tcp=(b[46]>>4)*4;if(tcp<20||tcp>bytes)return fallback(RXChecksumReason::Header);}
 if(protocol==17){if(bytes<8||get(b+38)!=bytes)return fallback(RXChecksumReason::Length);}
 // No bad result is relabelled good; absent/failed transport results go to BSD.
 if(fail&kRXIPFail)return fallback(RXChecksumReason::IPFailed);
 m.checked=m.valid=RXCheckedIP;
 if(protocol==17&&!get(b+40)){m.reason=RXChecksumReason::UDPZero;return m;}
 if(fail&transportFail){m.reason=RXChecksumReason::TransportFailed;return m;}
 if(protocol==6)m.checked=m.valid=RXCheckedIP|RXCheckedTCP;
 if(protocol==17)m.checked=m.valid=RXCheckedIP|RXCheckedUDP;
 m.reason=RXChecksumReason::Eligible;return m;
}
}
