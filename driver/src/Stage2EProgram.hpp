// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "Stage2DProgram.hpp"

namespace realtek_next {

constexpr uint32_t kStage2ERXDescriptorCount = 4;
constexpr uint32_t kStage2ERXCycles = 2;
constexpr uint32_t kStage2ERXTargetCount =
    kStage2ERXDescriptorCount * kStage2ERXCycles;
constexpr uint32_t kStage2ERXBufferLength = 2048;
constexpr uint32_t kStage2ERXPollAttempts = 30000;
constexpr uint32_t kStage2ERXPollIntervalUs = 1000;

enum class Stage2EStatus : uint32_t {
    Ok = 0,
    Stage2DIncomplete,
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
    RXRearmFailed,
    RXTimeout,
    RXDisableFailed,
    SafeQuiesceFailed,
    DescriptorCleanupFailed,
    Stage2CIncomplete,
};

struct Stage2EResult {
    Stage2EStatus status = Stage2EStatus::Stage2DIncomplete;
    const char *stage = "not-started";
    bool complete = false;
    bool packetDMAExercised = false;
    bool timeout = false;
    bool unsafeMappingRetained = false;
    bool trafficEnabled = false;
    uint32_t targetCount = kStage2ERXTargetCount;
    uint32_t completedCount = 0;
    uint32_t rearmedCount = 0;
    uint32_t firstCycleMask = 0;
    uint32_t secondCycleMask = 0;
    uint32_t descriptorValidMask = 0;
    uint32_t pollCount = 0;
    uint32_t diagnosticSamples = 0;
    uint32_t diagnosticReadFailures = 0;
    uint32_t rxCommandLostCount = 0;
    uint32_t busMasterLostCount = 0;
    bool pciCommandObserved = false;
    uint16_t pciCommandBeforeRX = 0;
    uint16_t pciCommandLast = 0;
    uint8_t chipCommandAtStart = 0xff;
    uint8_t chipCommandLast = 0xff;
    uint8_t phyStatusAtStart = 0xff;
    uint8_t phyStatusLast = 0xff;
    uint16_t interruptStatusLast = 0;
    uint32_t ownMaskLast = 0;
    uint32_t firstCompletionPoll = 0;
    uint32_t firstCycleCompletePoll = 0;
    uint32_t lastObservedOpts1[kStage2ERXDescriptorCount]{};
    uint64_t lastObservedAddress[kStage2ERXDescriptorCount]{};
    uint32_t rxConfig = 0;
    uint32_t rxConfigMaskedObserved = 0;
    uint8_t chipCommandBeforeRX = 0xff;
    uint8_t chipCommandDuringRX = 0xff;
    uint8_t chipCommandAfterRX = 0xff;
    uint64_t rxRingRegisterIOVA = 0;
    DMABuffer rxBuffers[kStage2ERXDescriptorCount];
    uint64_t bufferIOVA[kStage2ERXDescriptorCount]{};
    uint32_t bufferSegmentCount[kStage2ERXDescriptorCount]{};
    uint32_t completionCount[kStage2ERXDescriptorCount]{};
    uint32_t rearmCount[kStage2ERXDescriptorCount]{};
    uint32_t firstReceivedLength[kStage2ERXDescriptorCount]{};
    uint32_t secondReceivedLength[kStage2ERXDescriptorCount]{};
    uint32_t lastDescriptorOpts1[kStage2ERXDescriptorCount]{};
    uint32_t lastDescriptorErrorBits[kStage2ERXDescriptorCount]{};
};

Stage2EResult runStage2E(RegisterIO &io, DMAProvider &dma,
                         Stage2AResult &stage2a,
                         const Stage2DResult &stage2d, PCIConfig *pci = nullptr);
// Direct from C: the first burst and its recycling share one RX enable session.
Stage2EResult runStage2EFromC(RegisterIO &io, DMAProvider &dma,
                            Stage2AResult &stage2a,
                            const Stage2CResult &stage2c, PCIConfig *pci = nullptr);
bool teardownStage2E(RegisterIO &io, DMAProvider &dma, Stage2AResult &stage2a,
                     Stage2EResult &result);

}
