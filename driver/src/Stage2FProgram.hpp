// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "Stage2DProgram.hpp"

namespace realtek_next {
constexpr uint32_t kStage2FTXTarget = 8;
constexpr uint32_t kStage2FTXFrameLength = 60;

constexpr uint32_t kStage2FRXDescriptorCount = 4;
constexpr uint32_t kStage2FRXCycles = 4;
constexpr uint32_t kStage2FRXTargetCount =
    kStage2FRXDescriptorCount * kStage2FRXCycles;
constexpr uint32_t kStage2FRXBufferLength = 2048;
constexpr uint32_t kStage2FRXPollAttempts = 30000;
constexpr uint32_t kStage2FRXPollIntervalUs = 1000;

enum class Stage2FStatus : uint32_t {
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
};

struct Stage2FResult {
    Stage2FStatus status = Stage2FStatus::Stage2DIncomplete;
    const char *stage = "not-started";
    bool complete = false;
    bool packetDMAExercised = false;
    bool timeout = false;
    bool unsafeMappingRetained = false;
    bool trafficEnabled = false;
    uint32_t targetCount = kStage2FRXTargetCount;
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
    uint32_t lastObservedOpts1[kStage2FRXDescriptorCount]{};
    uint64_t lastObservedAddress[kStage2FRXDescriptorCount]{};
    uint32_t rxConfig = 0;
    uint32_t rxConfigMaskedObserved = 0;
    uint8_t chipCommandBeforeRX = 0xff;
    uint8_t chipCommandDuringRX = 0xff;
    uint8_t chipCommandAfterRX = 0xff;
    uint64_t rxRingRegisterIOVA = 0;
    uint32_t linkPollCount = 0;
    DMABuffer txBuffer;
    uint64_t txBufferIOVA = 0;
    uint32_t txSubmitted = 0;
    uint32_t txCompleted = 0;
    uint32_t txCompletionMask = 0;
    uint32_t txErrorBits = 0;
    bool txMayOwn = false;
    bool combinedEngineObserved = false;
    volatile uint32_t *txRing = nullptr;
    DMABuffer rxBuffers[kStage2FRXDescriptorCount];
    uint64_t bufferIOVA[kStage2FRXDescriptorCount]{};
    uint32_t bufferSegmentCount[kStage2FRXDescriptorCount]{};
    uint32_t completionCount[kStage2FRXDescriptorCount]{};
    uint32_t rearmCount[kStage2FRXDescriptorCount]{};
    uint32_t firstReceivedLength[kStage2FRXDescriptorCount]{};
    uint32_t secondReceivedLength[kStage2FRXDescriptorCount]{};
    uint32_t lastDescriptorOpts1[kStage2FRXDescriptorCount]{};
    uint32_t lastDescriptorErrorBits[kStage2FRXDescriptorCount]{};
};

Stage2FResult runStage2F(RegisterIO &io, DMAProvider &dma,
                         Stage2AResult &stage2a,
                         const Stage2DResult &stage2d, PCIConfig *pci = nullptr);
// Direct from C: the first burst and its recycling share one RX enable session.
Stage2FResult runStage2FFromC(RegisterIO &io, DMAProvider &dma,
                            Stage2AResult &stage2a,
                            const Stage2CResult &stage2c, PCIConfig *pci = nullptr);
Stage2FResult runStage2FFromA(RegisterIO &io, DMAProvider &dma,
                            Stage2AResult &stage2a, PCIConfig *pci = nullptr);
bool teardownStage2F(RegisterIO &io, DMAProvider &dma, Stage2AResult &stage2a,
                     Stage2FResult &result);

}
