// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <stdint.h>
namespace realtek_next {
struct TXChecksumAssistReceipt {
 bool attempted=false,configured=false,rollbackAttempted=false,rollbackOK=false;
 uint32_t beforeAssist=0,beforeSupported=0,beforeEnabled=0,afterAssist=0,afterSupported=0,afterEnabled=0;
 int status=0;
};
// Public ifnet checksum transaction. TX assist and RX capability are independent.
// RXCSUM1 supplies receiveEnabled only for exact GU25; legacy callers default OFF.
// set_offload recomputes capabilities. Explicitly restore all unrelated bits,
// including capabilities not represented by its offload flags, before readback.
template<class IO> bool configureTXChecksumAssist(IO &io,bool enabled,TXChecksumAssistReceipt &r,bool receiveEnabled=false,uint32_t ipv6Mask=0,bool tx6=false,bool rx6=false){
 r=TXChecksumAssistReceipt{};if(!io.valid()){r.status=-1;return false;}r.attempted=true;
 r.beforeAssist=io.offload();r.beforeSupported=io.supported();r.beforeEnabled=io.enabled();
 const uint32_t assist=(r.beforeAssist&~(IO::txAssistMask|ipv6Mask))|(enabled?IO::txAssistMask:0)|(tx6?ipv6Mask:0);
 const uint32_t capabilities=((enabled||tx6)?IO::txCapability:0)|((receiveEnabled||rx6)?(IO::checksumCaps&~IO::txCapability):0);
 const uint32_t supported=(r.beforeSupported&~IO::checksumCaps)|capabilities;
 const uint32_t active=(r.beforeEnabled&~IO::checksumCaps)|capabilities;
 auto read=[&](){r.afterAssist=io.offload();r.afterSupported=io.supported();r.afterEnabled=io.enabled();};
 auto rollback=[&](){r.rollbackAttempted=true;const int a=io.setOffload(r.beforeAssist),b=io.setSupported(r.beforeSupported),c=io.setEnabled(r.beforeEnabled);read();
  r.rollbackOK=!a&&!b&&!c&&r.afterAssist==r.beforeAssist&&r.afterSupported==r.beforeSupported&&r.afterEnabled==r.beforeEnabled;return false;};
 r.status=io.setOffload(assist);if(r.status)return rollback();
 r.status=io.setSupported(supported);if(r.status)return rollback();
 r.status=io.setEnabled(active);if(r.status)return rollback();read();
 if(r.afterAssist!=assist||(r.afterSupported&IO::checksumCaps)!=capabilities||(r.afterEnabled&IO::checksumCaps)!=capabilities||
    (r.afterSupported&~IO::checksumCaps)!=(r.beforeSupported&~IO::checksumCaps)||
    (r.afterEnabled&~IO::checksumCaps)!=(r.beforeEnabled&~IO::checksumCaps)){r.status=-2;return rollback();}
 r.configured=true;return true;
}
}
