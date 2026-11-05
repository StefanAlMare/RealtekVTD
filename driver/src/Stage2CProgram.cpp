// SPDX-License-Identifier: GPL-2.0-only
#include "Stage2CProgram.hpp"

namespace realtek_next {
namespace {
constexpr uint16_t kChipCommand = 0x37;
constexpr uint16_t kInterruptMask = 0x3c;
constexpr uint16_t kRXConfig = 0x44;
constexpr uint16_t kMulticastAddress0 = 0x08;
constexpr uint16_t kMulticastAddress4 = 0x0c;
constexpr uint16_t kRXMaxSize = 0xda;
constexpr uint16_t kInterruptMitigate = 0xe2;
constexpr uint16_t kRXAddressLow = 0xe4;
constexpr uint16_t kRXAddressHigh = 0xe8;
constexpr uint8_t kCmdTXEnable = 0x04;
constexpr uint8_t kCmdRXEnable = 0x08;
constexpr uint8_t kTXRXEnable = kCmdTXEnable | kCmdRXEnable;
constexpr uint32_t kDescOwn = 1U << 31;
constexpr uint32_t kRingEnd = 1U << 30;
constexpr uint32_t kFirstFrag = 1U << 29;
constexpr uint32_t kLastFrag = 1U << 28;
constexpr uint32_t kRXLengthMask = 0x3fff;
constexpr uint32_t kRXErrorMask = (1U << 22) | (1U << 21) |
                                  (1U << 20) | (1U << 19);
constexpr uint32_t kRXDMABurst = 7U << 8;
constexpr uint32_t kRX128InterruptEnable = 1U << 15;
constexpr uint32_t kRXMultiEnable = 1U << 14;
constexpr uint32_t kRXEarlyOff = 1U << 11;
constexpr uint32_t kAcceptBroadcast = 0x08;
constexpr uint32_t kAcceptMulticast = 0x04;
constexpr uint32_t kAcceptMyPhysical = 0x02;
constexpr uint32_t kRXConfigAcceptMask = 0x3f;
constexpr uint32_t kRXConfigValue = kRX128InterruptEnable | kRXMultiEnable |
                                    kRXDMABurst | kRXEarlyOff |
                                    kAcceptBroadcast | kAcceptMulticast |
                                    kAcceptMyPhysical;
// RTL8168H may report hardware-managed FIFO-threshold bits that were not in
// the value written (Acer MAC_VER_46 reports bit 17).  Verify every field this
// stage actually controls, including the disabled accept-error modes, without
// rejecting unrelated read-only/hardware-managed bits.
constexpr uint32_t kRXConfigVerifyMask = kRX128InterruptEnable | kRXMultiEnable |
                                         kRXDMABurst | kRXEarlyOff |
                                         kRXConfigAcceptMask;

uint64_t joinAddress(uint32_t high, uint32_t low) {
    return (uint64_t(high) << 32) | low;
}

void cleanupBuffer(DMAProvider &dma, DMABuffer &buffer) {
    if (buffer.mapped) dma.unmap(buffer);
    if (buffer.prepared) dma.complete(buffer);
    if (buffer.allocated || buffer.opaque) dma.release(buffer);
}

void clearDescriptor(volatile uint32_t *descriptor) {
    descriptor[0] = 0;
    descriptor[1] = 0;
    descriptor[2] = 0;
    descriptor[3] = 0;
}

bool readAndVerifyRXRing(RegisterIO &io, const Stage2AResult &stage2a,
                         Stage2CResult &result) {
    if (!io.read32(kRXAddressLow, result.rxRingRegisterLow) ||
        !io.read32(kRXAddressHigh, result.rxRingRegisterHigh))
        return false;
    result.rxRingRegisterIOVA = joinAddress(result.rxRingRegisterHigh,
                                            result.rxRingRegisterLow);
    return stage2a.rx.segmentCount == 1 &&
           result.rxRingRegisterIOVA == stage2a.rx.segments[0].address;
}

bool configureOneShotRX(RegisterIO &io, Stage2CResult &result) {
    auto access = [&](const char *step, uint16_t offset, unsigned width,
                      uint32_t expected, bool read,
                      uint32_t verifyMask = UINT32_MAX) -> bool {
        result.rxConfigStep = step;
        result.rxConfigOperation = read ? "read-verify" : "write";
        result.rxConfigOffset = offset;
        result.rxConfigWidth = width;
        result.rxConfigExpected = expected;
        result.rxConfigObserved = 0;
        result.rxConfigObservedValid = false;
        bool ok;
        if (read) {
            uint32_t value = 0;
            if (width == 16) {
                uint16_t shortValue = 0;
                ok = io.read16(offset, shortValue); value = shortValue;
            } else ok = io.read32(offset, value);
            result.rxConfigObserved = value;
            result.rxConfigObservedValid = ok;
            if (ok && offset == kRXMaxSize) result.rxMaxSize = uint16_t(value);
            if (ok && offset == kRXConfig) {
                result.rxConfig = value;
                result.rxConfigVerifyMask = verifyMask;
                result.rxConfigMaskedExpected = expected & verifyMask;
                result.rxConfigMaskedObserved = value & verifyMask;
                result.rxConfigHardwareManagedBits = value & ~verifyMask;
            }
        } else {
            ok = width == 16 ? io.write16(offset, uint16_t(expected))
                             : io.write32(offset, expected);
        }
        result.rxConfigAccessSucceeded = ok;
        return ok && (!read ||
                      (result.rxConfigObserved & verifyMask) ==
                          (expected & verifyMask));
    };
    if (!access("interrupt-mask-write", kInterruptMask, 16, 0, false) ||
        !access("mitigation-write", kInterruptMitigate, 16, 0, false) ||
        !access("max-size-write", kRXMaxSize, 16, kStage2CRXBufferLength + 1, false) ||
        !access("multicast-high-write", kMulticastAddress4, 32, 0xffffffffU, false) ||
        !access("multicast-low-write", kMulticastAddress0, 32, 0xffffffffU, false) ||
        !access("rx-config-write", kRXConfig, 32, kRXConfigValue, false) ||
        !access("mitigation-read", kInterruptMitigate, 16, 0, true) ||
        !access("max-size-read", kRXMaxSize, 16, kStage2CRXBufferLength + 1, true) ||
        !access("rx-config-read", kRXConfig, 32, kRXConfigValue, true,
                kRXConfigVerifyMask))
        return false;
    result.rxConfigStep = "complete";
    return true;
}

bool stopRX(RegisterIO &io, Stage2CResult &result) {
    uint8_t command = 0xff;
    const bool masked = io.write16(kInterruptMask, 0);
    const bool commandWritten = io.write8(kChipCommand, 0);
    const bool configCleared = io.write32(kRXConfig, 0);
    const bool multicastHighCleared = io.write32(kMulticastAddress4, 0);
    const bool multicastLowCleared = io.write32(kMulticastAddress0, 0);
    const bool commandRead = io.read8(kChipCommand, command);
    const bool stopped = masked && commandWritten && configCleared &&
                         multicastHighCleared && multicastLowCleared &&
                         commandRead && !(command & kTXRXEnable);
    result.chipCommandAfterRX = command;
    result.trafficEnabled = bool(command & kTXRXEnable);
    return stopped;
}

bool safeQuiesce(RegisterIO &io, DMAProvider &dma, Stage2AResult &stage2a,
                  Stage2CResult &result, bool requireBusMasterOff) {
    const bool stopped = stopRX(io, result);
    if (stopped && !requireBusMasterOff) return true;
    dma.memoryBarrier();
    const bool busMasterOff = dma.setBusMaster(false) == DMAStatus::Ok;
    if (busMasterOff) stage2a.busMasterEnabled = false;
    return busMasterOff;
}

Stage2CResult failBeforeOwn(Stage2CResult result, Stage2CStatus status,
                            const char *stage, DMAProvider &dma,
                            Stage2AResult &stage2a,
                            volatile uint32_t *descriptor) {
    result.status = status;
    result.stage = stage;
    if (descriptor) {
        clearDescriptor(descriptor);
        dma.memoryBarrier();
        dma.syncForDevice(stage2a.rx);
    }
    cleanupBuffer(dma, result.rxBuffer);
    return result;
}

Stage2CResult failAfterOwn(Stage2CResult result, Stage2CStatus status,
                           const char *stage, RegisterIO &io, DMAProvider &dma,
                           Stage2AResult &stage2a,
                           volatile uint32_t *descriptor,
                           bool ownershipStillPublished) {
    result.status = status;
    result.stage = stage;
    if (!safeQuiesce(io, dma, stage2a, result, ownershipStillPublished)) {
        result.status = Stage2CStatus::SafeQuiesceFailed;
        result.stage = "rx-safe-quiesce";
        result.unsafeMappingRetained = true;
        return result;
    }
    clearDescriptor(descriptor);
    dma.memoryBarrier();
    dma.syncForDevice(stage2a.rx);
    cleanupBuffer(dma, result.rxBuffer);
    return result;
}
}

Stage2CResult runStage2C(RegisterIO &io, DMAProvider &dma, Stage2AResult &stage2a,
                         const Stage2BResult &stage2b) {
    Stage2CResult result;
    if (!stage2a.complete || !stage2a.busMasterEnabled || !stage2a.rx.mapped ||
        !stage2b.complete || !stage2b.phyLink || !dma.dmaReady()) {
        result.stage = "stage2b-incomplete";
        return result;
    }

    result.stage = "rx-ring-register-readback";
    if (!readAndVerifyRXRing(io, stage2a, result)) {
        const bool readCompleted = result.rxRingRegisterIOVA ||
                                   result.rxRingRegisterLow ||
                                   result.rxRingRegisterHigh;
        result.status = readCompleted ? Stage2CStatus::RingRegisterMismatch
                                      : Stage2CStatus::RingRegisterIOFailure;
        result.stage = "rx-ring-register-verify";
        return result;
    }

    result.rxBufferLength = kStage2CRXBufferLength;
    result.stage = "rx-buffer-allocate";
    auto dmaStatus = dma.allocate(result.rxBuffer, kStage2CRXBufferLength, 16,
                                  DMADirection::FromDevice);
    if (dmaStatus != DMAStatus::Ok)
        return failBeforeOwn(result, Stage2CStatus::BufferAllocationFailed,
                             "rx-buffer-allocate", dma, stage2a, nullptr);
    result.stage = "rx-buffer-prepare";
    dmaStatus = dma.prepare(result.rxBuffer);
    if (dmaStatus != DMAStatus::Ok)
        return failBeforeOwn(result, Stage2CStatus::BufferPrepareFailed,
                             "rx-buffer-prepare", dma, stage2a, nullptr);
    result.stage = "rx-buffer-map";
    dmaStatus = dma.map(result.rxBuffer);
    if (dmaStatus != DMAStatus::Ok)
        return failBeforeOwn(result, Stage2CStatus::BufferMapFailed,
                             "rx-buffer-map", dma, stage2a, nullptr);
    result.rxBufferSegmentCount = uint32_t(result.rxBuffer.segmentCount);
    if (result.rxBuffer.segmentCount != 1 ||
        result.rxBuffer.mappedLength != kStage2CRXBufferLength ||
        result.rxBuffer.segments[0].length != kStage2CRXBufferLength ||
        result.rxBuffer.segments[0].address == 0 ||
        result.rxBuffer.segments[0].address > UINT64_MAX - (kStage2CRXBufferLength - 1))
        return failBeforeOwn(result, Stage2CStatus::BufferSegmentInvalid,
                             "rx-buffer-segment", dma, stage2a, nullptr);
    result.rxBufferMapped = true;
    result.rxBufferIOVA = result.rxBuffer.segments[0].address;
    if (!dma.bytes(result.rxBuffer))
        return failBeforeOwn(result, Stage2CStatus::BufferAccessFailed,
                             "rx-buffer-access", dma, stage2a, nullptr);
    if (dma.zero(result.rxBuffer) != DMAStatus::Ok)
        return failBeforeOwn(result, Stage2CStatus::BufferSyncFailed,
                             "rx-buffer-sync", dma, stage2a, nullptr);
    // A FromDevice buffer is synchronized with kIODirectionIn only after DMA.
    // Before ownership, only CPU store ordering is required.
    dma.memoryBarrier();

    auto *ringBytes = static_cast<uint8_t *>(dma.bytes(stage2a.rx));
    if (!ringBytes)
        return failBeforeOwn(result, Stage2CStatus::DescriptorAccessFailed,
                             "rx-descriptor-access", dma, stage2a, nullptr);
    auto *descriptor = reinterpret_cast<volatile uint32_t *>(ringBytes);
    result.rxDesc0AddressLow = uint32_t(result.rxBufferIOVA);
    result.rxDesc0AddressHigh = uint32_t(result.rxBufferIOVA >> 32);
    result.rxDesc0Opts2BeforeOwn = 0;
    result.rxDesc0Opts1BeforeOwn = kRingEnd | kStage2CRXBufferLength;
    descriptor[2] = result.rxDesc0AddressLow;
    descriptor[3] = result.rxDesc0AddressHigh;
    descriptor[1] = result.rxDesc0Opts2BeforeOwn;
    descriptor[0] = result.rxDesc0Opts1BeforeOwn;
    dma.memoryBarrier();
    if (dma.syncForDevice(stage2a.rx) != DMAStatus::Ok)
        return failBeforeOwn(result, Stage2CStatus::DescriptorSyncFailed,
                             "rx-descriptor-sync", dma, stage2a, descriptor);
    if (joinAddress(descriptor[3], descriptor[2]) != result.rxBufferIOVA ||
        descriptor[0] != result.rxDesc0Opts1BeforeOwn)
        return failBeforeOwn(result, Stage2CStatus::DescriptorMismatch,
                             "rx-descriptor-verify", dma, stage2a, descriptor);

    result.stage = "rx-configure";
    if (!configureOneShotRX(io, result))
        return failBeforeOwn(result, Stage2CStatus::RXConfigurationFailed,
                             "rx-configure", dma, stage2a, descriptor);

    result.stage = "rx-own-publish";
    if (!io.read8(kChipCommand, result.chipCommandBeforeRX) ||
        (result.chipCommandBeforeRX & kTXRXEnable))
        return failBeforeOwn(result, Stage2CStatus::RXEnableFailed,
                             "rx-command-precheck", dma, stage2a, descriptor);
    descriptor[0] = result.rxDesc0Opts1BeforeOwn | kDescOwn;
    result.ownershipPublished = true;
    dma.memoryBarrier();
    if (dma.syncForDevice(stage2a.rx) != DMAStatus::Ok)
        return failAfterOwn(result, Stage2CStatus::DescriptorSyncFailed,
                            "rx-own-sync", io, dma, stage2a, descriptor, true);

    result.stage = "rx-enable";
    if (!io.write8(kChipCommand, kCmdRXEnable) ||
        !io.read8(kChipCommand, result.chipCommandDuringRX) ||
        result.chipCommandDuringRX != kCmdRXEnable)
        return failAfterOwn(result, Stage2CStatus::RXEnableFailed,
                            "rx-enable", io, dma, stage2a, descriptor, true);

    result.stage = "rx-poll";
    for (uint32_t attempt = 1; attempt <= kStage2CRXPollAttempts; ++attempt) {
        io.delayUs(kStage2CRXPollIntervalUs);
        if (dma.syncForCPU(stage2a.rx) != DMAStatus::Ok)
            return failAfterOwn(result, Stage2CStatus::PollSyncFailed,
                                "rx-poll-sync", io, dma, stage2a, descriptor, true);
        dma.memoryBarrier();
        result.rxDesc0Opts1After = descriptor[0];
        result.rxDesc0Opts2After = descriptor[1];
        result.pollCount = attempt;
        if (!(result.rxDesc0Opts1After & kDescOwn)) {
            result.ownershipCleared = true;
            result.rxCompleted = true;
            result.receivedLength = result.rxDesc0Opts1After & kRXLengthMask;
            result.descriptorErrorBits = result.rxDesc0Opts1After & kRXErrorMask;
            result.descriptorValid =
                (result.rxDesc0Opts1After & (kFirstFrag | kLastFrag)) ==
                    (kFirstFrag | kLastFrag) &&
                !result.descriptorErrorBits && result.receivedLength >= 4 &&
                result.receivedLength <= kStage2CRXBufferLength;
            if (!result.descriptorValid)
                return failAfterOwn(result, Stage2CStatus::RXDescriptorInvalid,
                                    "rx-descriptor-result", io, dma, stage2a,
                                    descriptor, false);
            if (dma.syncForCPU(result.rxBuffer) != DMAStatus::Ok)
                return failAfterOwn(result, Stage2CStatus::RXBufferSyncFailed,
                                    "rx-buffer-cpu-sync", io, dma, stage2a,
                                    descriptor, false);
            result.packetDMAExercised = true;
            break;
        }
    }
    if (!result.ownershipCleared) {
        result.timeout = true;
        return failAfterOwn(result, Stage2CStatus::RXTimeout,
                            "rx-timeout", io, dma, stage2a, descriptor, true);
    }

    result.stage = "rx-disable";
    if (!stopRX(io, result))
        return failAfterOwn(result, Stage2CStatus::RXDisableFailed,
                            "rx-disable", io, dma, stage2a, descriptor, false);
    clearDescriptor(descriptor);
    dma.memoryBarrier();
    if (dma.syncForDevice(stage2a.rx) != DMAStatus::Ok) {
        cleanupBuffer(dma, result.rxBuffer);
        result.status = Stage2CStatus::DescriptorCleanupFailed;
        result.stage = "rx-descriptor-cleanup";
        return result;
    }
    cleanupBuffer(dma, result.rxBuffer);
    result.status = Stage2CStatus::Ok;
    result.stage = "stage2c-complete";
    result.complete = true;
    return result;
}

bool teardownStage2C(RegisterIO &io, DMAProvider &dma, Stage2AResult &stage2a,
                     Stage2CResult &result) {
    if (!result.rxBuffer.opaque && !result.rxBuffer.allocated) return true;
    if (!safeQuiesce(io, dma, stage2a, result, result.ownershipPublished &&
                                               !result.ownershipCleared)) {
        result.unsafeMappingRetained = true;
        return false;
    }
    auto *bytes = static_cast<uint8_t *>(dma.bytes(stage2a.rx));
    if (bytes) {
        clearDescriptor(reinterpret_cast<volatile uint32_t *>(bytes));
        dma.memoryBarrier();
        dma.syncForDevice(stage2a.rx);
    }
    cleanupBuffer(dma, result.rxBuffer);
    result.unsafeMappingRetained = false;
    return true;
}

}
