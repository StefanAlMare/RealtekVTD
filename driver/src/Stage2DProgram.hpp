// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "Stage2CProgram.hpp"

namespace realtek_next {

constexpr uint32_t kStage2DRXDescriptorCount = 4;
constexpr uint32_t kStage2DRXBufferLength = 2048;
constexpr uint32_t kStage2DRXPollAttempts = 5000;
constexpr uint32_t kStage2DRXPollIntervalUs = 1000;

enum class Stage2DStatus : uint32_t {
    Ok = 0,
    Stage2CIncomplete,
    RingRegisterIOFailure,
    RingRegisterMismatch,
    BufferAllocationFailed,
    BufferPrepareFailed,
    BufferMapFailed,
    BufferSegmentInvalid,
    BufferAccessFailed,
    BufferSyncFailed,
    DescriptorAccessFailed,
    DescriptorSyncFailed,
    RXConfigurationFailed,
    RXEnableFailed,
    PollSyncFailed,
    RXDescriptorInvalid,
    RXBufferSyncFailed,
    RXTimeout,
    RXDisableFailed,
    SafeQuiesceFailed,
    DescriptorCleanupFailed,
};

struct Stage2DResult {
    Stage2DStatus status = Stage2DStatus::Stage2CIncomplete;
    const char *stage = "not-started";
    bool complete = false;
    bool packetDMAExercised = false;
    bool timeout = false;
    bool unsafeMappingRetained = false;
    bool trafficEnabled = false;
    uint32_t targetCount = kStage2DRXDescriptorCount;
    uint32_t completedCount = 0;
    uint32_t ownershipPublishedMask = 0;
    uint32_t ownershipClearedMask = 0;
    uint32_t descriptorValidMask = 0;
    uint32_t pollCount = 0;
    uint32_t rxConfig = 0;
    uint32_t rxConfigMaskedObserved = 0;
    uint8_t chipCommandBeforeRX = 0xff;
    uint8_t chipCommandDuringRX = 0xff;
    uint8_t chipCommandAfterRX = 0xff;
    uint64_t rxRingRegisterIOVA = 0;
    DMABuffer rxBuffers[kStage2DRXDescriptorCount];
    uint64_t bufferIOVA[kStage2DRXDescriptorCount]{};
    uint32_t bufferSegmentCount[kStage2DRXDescriptorCount]{};
    uint32_t descriptorOpts1BeforeOwn[kStage2DRXDescriptorCount]{};
    uint32_t descriptorOpts1After[kStage2DRXDescriptorCount]{};
    uint32_t descriptorOpts2After[kStage2DRXDescriptorCount]{};
    uint32_t receivedLength[kStage2DRXDescriptorCount]{};
    uint32_t descriptorErrorBits[kStage2DRXDescriptorCount]{};
};

Stage2DResult runStage2D(RegisterIO &io, DMAProvider &dma,
                         Stage2AResult &stage2a,
                         const Stage2CResult &stage2c);
bool teardownStage2D(RegisterIO &io, DMAProvider &dma, Stage2AResult &stage2a,
                     Stage2DResult &result);

}
