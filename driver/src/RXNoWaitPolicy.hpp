// SPDX-License-Identifier: GPL-2.0-only
#pragma once
namespace realtek_next {
// Publication may inspect the requested policy; use the same atomic discipline
// as the existing checksum policies. This is not a live preference switch.
class RXNoWaitPolicy {
 bool requested_=false;
public:
 void reset(){__atomic_store_n(&requested_,false,__ATOMIC_RELEASE);}
 void configure(bool initialized,unsigned mac,unsigned cfg,unsigned xid,bool option){
  __atomic_store_n(&requested_,initialized&&mac==42&&cfg==25&&xid==0x509&&option,__ATOMIC_RELEASE);
 }
 bool requested()const{return __atomic_load_n(&requested_,__ATOMIC_ACQUIRE);}
 bool active(bool ready,bool mapper)const{return requested()&&ready&&!mapper;}
};
}
