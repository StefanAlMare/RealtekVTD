// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "RegisterAccess.hpp"

namespace realtek_next {

enum class DMADirection : uint8_t { ToDevice, FromDevice, Bidirectional };

enum class DMAStatus : uint32_t {
    Ok = 0,
    InvalidState,
    MapperMissing,
    AllocationFailed,
    PrepareFailed,
    MapFailed,
    ZeroFailed,
    SyncFailed,
    SegmentCountInvalid,
    AlignmentInvalid,
    RangeOverflow,
    RegisterIOFailure,
    RegisterVerificationFailed,
    BusMasterFailed,
    PoolFull,
};

struct DMASegment {
    uint64_t address = 0;
    uint64_t length = 0;
};

struct DMABuffer {
    void *opaque = nullptr;
    uint64_t requestedLength = 0;
    uint64_t mappedLength = 0;
    DMASegment segments[4]{};
    size_t segmentCount = 0;
    bool allocated = false;
    bool prepared = false;
    bool mapped = false;
};

class DMAProvider {
public:
    virtual ~DMAProvider() = default;
    virtual bool deviceMapperPresent() const = 0;
    // Explicit capability: absence of a device mapper alone is not sufficient.
    virtual bool dmaReady() const { return deviceMapperPresent(); }
    virtual DMAStatus allocate(DMABuffer &, uint64_t length, uint32_t alignment,
                               DMADirection direction) = 0;
    virtual DMAStatus prepare(DMABuffer &) = 0;
    virtual DMAStatus map(DMABuffer &) = 0;
    virtual DMAStatus zero(DMABuffer &) = 0;
    virtual void *bytes(DMABuffer &) = 0;
    virtual DMAStatus syncForDevice(DMABuffer &) = 0;
    virtual DMAStatus syncForCPU(DMABuffer &) = 0;
    virtual void memoryBarrier() = 0;
    virtual void unmap(DMABuffer &) = 0;
    virtual void complete(DMABuffer &) = 0;
    virtual void release(DMABuffer &) = 0;
    virtual DMAStatus setBusMaster(bool enable) = 0;
};

struct Stage2AResult {
    DMAStatus status = DMAStatus::InvalidState;
    const char *stage = "not-started";
    bool mapperPresent = false;
    bool busMasterEnabled = false;
    bool complete = false;
    bool unsafeMappingRetained = false;
    bool stopSafetyRetained = false; // STOPSAFE1: sticky; BME alone cannot clear it.
    unsigned txDescriptorCount = 64; // Active count; RX allocation remains unchanged.
    DMABuffer tx;
    DMABuffer rx;
};

constexpr uint32_t kDescriptorSize = 16;
constexpr uint32_t kTXDescriptorCount = 64;
constexpr uint32_t kTXDescriptorMaxCount = 256;
constexpr bool txDescriptorCountValid(unsigned n){return n==kTXDescriptorCount||n==kTXDescriptorMaxCount;}
constexpr uint32_t kRXDescriptorCount = 256;
constexpr uint32_t kDescriptorRingAlignment = 256;
constexpr uint32_t kTXRingLength = kDescriptorSize * kTXDescriptorCount;
constexpr uint32_t kRXRingLength = kDescriptorSize * kRXDescriptorCount;

Stage2AResult runStage2A(RegisterIO &io, DMAProvider &dma, unsigned txCount=kTXDescriptorCount);
void teardownStage2A(RegisterIO &io, DMAProvider &dma, Stage2AResult &result);

}
