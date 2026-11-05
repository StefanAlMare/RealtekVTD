// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "NetworkProgram.hpp"
#include "FramePolicy.hpp"
namespace realtek_next {
// MAC34 software-TSO completion ownership. Every access uses the existing
// controller lock. No descriptors, payload bytes, DMA mappings or queue APIs are
// touched here. A retired parent's last descriptor owns one stable TailRecord;
// earlier child cookies stay non-null so the core cleans/counts every lease.
class MAC34TXCompletion {
public:
 struct TailRecord {void *original=nullptr;uint64_t epoch=0,diagID=0;unsigned family=0;bool cancelled=false;};
 enum TransferResult:unsigned {Success=0,Fallback=1,InvariantError=2};
 enum TakeResult:unsigned {Foreign=0,Child=1,Tail=2,Invalid=3};
 enum Reason:unsigned {Disabled=0,NotReady,Shape,Parent,Count,Buffer,NotTail,SlotBusy,Duplicate,Latched,ReasonCount};
 struct Counters {
  uint64_t attempts=0,transferred[2]{},completed[2]{},cancelled[2]{},childReclaims=0;
  uint64_t live=0,maximumLive=0,invalidCookies=0,invariantErrors=0,fallback[ReasonCount]{};
 };
private:
 TailRecord tails[64];uint8_t childSentinel=0;bool on=false,failed=false;Counters counts;
 TransferResult reject(Reason reason,bool invariant=false){++counts.fallback[reason];if(invariant){++counts.invariantErrors;failed=true;return InvariantError;}return Fallback;}
 bool recordAddress(void *p,unsigned &index,bool &inside)const{
  const uintptr_t a=reinterpret_cast<uintptr_t>(p),b=reinterpret_cast<uintptr_t>(&tails[0]);
  inside=a>=b&&a-b<sizeof(tails);if(!inside)return false;
  const uintptr_t offset=a-b;if(offset%sizeof(TailRecord))return false;index=unsigned(offset/sizeof(TailRecord));return index<64;
 }
 bool internal(void*p)const{unsigned i=0;bool inside=false;recordAddress(p,i,inside);return p==&childSentinel||inside;}
public:
 void configure(bool exact,bool wanted){on=exact&&wanted;} // Never clear in-flight ownership.
 bool enabled()const{return on;}
 bool failedInvariant()const{return failed;}
 const Counters &receiptCounters()const{return counts;}
 uint64_t liveCount()const{return counts.live;}
 void markCancelled(){for(auto &r:tails)if(r.original)r.cancelled=true;}
 // Caller additionally proves owned=true and remaining=false under this lock;
 // this class cannot inspect the private emitter state or infer either fact.
 template<class Owned>
 TransferResult transfer(NetworkResult &n,void *oldCookie,void *original,unsigned family,
                         uint64_t epoch,unsigned outstanding,uint64_t diagID,Owned owns){
  ++counts.attempts;
  if(failed)return reject(Latched,true);
  if(!on)return reject(Disabled);
  if(n.macVersion!=34||!n.mac34Native64Verified||!n.prepared||!n.running||
     n.status!=NetworkStatus::Ok||n.unsafeRetained||n.stopSafety.latched)return reject(NotReady);
  if(n.txCount!=64||!n.tx||n.consumer>=64||n.producer>=64||!n.used||n.used>64||
     (n.consumer+n.used)%64!=n.producer)return reject(Shape,true);
  if(!oldCookie||!original||oldCookie==original||(family!=4&&family!=6)||
     internal(oldCookie)||internal(original))return reject(Parent,true);
  if(!outstanding||outstanding>n.used)return reject(Count,true);
  for(auto&r:tails)if(r.original==original)return reject(Duplicate,true);
  unsigned matched=0,last=64;
  for(unsigned j=0;j<64;++j){const unsigned i=(n.consumer+j)%64;if(n.cookies[i]!=oldCookie)continue;
   if(j>=n.used)return reject(Count,true); // refuse any stale singleton reference outside the live window
   const auto &b=n.txPackets[i];
   if(!b.opaque||!b.allocated||!b.prepared||!b.mapped||b.segmentCount!=1||
      b.requestedLength<60||b.requestedLength>maxFrameBytes(n.mtu)||b.mappedLength!=b.requestedLength||
      !b.segments[0].address||b.segments[0].length!=b.requestedLength||
      b.segments[0].address>UINT64_MAX-(b.requestedLength-1)||!owns(b))return reject(Buffer,true);
   ++matched;last=i;
  }
  if(matched!=outstanding)return reject(Count,true);
  if(last!=(n.producer+63)%64)return reject(NotTail);
  if(tails[last].original)return reject(SlotBusy);
  if(counts.live>=64)return reject(Count,true);
  // Commit only after exhaustive preflight. OWN and all DMA data are unchanged.
  auto &tail=tails[last];tail.original=original;tail.epoch=epoch;tail.diagID=diagID;tail.family=family;tail.cancelled=false;
  for(unsigned j=0;j<n.used;++j){const unsigned i=(n.consumer+j)%64;if(n.cookies[i]==oldCookie)n.cookies[i]=&childSentinel;}
  n.cookies[last]=&tail;++counts.transferred[family==6];++counts.live;
  if(counts.live>counts.maximumLive)counts.maximumLive=counts.live;return Success;
 }
 TakeResult take(void *cookie,TailRecord &receipt){
  if(!cookie){++counts.invalidCookies;failed=true;return Invalid;}
  if(cookie==&childSentinel){++counts.childReclaims;return Child;}
  unsigned index=0;bool inside=false;
  if(!recordAddress(cookie,index,inside)){
   if(!inside)return Foreign;++counts.invalidCookies;failed=true;return Invalid;
  }
  auto &r=tails[index];
  if(!r.original||(r.family!=4&&r.family!=6)||!counts.live){++counts.invalidCookies;failed=true;return Invalid;}
  receipt=r;r={};--counts.live;if(receipt.cancelled)++counts.cancelled[receipt.family==6];else ++counts.completed[receipt.family==6];return Tail;
 }
};
}
