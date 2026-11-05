// SPDX-License-Identifier: GPL-2.0-only
#include "TXPool.hpp"
#include "FramePolicy.hpp"
namespace realtek_next {
namespace {
void discard(DMAProvider &d,DMABuffer &b){
 if(b.mapped)d.unmap(b);if(b.prepared)d.complete(b);if(b.allocated)d.release(b);
}
}
int TXPool::index(const DMABuffer &b) const {
#if defined(__x86_64__)
 // GU25 uses 257 active slots. Preserve the legacy 65-slot lookup below.
 // Compare integer representations before subtracting: an opaque handle may
 // belong to a native DMA object, not this array. Never dereference the handle.
 if(activeCount==kTXPoolMaxCount){
  const uintptr_t base=reinterpret_cast<uintptr_t>(&slots[0]);
  const uintptr_t address=reinterpret_cast<uintptr_t>(b.opaque);
  if(address<base)return -1;
  const uintptr_t offset=address-base;
  if(offset>=sizeof(slots)||offset%sizeof(Slot)!=0)return -1;
  return int(offset/sizeof(Slot));
 }
#endif
 for(unsigned i=0;i<activeCount;++i)if(b.opaque==&slots[i])return int(i);
 return -1;
}
unsigned TXPool::leased() const {
 unsigned count=0;for(unsigned i=0;i<activeCount;++i)count+=__atomic_load_n(&slots[i].busy,__ATOMIC_ACQUIRE)!=0;
 return count;
}
bool TXPool::initialize(DMAProvider &d,unsigned slotStride,unsigned slotCount,bool allowGU25Jumbo){
 const bool extendedShape=slotCount==kTXPoolMaxCount&&slotStride==kJumboTXStride;
 if(data||storage.opaque||!d.dmaReady()||(slotStride!=kTXPoolStride&&slotStride!=kJumboTXStride)||
    (slotCount!=kTXPoolCount&&slotCount!=kTXPoolMaxCount)||
    (allowGU25Jumbo&&!extendedShape)||
    (slotCount==kTXPoolMaxCount&&slotStride!=kTXPoolStride&&!allowGU25Jumbo))return false;
 stride=slotStride;activeCount=slotCount;const uint64_t size=uint64_t(activeCount)*stride;
 if(d.allocate(storage,size,kDescriptorRingAlignment,DMADirection::ToDevice)!=DMAStatus::Ok||
    d.prepare(storage)!=DMAStatus::Ok||d.map(storage)!=DMAStatus::Ok||
    storage.segmentCount!=1||storage.mappedLength!=size||storage.segments[0].length!=size||
    !storage.segments[0].address||storage.segments[0].address>UINT64_MAX-(size-1)||
    !(data=static_cast<uint8_t*>(d.bytes(storage)))){
  data=nullptr;discard(d,storage);return false;
 }
 return true;
}
bool TXPool::shutdown(DMAProvider &d){
 if(leased())return false;
 data=nullptr;discard(d,storage);return true;
}
DMAStatus TXPool::snapshot(DMABuffer &b,size_t length,TXCopy copy,void *context){
 if(!data||b.opaque||b.allocated||!copy||length<60||length>maxFrameBytes(stride==kJumboTXStride?kJumboMTU:1500))return DMAStatus::InvalidState;
#if defined(__x86_64__)
 // SCAN1: preserve IDX257 lookup and all lease/copy/release semantics.
 // For the 257-slot pool only, visit the same circular sequence without a
 // variable integer division at every probe. Keep exactly one cursor fetch.
 if(activeCount==kTXPoolMaxCount){
  const unsigned start=__atomic_fetch_add(&cursor,1U,__ATOMIC_RELAXED)%kTXPoolMaxCount;
  for(unsigned n=0,i=start;n<kTXPoolMaxCount;++n,i=(i+1==kTXPoolMaxCount?0:i+1)){
   // Observation only: it never grants ownership or orders payload access.
   // Even after reading zero, the existing strong acquire CAS must succeed.
   if(__atomic_load_n(&slots[i].busy,__ATOMIC_RELAXED)!=0)continue;
   unsigned expected=0;
   if(!__atomic_compare_exchange_n(&slots[i].busy,&expected,1U,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))continue;
   if(!copy(context,data+i*stride,length)||!frameLengthValid(data+i*stride,length,stride==kJumboTXStride?kJumboMTU:1500)){
    __atomic_store_n(&slots[i].busy,0U,__ATOMIC_RELEASE);return DMAStatus::InvalidState;
   }
   b.opaque=&slots[i];b.requestedLength=b.mappedLength=length;b.segmentCount=1;
   b.segments[0]={storage.segments[0].address+uint64_t(i)*stride,length};
   b.allocated=b.prepared=b.mapped=true;return DMAStatus::Ok;
  }
  return DMAStatus::PoolFull;
 }
#endif
 const unsigned start=__atomic_fetch_add(&cursor,1U,__ATOMIC_RELAXED)%activeCount;
 for(unsigned n=0;n<activeCount;++n){const unsigned i=(start+n)%activeCount;unsigned expected=0;
  if(!__atomic_compare_exchange_n(&slots[i].busy,&expected,1U,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))continue;
  if(!copy(context,data+i*stride,length)||!frameLengthValid(data+i*stride,length,stride==kJumboTXStride?kJumboMTU:1500)){
   __atomic_store_n(&slots[i].busy,0U,__ATOMIC_RELEASE);return DMAStatus::InvalidState;
  }
  b.opaque=&slots[i];b.requestedLength=b.mappedLength=length;b.segmentCount=1;
  b.segments[0]={storage.segments[0].address+uint64_t(i)*stride,length};
  b.allocated=b.prepared=b.mapped=true;return DMAStatus::Ok;
 }
 return DMAStatus::PoolFull;
}
void *TXPool::bytes(const DMABuffer &b){const int i=index(b);return i>=0&&data?data+unsigned(i)*stride:nullptr;}
DMAStatus TXPool::synchronize(DMAProvider &d){return data?d.syncForDevice(storage):DMAStatus::InvalidState;}
bool TXPool::release(DMABuffer &b){const int i=index(b);if(i<0)return false;
 b=DMABuffer{};__atomic_store_n(&slots[i].busy,0U,__ATOMIC_RELEASE);return true;
}
}
