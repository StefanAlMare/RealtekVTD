// SPDX-License-Identifier: GPL-2.0-only
#include "Stage2DProgram.hpp"

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
constexpr uint8_t kCmdRXEnable = 0x08;
constexpr uint8_t kTXRXEnable = 0x0c;
constexpr uint32_t kDescOwn = 1U << 31;
constexpr uint32_t kRingEnd = 1U << 30;
constexpr uint32_t kFirstFrag = 1U << 29;
constexpr uint32_t kLastFrag = 1U << 28;
constexpr uint32_t kRXLengthMask = 0x3fff;
constexpr uint32_t kRXErrorMask = (1U << 22) | (1U << 21) |
                                  (1U << 20) | (1U << 19);
constexpr uint32_t kRXConfigValue = (1U << 15) | (1U << 14) |
                                    (7U << 8) | (1U << 11) |
                                    0x08 | 0x04 | 0x02;
constexpr uint32_t kRXConfigVerifyMask = (1U << 15) | (1U << 14) |
                                         (7U << 8) | (1U << 11) | 0x3f;

uint64_t joinAddress(uint32_t high, uint32_t low) {
    return (uint64_t(high) << 32) | low;
}

void cleanupBuffer(DMAProvider &dma, DMABuffer &buffer) {
    if (buffer.mapped) dma.unmap(buffer);
    if (buffer.prepared) dma.complete(buffer);
    if (buffer.allocated || buffer.opaque) dma.release(buffer);
}

void cleanupBuffers(DMAProvider &dma, Stage2DResult &result) {
    for (uint32_t i = 0; i < kStage2DRXDescriptorCount; ++i)
        cleanupBuffer(dma, result.rxBuffers[i]);
}

void clearDescriptors(volatile uint32_t *ring) {
    if (!ring) return;
    for (uint32_t i = 0; i < kStage2DRXDescriptorCount; ++i) {
        ring[i * 4 + 0] = 0;
        ring[i * 4 + 1] = 0;
        ring[i * 4 + 2] = 0;
        ring[i * 4 + 3] = 0;
    }
}

bool stopRX(RegisterIO &io, Stage2DResult &result) {
    uint8_t command = 0xff;
    const bool ok = io.write16(kInterruptMask, 0) &&
                    io.write8(kChipCommand, 0) &&
                    io.write32(kRXConfig, 0) &&
                    io.write32(kMulticastAddress4, 0) &&
                    io.write32(kMulticastAddress0, 0) &&
                    io.read8(kChipCommand, command) &&
                    !(command & kTXRXEnable);
    result.chipCommandAfterRX = command;
    result.trafficEnabled = bool(command & kTXRXEnable);
    return ok;
}

bool configureRX(RegisterIO &io, Stage2DResult &result) {
    uint16_t mitigate = 0, maxSize = 0;
    uint32_t config = 0;
    if (!io.write16(kInterruptMask, 0) ||
        !io.write16(kInterruptMitigate, 0) ||
        !io.write16(kRXMaxSize, kStage2CRXBufferLength + 1) ||
        !io.write32(kMulticastAddress4, 0xffffffffU) ||
        !io.write32(kMulticastAddress0, 0xffffffffU) ||
        !io.write32(kRXConfig, kRXConfigValue) ||
        !io.read16(kInterruptMitigate, mitigate) || mitigate != 0 ||
        !io.read16(kRXMaxSize, maxSize) ||
        maxSize != kStage2CRXBufferLength + 1 ||
        !io.read32(kRXConfig, config))
        return false;
    result.rxConfig = config;
    result.rxConfigMaskedObserved = config & kRXConfigVerifyMask;
    return result.rxConfigMaskedObserved == kRXConfigValue;
}

bool safeQuiesce(RegisterIO &io, DMAProvider &dma, Stage2AResult &stage2a,
                 Stage2DResult &result, bool deviceMayOwn) {
    const bool stopped = stopRX(io, result);
    if (stopped && !deviceMayOwn) return true;
    dma.memoryBarrier();
    const bool busMasterOff = dma.setBusMaster(false) == DMAStatus::Ok;
    if (busMasterOff) stage2a.busMasterEnabled = false;
    return busMasterOff;
}

Stage2DResult failBeforeOwn(Stage2DResult &result, Stage2DStatus status,
                            const char *stage, DMAProvider &dma,
                            Stage2AResult &stage2a,
                            volatile uint32_t *ring) {
    result.status = status;
    result.stage = stage;
    clearDescriptors(ring);
    if (ring) {
        dma.memoryBarrier();
        dma.syncForDevice(stage2a.rx);
    }
    cleanupBuffers(dma, result);
    return result;
}

Stage2DResult failAfterOwn(Stage2DResult &result, Stage2DStatus status,
                           const char *stage, RegisterIO &io, DMAProvider &dma,
                           Stage2AResult &stage2a, volatile uint32_t *ring) {
    result.status = status;
    result.stage = stage;
    const uint32_t all = (1U << kStage2DRXDescriptorCount) - 1;
    const bool deviceMayOwn = result.ownershipClearedMask != all;
    if (!safeQuiesce(io, dma, stage2a, result, deviceMayOwn)) {
        result.status = Stage2DStatus::SafeQuiesceFailed;
        result.stage = "rx-burst-safe-quiesce";
        result.unsafeMappingRetained = true;
        return result;
    }
    clearDescriptors(ring);
    dma.memoryBarrier();
    dma.syncForDevice(stage2a.rx);
    cleanupBuffers(dma, result);
    return result;
}
}

Stage2DResult runStage2D(RegisterIO &io, DMAProvider &dma,
                         Stage2AResult &stage2a,
                         const Stage2CResult &stage2c) {
    Stage2DResult result;
    if (!stage2c.complete || !stage2a.complete || !stage2a.busMasterEnabled ||
        !stage2a.rx.mapped || !dma.dmaReady()) {
        result.stage = "stage2c-incomplete";
        return result;
    }

    result.stage = "rx-burst-ring-verify";
    uint32_t low = 0, high = 0;
    if (!io.read32(kRXAddressLow, low) || !io.read32(kRXAddressHigh, high)) {
        result.status = Stage2DStatus::RingRegisterIOFailure;
        return result;
    }
    result.rxRingRegisterIOVA = joinAddress(high, low);
    if (stage2a.rx.segmentCount != 1 ||
        result.rxRingRegisterIOVA != stage2a.rx.segments[0].address) {
        result.status = Stage2DStatus::RingRegisterMismatch;
        return result;
    }

    auto *ringBytes = static_cast<uint8_t *>(dma.bytes(stage2a.rx));
    if (!ringBytes) {
        result.status = Stage2DStatus::DescriptorAccessFailed;
        result.stage = "rx-burst-descriptor-access";
        return result;
    }
    auto *ring = reinterpret_cast<volatile uint32_t *>(ringBytes);
    clearDescriptors(ring);

    for (uint32_t i = 0; i < kStage2DRXDescriptorCount; ++i) {
        result.stage = "rx-burst-buffer-allocate";
        auto status = dma.allocate(result.rxBuffers[i], kStage2DRXBufferLength,
                                   16, DMADirection::FromDevice);
        if (status != DMAStatus::Ok)
            return failBeforeOwn(result, Stage2DStatus::BufferAllocationFailed,
                                 "rx-burst-buffer-allocate", dma, stage2a, ring);
        result.stage = "rx-burst-buffer-prepare";
        if (dma.prepare(result.rxBuffers[i]) != DMAStatus::Ok)
            return failBeforeOwn(result, Stage2DStatus::BufferPrepareFailed,
                                 "rx-burst-buffer-prepare", dma, stage2a, ring);
        result.stage = "rx-burst-buffer-map";
        if (dma.map(result.rxBuffers[i]) != DMAStatus::Ok)
            return failBeforeOwn(result, Stage2DStatus::BufferMapFailed,
                                 "rx-burst-buffer-map", dma, stage2a, ring);
        auto &buffer = result.rxBuffers[i];
        result.bufferSegmentCount[i] = uint32_t(buffer.segmentCount);
        if (buffer.segmentCount != 1 ||
            buffer.mappedLength != kStage2DRXBufferLength ||
            buffer.segments[0].length != kStage2DRXBufferLength ||
            buffer.segments[0].address == 0 ||
            buffer.segments[0].address > UINT64_MAX - (kStage2DRXBufferLength - 1))
            return failBeforeOwn(result, Stage2DStatus::BufferSegmentInvalid,
                                 "rx-burst-buffer-segment", dma, stage2a, ring);
        result.bufferIOVA[i] = buffer.segments[0].address;
        if (!dma.bytes(buffer))
            return failBeforeOwn(result, Stage2DStatus::BufferAccessFailed,
                                 "rx-burst-buffer-access", dma, stage2a, ring);
        if (dma.zero(buffer) != DMAStatus::Ok)
            return failBeforeOwn(result, Stage2DStatus::BufferSyncFailed,
                                 "rx-burst-buffer-zero", dma, stage2a, ring);
        result.descriptorOpts1BeforeOwn[i] = kStage2DRXBufferLength |
            (i + 1 == kStage2DRXDescriptorCount ? kRingEnd : 0);
        ring[i * 4 + 2] = uint32_t(result.bufferIOVA[i]);
        ring[i * 4 + 3] = uint32_t(result.bufferIOVA[i] >> 32);
        ring[i * 4 + 1] = 0;
        ring[i * 4 + 0] = result.descriptorOpts1BeforeOwn[i];
    }
    dma.memoryBarrier();
    if (dma.syncForDevice(stage2a.rx) != DMAStatus::Ok)
        return failBeforeOwn(result, Stage2DStatus::DescriptorSyncFailed,
                             "rx-burst-descriptor-sync", dma, stage2a, ring);

    result.stage = "rx-burst-configure";
    if (!configureRX(io, result))
        return failBeforeOwn(result, Stage2DStatus::RXConfigurationFailed,
                             "rx-burst-configure", dma, stage2a, ring);
    if (!io.read8(kChipCommand, result.chipCommandBeforeRX) ||
        (result.chipCommandBeforeRX & kTXRXEnable))
        return failBeforeOwn(result, Stage2DStatus::RXEnableFailed,
                             "rx-burst-command-precheck", dma, stage2a, ring);

    const uint32_t all = (1U << kStage2DRXDescriptorCount) - 1;
    for (uint32_t i = 0; i < kStage2DRXDescriptorCount; ++i)
        ring[i * 4] = result.descriptorOpts1BeforeOwn[i] | kDescOwn;
    result.ownershipPublishedMask = all;
    dma.memoryBarrier();
    if (dma.syncForDevice(stage2a.rx) != DMAStatus::Ok)
        return failAfterOwn(result, Stage2DStatus::DescriptorSyncFailed,
                            "rx-burst-own-sync", io, dma, stage2a, ring);

    result.stage = "rx-burst-enable";
    if (!io.write8(kChipCommand, kCmdRXEnable) ||
        !io.read8(kChipCommand, result.chipCommandDuringRX) ||
        result.chipCommandDuringRX != kCmdRXEnable)
        return failAfterOwn(result, Stage2DStatus::RXEnableFailed,
                            "rx-burst-enable", io, dma, stage2a, ring);

    result.stage = "rx-burst-poll";
    for (uint32_t attempt = 1; attempt <= kStage2DRXPollAttempts; ++attempt) {
        io.delayUs(kStage2DRXPollIntervalUs);
        if (dma.syncForCPU(stage2a.rx) != DMAStatus::Ok)
            return failAfterOwn(result, Stage2DStatus::PollSyncFailed,
                                "rx-burst-poll-sync", io, dma, stage2a, ring);
        dma.memoryBarrier();
        result.pollCount = attempt;
        for (uint32_t i = 0; i < kStage2DRXDescriptorCount; ++i) {
            const uint32_t bit = 1U << i;
            if (result.ownershipClearedMask & bit) continue;
            const uint32_t opts1 = ring[i * 4];
            result.descriptorOpts1After[i] = opts1;
            result.descriptorOpts2After[i] = ring[i * 4 + 1];
            if (opts1 & kDescOwn) continue;
            result.ownershipClearedMask |= bit;
            result.receivedLength[i] = opts1 & kRXLengthMask;
            result.descriptorErrorBits[i] = opts1 & kRXErrorMask;
            const bool valid = (opts1 & (kFirstFrag | kLastFrag)) ==
                                   (kFirstFrag | kLastFrag) &&
                               !result.descriptorErrorBits[i] &&
                               result.receivedLength[i] >= 4 &&
                               result.receivedLength[i] <= kStage2DRXBufferLength;
            if (!valid)
                return failAfterOwn(result, Stage2DStatus::RXDescriptorInvalid,
                                    "rx-burst-descriptor-result", io, dma,
                                    stage2a, ring);
            if (dma.syncForCPU(result.rxBuffers[i]) != DMAStatus::Ok)
                return failAfterOwn(result, Stage2DStatus::RXBufferSyncFailed,
                                    "rx-burst-buffer-cpu-sync", io, dma,
                                    stage2a, ring);
            result.descriptorValidMask |= bit;
            ++result.completedCount;
            result.packetDMAExercised = true;
        }
        if (result.ownershipClearedMask == all) break;
    }
    if (result.ownershipClearedMask != all) {
        result.timeout = true;
        return failAfterOwn(result, Stage2DStatus::RXTimeout,
                            "rx-burst-timeout", io, dma, stage2a, ring);
    }

    result.stage = "rx-burst-disable";
    if (!stopRX(io, result))
        return failAfterOwn(result, Stage2DStatus::RXDisableFailed,
                            "rx-burst-disable", io, dma, stage2a, ring);
    clearDescriptors(ring);
    dma.memoryBarrier();
    if (dma.syncForDevice(stage2a.rx) != DMAStatus::Ok) {
        cleanupBuffers(dma, result);
        result.status = Stage2DStatus::DescriptorCleanupFailed;
        result.stage = "rx-burst-descriptor-cleanup";
        return result;
    }
    cleanupBuffers(dma, result);
    result.status = Stage2DStatus::Ok;
    result.stage = "stage2d-complete";
    result.complete = true;
    return result;
}

bool teardownStage2D(RegisterIO &io, DMAProvider &dma, Stage2AResult &stage2a,
                     Stage2DResult &result) {
    bool hasBuffer = false;
    for (uint32_t i = 0; i < kStage2DRXDescriptorCount; ++i)
        hasBuffer = hasBuffer || result.rxBuffers[i].opaque ||
                    result.rxBuffers[i].allocated;
    if (!hasBuffer) return true;
    const uint32_t all = (1U << kStage2DRXDescriptorCount) - 1;
    if (!safeQuiesce(io, dma, stage2a, result,
                     result.ownershipPublishedMask &&
                     result.ownershipClearedMask != all)) {
        result.unsafeMappingRetained = true;
        return false;
    }
    auto *bytes = static_cast<uint8_t *>(dma.bytes(stage2a.rx));
    if (bytes) {
        clearDescriptors(reinterpret_cast<volatile uint32_t *>(bytes));
        dma.memoryBarrier();
        dma.syncForDevice(stage2a.rx);
    }
    cleanupBuffers(dma, result);
    result.unsafeMappingRetained = false;
    return true;
}

}
