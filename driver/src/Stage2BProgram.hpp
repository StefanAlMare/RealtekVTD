// SPDX-License-Identifier: GPL-2.0-only
// Descriptor and doorbell semantics: Linux r8169 commit
// 3cdeaef1754ab8155cdd828c958ada3d36e9ad9a.
#pragma once
#include "DMAProgram.hpp"

namespace realtek_next {

enum class Stage2BStatus : uint32_t {
    Ok = 0,
    Stage2AIncomplete,
    RingRegisterIOFailure,
    RingRegisterMismatch,
    LinkReadFailure,
    LinkDown,
    TXConfigurationFailed,
    SourceMACInvalid,
    BufferAllocationFailed,
    BufferPrepareFailed,
    BufferMapFailed,
    BufferSegmentInvalid,
    BufferAccessFailed,
    BufferSyncFailed,
    DescriptorAccessFailed,
    DescriptorMismatch,
    DescriptorSyncFailed,
    TXEnableFailed,
    TXKickFailed,
    PollSyncFailed,
    TXTimeout,
    TXDisableFailed,
    SafeQuiesceFailed,
    DescriptorCleanupFailed,
};

struct Stage2BResult {
    Stage2BStatus status = Stage2BStatus::Stage2AIncomplete;
    const char *stage = "not-started";
    bool complete = false;
    bool packetDMAExercised = false;

    uint32_t txRingRegisterLow = 0;
    uint32_t txRingRegisterHigh = 0;
    uint32_t rxRingRegisterLow = 0;
    uint32_t rxRingRegisterHigh = 0;
    uint64_t txRingRegisterIOVA = 0;
    uint64_t rxRingRegisterIOVA = 0;

    uint16_t phyBMSRFirst = 0;
    uint16_t phyBMSRSecond = 0;
    bool phyLink = false;
    uint32_t linkPollCount = 0;
    uint64_t linkWaitUs = 0;
    bool linkTimedOut = false;
    bool txConfigured = false;

    bool txBufferMapped = false;
    uint64_t txBufferIOVA = 0;
    uint32_t txBufferSegmentCount = 0;
    uint32_t txBufferLength = 0;
    DMABuffer txBuffer;

    uint32_t txDesc0AddressHigh = 0;
    uint32_t txDesc0AddressLow = 0;
    uint32_t txDesc0Opts1BeforeOwn = 0;
    uint32_t txDesc0Opts1After = 0;
    uint32_t txDesc0Opts2BeforeOwn = 0;
    uint32_t txDesc0Opts2After = 0;

    bool ownershipPublished = false;
    bool ownershipCleared = false;
    uint32_t pollCount = 0;
    bool timeout = false;
    bool txCompleted = false;
    bool unsafeMappingRetained = false;

    uint8_t chipCommandBeforeTX = 0xff;
    uint8_t chipCommandDuringTX = 0xff;
    uint8_t chipCommandAfterTX = 0xff;
    bool trafficEnabled = false;
};

constexpr uint32_t kStage2BFrameLength = 60;
constexpr uint32_t kStage2BLinkPollAttempts = 60;
constexpr uint32_t kStage2BLinkPollIntervalUs = 100000;
constexpr uint32_t kStage2BPollAttempts = 1000;
constexpr uint32_t kStage2BPollIntervalUs = 100;

Stage2BResult runStage2B(RegisterIO &io, DMAProvider &dma, Stage2AResult &stage2a);
bool teardownStage2B(RegisterIO &io, DMAProvider &dma, Stage2AResult &stage2a,
                     Stage2BResult &result);

}
