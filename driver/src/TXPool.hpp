// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "TXSnapshot.hpp"
#include "JumboProfile.hpp"
namespace realtek_next {
// Active ring capacity plus one snapshot waiting to submit. Legacy default remains 65.
constexpr unsigned kTXPoolCount=65,kTXPoolMaxCount=257;
static_assert(uint64_t(kTXPoolMaxCount)*2048<=uint64_t(kTXPoolCount)*kJumboTXStride,"GU25 pool fits unchanged native allocation bound");
constexpr unsigned kTXPoolStride=2048;
class TXPool {
 struct Slot { unsigned busy=0; } slots[kTXPoolMaxCount];
 DMABuffer storage;uint8_t *data=nullptr;
 unsigned cursor=0,stride=kTXPoolStride,activeCount=kTXPoolCount;
 int index(const DMABuffer &) const;
public:
 // The fourth argument is an explicit prepared-resource permission, not a runtime toggle.
 bool initialize(DMAProvider &,unsigned slotStride=kTXPoolStride,unsigned slotCount=kTXPoolCount,
                 bool allowGU25Jumbo=false);
 bool shutdown(DMAProvider &); // Caller must have verified BME-off and stopped output.
 bool owns(const DMABuffer &b) const {return index(b)>=0;}
 bool ready() const {return data!=nullptr;}
 unsigned leased() const;
 unsigned count() const {return activeCount;}
 DMAStatus snapshot(DMABuffer &,size_t,TXCopy,void *);
 void *bytes(const DMABuffer &);
 DMAStatus synchronize(DMAProvider &);
 bool release(DMABuffer &);
 uint64_t iova() const {return storage.segmentCount?storage.segments[0].address:0;}
 unsigned slotStride()const{return stride;} // Diagnostic accessor; no state change.
};
}
