// SPDX-License-Identifier: GPL-2.0-only
#include "Stage2EProgram.hpp"

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

uint32_t descriptorLength(uint32_t index) {
    return kStage2ERXBufferLength |
           (index + 1 == kStage2ERXDescriptorCount ? kRingEnd : 0);
}

uint64_t joinAddress(uint32_t high, uint32_t low) {
    return (uint64_t(high) << 32) | low;
}

void cleanupBuffer(DMAProvider &dma, DMABuffer &buffer) {
    if (buffer.mapped) dma.unmap(buffer);
    if (buffer.prepared) dma.complete(buffer);
    if (buffer.allocated || buffer.opaque) dma.release(buffer);
}

void cleanupBuffers(DMAProvider &dma, Stage2EResult &result) {
    for (uint32_t i = 0; i < kStage2ERXDescriptorCount; ++i)
        cleanupBuffer(dma, result.rxBuffers[i]);
}

void clearDescriptors(volatile uint32_t *ring) {
    if (!ring) return;
    for (uint32_t i = 0; i < kStage2ERXDescriptorCount; ++i) {
        ring[i * 4 + 0] = 0;
        ring[i * 4 + 1] = 0;
        ring[i * 4 + 2] = 0;
        ring[i * 4 + 3] = 0;
    }
}

bool stopRX(RegisterIO &io, Stage2EResult &result) {
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

bool configureRX(RegisterIO &io, Stage2EResult &result) {
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

// Read-only snapshots. Failures are recorded; the original RX policy is unchanged.
void snapshotRX(RegisterIO &io, PCIConfig *pci, volatile uint32_t *ring,
                Stage2EResult &result, bool initial) {
    ++result.diagnosticSamples;
    uint8_t command = 0xff, phy = 0xff;
    uint16_t interrupt = 0, pciCommand = 0;
    if (io.read8(kChipCommand, command)) {
        result.chipCommandLast = command;
        if (initial) result.chipCommandAtStart = command;
        if (!(command & kCmdRXEnable)) ++result.rxCommandLostCount;
    } else ++result.diagnosticReadFailures;
    if (io.read8(0x6c, phy)) {
        result.phyStatusLast = phy;
        if (initial) result.phyStatusAtStart = phy;
    } else ++result.diagnosticReadFailures;
    if (io.read16(0x3e, interrupt)) result.interruptStatusLast = interrupt;
    else ++result.diagnosticReadFailures;
    if (pci) {
        if (pci->readConfig16(4, pciCommand)) {
            result.pciCommandObserved = true;
            result.pciCommandLast = pciCommand;
            if (initial) result.pciCommandBeforeRX = pciCommand;
            if (!(pciCommand & 4)) ++result.busMasterLostCount;
        } else ++result.diagnosticReadFailures;
    }
    result.ownMaskLast = 0;
    for (uint32_t i = 0; i < kStage2ERXDescriptorCount; ++i) {
        result.lastObservedOpts1[i] = ring[i * 4];
        result.lastObservedAddress[i] = (uint64_t(ring[i*4+3]) << 32) | ring[i*4+2];
        if (ring[i*4] & kDescOwn) result.ownMaskLast |= 1U << i;
    }
}

bool anyDeviceOwned(volatile uint32_t *ring) {
    if (!ring) return false;
    for (uint32_t i = 0; i < kStage2ERXDescriptorCount; ++i)
        if (ring[i * 4] & kDescOwn) return true;
    return false;
}

bool safeQuiesce(RegisterIO &io, DMAProvider &dma, Stage2AResult &stage2a,
                 Stage2EResult &result, bool deviceMayOwn) {
    const bool stopped = stopRX(io, result);
    if (stopped && !deviceMayOwn) return true;
    dma.memoryBarrier();
    const bool busMasterOff = dma.setBusMaster(false) == DMAStatus::Ok;
    if (busMasterOff) stage2a.busMasterEnabled = false;
    return busMasterOff;
}

Stage2EResult failBeforeOwn(Stage2EResult &result, Stage2EStatus status,
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

Stage2EResult failAfterOwn(Stage2EResult &result, Stage2EStatus status,
                           const char *stage, RegisterIO &io, DMAProvider &dma,
                           Stage2AResult &stage2a,
                           volatile uint32_t *ring) {
    result.status = status;
    result.stage = stage;
    const bool deviceMayOwn = anyDeviceOwned(ring);
    if (!safeQuiesce(io, dma, stage2a, result, deviceMayOwn)) {
        result.status = Stage2EStatus::SafeQuiesceFailed;
        result.stage = "rx-recycle-safe-quiesce";
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

static Stage2EResult runRecycleSession(RegisterIO &io, DMAProvider &dma,
                         Stage2AResult &stage2a, PCIConfig *pci) {
    Stage2EResult result;
    if (!stage2a.complete || !stage2a.busMasterEnabled ||
        !stage2a.rx.mapped || !dma.dmaReady()) {
        result.stage = "stage2a-incomplete";
        return result;
    }

    result.stage = "rx-recycle-ring-verify";
    uint32_t low = 0, high = 0;
    if (!io.read32(kRXAddressLow, low) || !io.read32(kRXAddressHigh, high)) {
        result.status = Stage2EStatus::RingRegisterIOFailure;
        return result;
    }
    result.rxRingRegisterIOVA = joinAddress(high, low);
    if (stage2a.rx.segmentCount != 1 ||
        result.rxRingRegisterIOVA != stage2a.rx.segments[0].address) {
        result.status = Stage2EStatus::RingRegisterMismatch;
        return result;
    }

    auto *ringBytes = static_cast<uint8_t *>(dma.bytes(stage2a.rx));
    if (!ringBytes) {
        result.status = Stage2EStatus::DescriptorAccessFailed;
        result.stage = "rx-recycle-descriptor-access";
        return result;
    }
    auto *ring = reinterpret_cast<volatile uint32_t *>(ringBytes);
    clearDescriptors(ring);

    for (uint32_t i = 0; i < kStage2ERXDescriptorCount; ++i) {
        result.stage = "rx-recycle-buffer-allocate";
        if (dma.allocate(result.rxBuffers[i], kStage2ERXBufferLength, 16,
                         DMADirection::FromDevice) != DMAStatus::Ok)
            return failBeforeOwn(result, Stage2EStatus::BufferAllocationFailed,
                                 result.stage, dma, stage2a, ring);
        result.stage = "rx-recycle-buffer-prepare";
        if (dma.prepare(result.rxBuffers[i]) != DMAStatus::Ok)
            return failBeforeOwn(result, Stage2EStatus::BufferPrepareFailed,
                                 result.stage, dma, stage2a, ring);
        result.stage = "rx-recycle-buffer-map";
        if (dma.map(result.rxBuffers[i]) != DMAStatus::Ok)
            return failBeforeOwn(result, Stage2EStatus::BufferMapFailed,
                                 result.stage, dma, stage2a, ring);
        auto &buffer = result.rxBuffers[i];
        result.bufferSegmentCount[i] = uint32_t(buffer.segmentCount);
        if (buffer.segmentCount != 1 ||
            buffer.mappedLength != kStage2ERXBufferLength ||
            buffer.segments[0].length != kStage2ERXBufferLength ||
            !buffer.segments[0].address ||
            buffer.segments[0].address > UINT64_MAX - (kStage2ERXBufferLength - 1))
            return failBeforeOwn(result, Stage2EStatus::BufferSegmentInvalid,
                                 "rx-recycle-buffer-segment", dma, stage2a,
                                 ring);
        result.bufferIOVA[i] = buffer.segments[0].address;
        if (!dma.bytes(buffer))
            return failBeforeOwn(result, Stage2EStatus::BufferAccessFailed,
                                 "rx-recycle-buffer-access", dma, stage2a,
                                 ring);
        if (dma.zero(buffer) != DMAStatus::Ok ||
            dma.syncForDevice(buffer) != DMAStatus::Ok)
            return failBeforeOwn(result, Stage2EStatus::BufferSyncFailed,
                                 "rx-recycle-buffer-device-sync", dma,
                                 stage2a, ring);
        ring[i * 4 + 2] = uint32_t(result.bufferIOVA[i]);
        ring[i * 4 + 3] = uint32_t(result.bufferIOVA[i] >> 32);
        ring[i * 4 + 1] = 0;
        ring[i * 4 + 0] = descriptorLength(i);
    }
    dma.memoryBarrier();
    if (dma.syncForDevice(stage2a.rx) != DMAStatus::Ok)
        return failBeforeOwn(result, Stage2EStatus::DescriptorSyncFailed,
                             "rx-recycle-descriptor-sync", dma, stage2a,
                             ring);

    result.stage = "rx-recycle-configure";
    if (!configureRX(io, result))
        return failBeforeOwn(result, Stage2EStatus::RXConfigurationFailed,
                             result.stage, dma, stage2a, ring);
    if (!io.read8(kChipCommand, result.chipCommandBeforeRX) ||
        (result.chipCommandBeforeRX & kTXRXEnable))
        return failBeforeOwn(result, Stage2EStatus::RXEnableFailed,
                             "rx-recycle-command-precheck", dma, stage2a,
                             ring);

    for (uint32_t i = 0; i < kStage2ERXDescriptorCount; ++i)
        ring[i * 4] = descriptorLength(i) | kDescOwn;
    dma.memoryBarrier();
    if (dma.syncForDevice(stage2a.rx) != DMAStatus::Ok)
        return failAfterOwn(result, Stage2EStatus::DescriptorSyncFailed,
                            "rx-recycle-own-sync", io, dma, stage2a, ring);

    result.stage = "rx-recycle-enable";
    if (!io.write8(kChipCommand, kCmdRXEnable) ||
        !io.read8(kChipCommand, result.chipCommandDuringRX) ||
        result.chipCommandDuringRX != kCmdRXEnable)
        return failAfterOwn(result, Stage2EStatus::RXEnableFailed,
                            result.stage, io, dma, stage2a, ring);

    snapshotRX(io, pci, ring, result, true);
    result.stage = "rx-recycle-poll";
    for (uint32_t attempt = 1; attempt <= kStage2ERXPollAttempts; ++attempt) {
        io.delayUs(kStage2ERXPollIntervalUs);
        if (dma.syncForCPU(stage2a.rx) != DMAStatus::Ok)
            return failAfterOwn(result, Stage2EStatus::PollSyncFailed,
                                "rx-recycle-poll-sync", io, dma, stage2a,
                                ring);
        dma.memoryBarrier();
        result.pollCount = attempt;
        if (attempt == 1 || attempt % 100 == 0)
            snapshotRX(io, pci, ring, result, false);
        bool rearmed = false;
        for (uint32_t i = 0; i < kStage2ERXDescriptorCount; ++i) {
            if (result.completionCount[i] >= kStage2ERXCycles) continue;
            const uint32_t opts1 = ring[i * 4];
            if (opts1 & kDescOwn) continue;
            const uint32_t errorBits = opts1 & kRXErrorMask;
            const uint32_t length = opts1 & kRXLengthMask;
            result.lastDescriptorOpts1[i] = opts1;
            result.lastDescriptorErrorBits[i] = errorBits;
            const bool valid = (opts1 & (kFirstFrag | kLastFrag)) ==
                                   (kFirstFrag | kLastFrag) &&
                               !errorBits && length >= 4 &&
                               length <= kStage2ERXBufferLength;
            if (!valid)
                return failAfterOwn(result, Stage2EStatus::RXDescriptorInvalid,
                                    "rx-recycle-descriptor-result", io, dma,
                                    stage2a, ring);
            if (dma.syncForCPU(result.rxBuffers[i]) != DMAStatus::Ok)
                return failAfterOwn(result, Stage2EStatus::RXBufferSyncFailed,
                                    "rx-recycle-buffer-cpu-sync", io, dma,
                                    stage2a, ring);
            ++result.completionCount[i];
            ++result.completedCount;
            if (!result.firstCompletionPoll) result.firstCompletionPoll = attempt;
            result.packetDMAExercised = true;
            if (result.completionCount[i] == 1) {
                result.firstCycleMask |= 1U << i;
                if (result.firstCycleMask == 15 && !result.firstCycleCompletePoll)
                    result.firstCycleCompletePoll = attempt;
                result.firstReceivedLength[i] = length;
                if (dma.zero(result.rxBuffers[i]) != DMAStatus::Ok ||
                    dma.syncForDevice(result.rxBuffers[i]) != DMAStatus::Ok)
                    return failAfterOwn(result, Stage2EStatus::RXRearmFailed,
                                        "rx-recycle-buffer-rearm", io, dma,
                                        stage2a, ring);
                ring[i * 4 + 1] = 0;
                ring[i * 4] = descriptorLength(i) | kDescOwn;
                ++result.rearmCount[i];
                ++result.rearmedCount;
                rearmed = true;
            } else {
                result.secondCycleMask |= 1U << i;
                result.secondReceivedLength[i] = length;
            }
            result.descriptorValidMask |= 1U << i;
        }
        if (rearmed) {
            dma.memoryBarrier();
            if (dma.syncForDevice(stage2a.rx) != DMAStatus::Ok)
                return failAfterOwn(result, Stage2EStatus::RXRearmFailed,
                                    "rx-recycle-descriptor-rearm", io, dma,
                                    stage2a, ring);
        }
        if (result.completedCount == kStage2ERXTargetCount) break;
    }
    snapshotRX(io, pci, ring, result, false);
    if (result.completedCount != kStage2ERXTargetCount) {
        result.timeout = true;
        return failAfterOwn(result, Stage2EStatus::RXTimeout,
                            "rx-recycle-timeout", io, dma, stage2a, ring);
    }

    result.stage = "rx-recycle-disable";
    if (!stopRX(io, result))
        return failAfterOwn(result, Stage2EStatus::RXDisableFailed,
                            result.stage, io, dma, stage2a, ring);
    clearDescriptors(ring);
    dma.memoryBarrier();
    if (dma.syncForDevice(stage2a.rx) != DMAStatus::Ok) {
        cleanupBuffers(dma, result);
        result.status = Stage2EStatus::DescriptorCleanupFailed;
        result.stage = "rx-recycle-descriptor-cleanup";
        return result;
    }
    cleanupBuffers(dma, result);
    result.status = Stage2EStatus::Ok;
    result.stage = "stage2e-complete";
    result.complete = true;
    return result;
}

Stage2EResult runStage2E(RegisterIO &io, DMAProvider &dma,
                         Stage2AResult &stage2a,
                         const Stage2DResult &stage2d, PCIConfig *pci) {
    if (!stage2d.complete) {
        Stage2EResult result; result.stage = "stage2d-incomplete";
        return result;
    }
    return runRecycleSession(io, dma, stage2a, pci);
}

Stage2EResult runStage2EFromC(RegisterIO &io, DMAProvider &dma,
                            Stage2AResult &stage2a,
                            const Stage2CResult &stage2c, PCIConfig *pci) {
    if (!stage2c.complete) {
        Stage2EResult result;
        result.status = Stage2EStatus::Stage2CIncomplete;
        result.stage = "stage2c-incomplete";
        return result;
    }
    return runRecycleSession(io, dma, stage2a, pci);
}

bool teardownStage2E(RegisterIO &io, DMAProvider &dma, Stage2AResult &stage2a,
                     Stage2EResult &result) {
    bool hasBuffer = false;
    for (uint32_t i = 0; i < kStage2ERXDescriptorCount; ++i)
        hasBuffer = hasBuffer || result.rxBuffers[i].opaque ||
                    result.rxBuffers[i].allocated;
    if (!hasBuffer) return true;
    auto *bytes = static_cast<uint8_t *>(dma.bytes(stage2a.rx));
    auto *ring = bytes ? reinterpret_cast<volatile uint32_t *>(bytes) : nullptr;
    if (!safeQuiesce(io, dma, stage2a, result, anyDeviceOwned(ring))) {
        result.unsafeMappingRetained = true;
        return false;
    }
    clearDescriptors(ring);
    if (ring) {
        dma.memoryBarrier();
        dma.syncForDevice(stage2a.rx);
    }
    cleanupBuffers(dma, result);
    result.unsafeMappingRetained = false;
    return true;
}

}
