// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <stdint.h>
namespace realtek_next {
// B-only aggregate evidence. Caller uses existing controller serialization.
// No allocation, hardware read, clock, queue call or retained packet pointer.
struct MAC46N64TXPrecheck {
 bool targeted=false;
 uint64_t earlyFull=0,snapshotAttempts=0,snapshotCopies=0,snapshotBytes=0,snapshotFailures=0;
 uint64_t finalFullCopies=0,finalFullBytes=0,retrySite[18]{},retryReason[6]{},retryFamily[3]{};
 void beginSnapshot(){targeted=true;++snapshotAttempts;}
 void snapshotResult(bool copied,uint64_t bytes,bool ok){
  if(copied){++snapshotCopies;snapshotBytes+=bytes;}if(!ok)++snapshotFailures;
 }
 void finalFull(bool copied,uint64_t bytes){if(copied){++finalFullCopies;finalFullBytes+=bytes;}}
 void retry(unsigned site,unsigned family,unsigned reason){
  targeted=true;if(site>=1&&site<=17)++retrySite[site];
  ++retryReason[reason<6?reason:0];++retryFamily[family==4?1:family==6?2:0];
 }
 template<class Sink>void write(Sink &s,bool eligible,bool active,unsigned epoch){
  if(eligible)targeted=true;if(!targeted)return;
  s.setProperty("MAC46N64TXPrecheckSchema",uint64_t(1),32);
  s.setProperty("MAC46N64TXPrecheckEverEligible",targeted);
  s.setProperty("MAC46N64TXPrecheckEligible",eligible);
  s.setProperty("MAC46N64TXPrecheckActive",active);
  s.setProperty("MAC46N64TXPrecheckEpoch",uint64_t(epoch),64);
  s.setProperty("MAC46N64TXPrecheckCountersLifetime",true);
  s.setProperty("MAC46N64TXPrecheckEarlyFull",earlyFull,64);
  s.setProperty("MAC46N64TXPrecheckSnapshotAttempts",snapshotAttempts,64);
  s.setProperty("MAC46N64TXPrecheckSnapshotCopies",snapshotCopies,64);
  s.setProperty("MAC46N64TXPrecheckSnapshotBytes",snapshotBytes,64);
  s.setProperty("MAC46N64TXPrecheckSnapshotFailures",snapshotFailures,64);
  s.setProperty("MAC46N64TXPrecheckFinalFullCopies",finalFullCopies,64);
  s.setProperty("MAC46N64TXPrecheckFinalFullBytes",finalFullBytes,64);
  static const char *sites[]={"", "MAC46N64TXPrecheckRetrySite1","MAC46N64TXPrecheckRetrySite2","MAC46N64TXPrecheckRetrySite3",
   "MAC46N64TXPrecheckRetrySite4","MAC46N64TXPrecheckRetrySite5","MAC46N64TXPrecheckRetrySite6","MAC46N64TXPrecheckRetrySite7",
   "MAC46N64TXPrecheckRetrySite8","MAC46N64TXPrecheckRetrySite9","MAC46N64TXPrecheckRetrySite10","MAC46N64TXPrecheckRetrySite11",
   "MAC46N64TXPrecheckRetrySite12","MAC46N64TXPrecheckRetrySite13","MAC46N64TXPrecheckRetrySite14","MAC46N64TXPrecheckRetrySite15",
   "MAC46N64TXPrecheckRetrySite16","MAC46N64TXPrecheckRetrySite17"};
  static const char *reasons[]={"MAC46N64TXPrecheckRetryReason0","MAC46N64TXPrecheckRetryReason1","MAC46N64TXPrecheckRetryReason2",
   "MAC46N64TXPrecheckRetryReason3","MAC46N64TXPrecheckRetryReason4","MAC46N64TXPrecheckRetryReason5"};
  static const char *families[]={"MAC46N64TXPrecheckRetryFamily0","MAC46N64TXPrecheckRetryFamily4","MAC46N64TXPrecheckRetryFamily6"};
  for(unsigned i=1;i<=17;++i)s.setProperty(sites[i],retrySite[i],64);
  for(unsigned i=0;i<6;++i)s.setProperty(reasons[i],retryReason[i],64);
  for(unsigned i=0;i<3;++i)s.setProperty(families[i],retryFamily[i],64);
 }
};
}
