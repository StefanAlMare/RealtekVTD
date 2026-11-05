// SPDX-License-Identifier: GPL-2.0-only
// RX descriptor semantics follow Linux r8169 commit
// 3cdeaef1754ab8155cdd828c958ada3d36e9ad9a.
#pragma once
#include "Stage2BProgram.hpp"

namespace realtek_next {

enum class Stage2CStatus : uint32_t {
    Ok = 0,
    Stage2BIncomplete,
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
    DescriptorMismatch,
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

struct Stage2CResult {
    Stage2CStatus status = Stage2CStatus::Stage2BIncomplete;
    const char *stage = "not-started";
    bool complete = false;
    bool packetDMAExercised = false;

    uint32_t rxRingRegisterLow = 0;
    uint32_t rxRingRegisterHigh = 0;
    uint64_t rxRingRegisterIOVA = 0;

    bool rxBufferMapped = false;
    uint64_t rxBufferIOVA = 0;
    uint32_t rxBufferSegmentCount = 0;
    uint32_t rxBufferLength = 0;
    DMABuffer rxBuffer;

    uint32_t rxDesc0AddressHigh = 0;
    uint32_t rxDesc0AddressLow = 0;
    uint32_t rxDesc0Opts1BeforeOwn = 0;
    uint32_t rxDesc0Opts1After = 0;
    uint32_t rxDesc0Opts2BeforeOwn = 0;
    uint32_t rxDesc0Opts2After = 0;

    bool ownershipPublished = false;
    bool ownershipCleared = false;
    uint32_t pollCount = 0;
    bool timeout = false;
    bool rxCompleted = false;
    bool descriptorValid = false;
    uint32_t receivedLength = 0;
    uint32_t descriptorErrorBits = 0;
    bool unsafeMappingRetained = false;

    uint32_t rxConfig = 0;
    const char *rxConfigStep = "not-started";
    const char *rxConfigOperation = "none";
    uint32_t rxConfigOffset = 0;
    uint32_t rxConfigWidth = 0;
    uint32_t rxConfigExpected = 0;
    uint32_t rxConfigObserved = 0;
    uint32_t rxConfigVerifyMask = 0;
    uint32_t rxConfigMaskedExpected = 0;
    uint32_t rxConfigMaskedObserved = 0;
    uint32_t rxConfigHardwareManagedBits = 0;
    bool rxConfigAccessSucceeded = false;
    bool rxConfigObservedValid = false;
    uint16_t rxMaxSize = 0;
    uint8_t chipCommandBeforeRX = 0xff;
    uint8_t chipCommandDuringRX = 0xff;
    uint8_t chipCommandAfterRX = 0xff;
    bool trafficEnabled = false;
};

constexpr uint32_t kStage2CRXBufferLength = 16383;
constexpr uint32_t kStage2CRXPollAttempts = 5000;
constexpr uint32_t kStage2CRXPollIntervalUs = 1000;

Stage2CResult runStage2C(RegisterIO &io, DMAProvider &dma, Stage2AResult &stage2a,
                         const Stage2BResult &stage2b);
bool teardownStage2C(RegisterIO &io, DMAProvider &dma, Stage2AResult &stage2a,
                     Stage2CResult &result);

}
