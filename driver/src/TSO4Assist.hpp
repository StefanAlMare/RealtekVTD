// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <stdint.h>
namespace realtek_next {
struct TSO4AssistReceipt {
 bool attempted=false,configured=false,enabled=false,priorLimitKnown=false,rollbackAttempted=false,rollbackOK=false;
 uint32_t beforeAssist=0,beforeSupported=0,beforeEnabled=0,beforeLimit=0,afterAssist=0,afterSupported=0,afterEnabled=0,afterLimit=0;
 int status=0;
};
// Caller stops/joins the output consumer and holds no controller IOLock. The
// flag must precede set_tso_mtu; consumers cannot observe its default 64K limit.
template<class IO> bool configureTSO4Assist(IO &io,bool enabled,TSO4AssistReceipt&r,uint32_t maximumIP=9000){
 r={};if(enabled&&(!maximumIP||maximumIP>65535)){r.status=-3;return false;}if(!io.valid()){r.status=-1;return false;}r.attempted=true;r.beforeAssist=io.offload();r.beforeSupported=io.supported();r.beforeEnabled=io.enabled();
 if(r.beforeAssist&IO::tsoAssist){r.status=io.getLimit(r.beforeLimit);if(r.status)return false;r.priorLimitKnown=true;}
 const uint32_t a=(r.beforeAssist&~IO::tsoAssist)|(enabled?IO::tsoAssist:0),s=(r.beforeSupported&~IO::tsoCapability)|(enabled?IO::tsoCapability:0),e=(r.beforeEnabled&~IO::tsoCapability)|(enabled?IO::tsoCapability:0);
 auto read=[&](){r.afterAssist=io.offload();r.afterSupported=io.supported();r.afterEnabled=io.enabled();r.afterLimit=0;return (r.afterAssist&IO::tsoAssist)?io.getLimit(r.afterLimit):0;};
 auto rollback=[&](){r.rollbackAttempted=true;int x=io.setOffload(r.beforeAssist);int limit=0;if(r.priorLimitKnown)limit=io.setLimit(r.beforeLimit);int y=io.setSupported(r.beforeSupported),z=io.setEnabled(r.beforeEnabled);int q=read();r.rollbackOK=!x&&!limit&&!y&&!z&&!q&&r.afterAssist==r.beforeAssist&&r.afterSupported==r.beforeSupported&&r.afterEnabled==r.beforeEnabled&&(!r.priorLimitKnown||r.afterLimit==r.beforeLimit);return false;};
 r.status=io.setOffload(a);if(r.status)return rollback();
 if(enabled){r.status=io.setLimit(maximumIP);if(r.status)return rollback();}
 r.status=io.setSupported(s);if(r.status)return rollback();r.status=io.setEnabled(e);if(r.status)return rollback();r.status=read();if(r.status)return rollback();
 if(r.afterAssist!=a||r.afterSupported!=s||r.afterEnabled!=e||(enabled&&r.afterLimit!=maximumIP)){r.status=-2;return rollback();}r.configured=true;r.enabled=enabled;return true;
}
}
