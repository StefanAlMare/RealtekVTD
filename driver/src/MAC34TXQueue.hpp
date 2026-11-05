// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <stdint.h>
namespace realtek_next {
// All access is serialized by the existing controller lock. No pending mbuf,
// deferred callout, queue pointer or new lifetime is retained here.
struct MAC34TXQueue {
 bool requested=true;
 uint64_t decisions=0,deferredParent=0,serviceAllowed=0;
 void configure(bool on){requested=on;}
 bool interruptService(bool active,bool exactNative64,bool parent4,bool parent6){
  if(!active)return false;
  if(!requested||!exactNative64)return true;
  ++decisions;
  // The next output is unconditionally rejected while either SW parent exists.
  // IRQ reclaim/pump still runs first. The final child reclaim removes the
  // parent before this decision and MUST retain the existing service call.
  if(parent4||parent6){++deferredParent;return false;}
  ++serviceAllowed;return true;
 }
};
}
