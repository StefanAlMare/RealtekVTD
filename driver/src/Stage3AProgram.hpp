// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "Stage2DProgram.hpp"

namespace realtek_next {
constexpr uint32_t kStage3ATXRingCount = 8;
constexpr uint32_t kStage3ATXTarget = 32;
constexpr uint32_t kStage3ATXFrameLength = 60;

constexpr uint32_t kStage3ARXDescriptorCount = 4;
constexpr uint32_t kStage3ARXCycles = 16;
constexpr uint32_t kStage3ARXTargetCount =
    kStage3ARXDescriptorCount * kStage3ARXCycles;
constexpr uint32_t kStage3ARXBufferLength = 2048;
constexpr uint32_t kStage3ARXPollAttempts = 30000;
constexpr uint32_t kStage3ARXPollIntervalUs = 1000;

enum class Stage3AStatus : uint32_t {
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
    TXPrepareFailed,
    TXSyncFailed,
    TXKickFailed,
    TXTimeout,
    TXDescriptorError,
    LinkReadFailed,
    LinkDown,
    InterruptFailure,
};

struct Stage3AResult {
    Stage3AStatus status = Stage3AStatus::Stage2DIncomplete;
    const char *stage = "not-started";
    bool complete = false;
    bool running = false;
    uint32_t interruptActions = 0;
    uint16_t interruptCauses = 0;
    volatile uint32_t *rxRing = nullptr;
    bool packetDMAExercised = false;
    bool timeout = false;
    bool unsafeMappingRetained = false;
    bool trafficEnabled = false;
    uint32_t targetCount = kStage3ARXTargetCount;
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
    uint32_t lastObservedOpts1[kStage3ARXDescriptorCount]{};
    uint64_t lastObservedAddress[kStage3ARXDescriptorCount]{};
    uint32_t rxConfig = 0;
    uint32_t rxConfigMaskedObserved = 0;
    uint8_t chipCommandBeforeRX = 0xff;
    uint8_t chipCommandDuringRX = 0xff;
    uint8_t chipCommandAfterRX = 0xff;
    uint64_t rxRingRegisterIOVA = 0;
    uint32_t linkPollCount = 0;
    DMABuffer txBuffer;
    uint64_t txBufferIOVA = 0;
    uint32_t txRearmed = 0;
    uint32_t txSubmissionCount[kStage3ATXRingCount]{};
    uint32_t txDescriptorCompletions[kStage3ATXRingCount]{};
    uint32_t txSequence[kStage3ATXRingCount]{};
    bool txPending[kStage3ATXRingCount]{};
    uint32_t txSubmitted = 0;
    uint32_t txCompleted = 0;
    uint32_t txCompletionMask = 0;
    uint32_t txErrorBits = 0;
    bool txMayOwn = false;
    bool combinedEngineObserved = false;
    volatile uint32_t *txRing = nullptr;
    DMABuffer rxBuffers[kStage3ARXDescriptorCount];
    uint64_t bufferIOVA[kStage3ARXDescriptorCount]{};
    uint32_t bufferSegmentCount[kStage3ARXDescriptorCount]{};
    uint32_t completionCount[kStage3ARXDescriptorCount]{};
    uint32_t rearmCount[kStage3ARXDescriptorCount]{};
    uint32_t firstReceivedLength[kStage3ARXDescriptorCount]{};
    uint32_t secondReceivedLength[kStage3ARXDescriptorCount]{};
    uint32_t lastDescriptorOpts1[kStage3ARXDescriptorCount]{};
    uint32_t lastDescriptorErrorBits[kStage3ARXDescriptorCount]{};
};

Stage3AResult beginStage3A(RegisterIO &, DMAProvider &, Stage2AResult &, PCIConfig * = nullptr);
void processStage3AInterrupt(RegisterIO &, DMAProvider &, Stage2AResult &, Stage3AResult &, uint16_t, PCIConfig * = nullptr);
void scheduleStage3ATX(RegisterIO &, DMAProvider &, Stage2AResult &, Stage3AResult &);
void abortStage3A(RegisterIO &, DMAProvider &, Stage2AResult &, Stage3AResult &);
bool teardownStage3A(RegisterIO &, DMAProvider &, Stage2AResult &, Stage3AResult &);
}
