// SPDX-License-Identifier: GPL-2.0-only
#include "Stage2BProgram.hpp"
#include "RegisterAccess.hpp"
#include <string.h>

namespace realtek_next {
namespace {
constexpr uint16_t kTXAddressLow = 0x20;
constexpr uint16_t kTXAddressHigh = 0x24;
constexpr uint16_t kChipCommand = 0x37;
constexpr uint16_t kTXPoll = 0x38;
constexpr uint16_t kInterruptMask = 0x3c;
constexpr uint16_t kTXConfig = 0x40;
constexpr uint16_t kInterruptMitigate = 0xe2;
constexpr uint16_t kRXAddressLow = 0xe4;
constexpr uint16_t kRXAddressHigh = 0xe8;
constexpr uint16_t kMaxTXPacketSize = 0xec;
constexpr uint8_t kCmdTXEnable = 0x04;
constexpr uint8_t kCmdRXEnable = 0x08;
constexpr uint8_t kTXRXEnable = kCmdTXEnable | kCmdRXEnable;
constexpr uint8_t kNormalPriorityQueue = 0x40;
constexpr uint32_t kDescOwn = 1U << 31;
constexpr uint32_t kFirstFrag = 1U << 29;
constexpr uint32_t kLastFrag = 1U << 28;
constexpr uint32_t kTXConfigValue = (3U << 24) | (7U << 8) | (1U << 7);

uint64_t joinAddress(uint32_t high, uint32_t low) {
    return (uint64_t(high) << 32) | low;
}

void cleanupBuffer(DMAProvider &dma, DMABuffer &buffer) {
    if (buffer.mapped) dma.unmap(buffer);
    if (buffer.prepared) dma.complete(buffer);
    if (buffer.allocated || buffer.opaque) dma.release(buffer);
}

bool waitForLink(RegisterIO &io, Stage2BResult &result) {
    RegisterAccess access(io);
    for (uint32_t attempt = 1; attempt <= kStage2BLinkPollAttempts; ++attempt) {
        access.restorePHYBase();
        const auto first = access.mdioRead(1);
        if (first.status != AccessStatus::Ok) return false;
        const auto second = access.mdioRead(1);
        if (second.status != AccessStatus::Ok) return false;
        result.phyBMSRFirst = first.value;
        result.phyBMSRSecond = second.value;
        result.phyLink = bool(second.value & 0x0004);
        result.linkPollCount = attempt;
        if (result.phyLink) return true;
        if (attempt != kStage2BLinkPollAttempts) {
            io.delayUs(kStage2BLinkPollIntervalUs);
            result.linkWaitUs += kStage2BLinkPollIntervalUs;
        }
    }
    result.linkTimedOut = true;
    return true;
}

bool readAndVerifyRingRegisters(RegisterIO &io, const Stage2AResult &stage2a,
                                Stage2BResult &result) {
    if (!io.read32(kTXAddressLow, result.txRingRegisterLow) ||
        !io.read32(kTXAddressHigh, result.txRingRegisterHigh) ||
        !io.read32(kRXAddressLow, result.rxRingRegisterLow) ||
        !io.read32(kRXAddressHigh, result.rxRingRegisterHigh))
        return false;
    result.txRingRegisterIOVA = joinAddress(result.txRingRegisterHigh,
                                            result.txRingRegisterLow);
    result.rxRingRegisterIOVA = joinAddress(result.rxRingRegisterHigh,
                                            result.rxRingRegisterLow);
    return stage2a.tx.segmentCount == 1 && stage2a.rx.segmentCount == 1 &&
           result.txRingRegisterIOVA == stage2a.tx.segments[0].address &&
           result.rxRingRegisterIOVA == stage2a.rx.segments[0].address;
}

bool validSourceMAC(const uint8_t *mac) {
    bool any = false;
    for (unsigned i = 0; i < 6; ++i) any |= mac[i] != 0;
    return any && !(mac[0] & 1);
}

bool buildFrame(RegisterIO &io, uint8_t *frame) {
    uint32_t macLow = 0;
    uint16_t macHigh = 0;
    if (!io.read32(0x00, macLow) || !io.read16(0x04, macHigh)) return false;
    uint8_t source[6] = {uint8_t(macLow), uint8_t(macLow >> 8),
                         uint8_t(macLow >> 16), uint8_t(macLow >> 24),
                         uint8_t(macHigh), uint8_t(macHigh >> 8)};
    if (!validSourceMAC(source)) return false;
    memset(frame, 0, kStage2BFrameLength);
    const uint8_t destination[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    memcpy(frame, destination, sizeof(destination));
    memcpy(frame + 6, source, sizeof(source));
    frame[12] = 0x88; frame[13] = 0xb5;
    const char marker[] = "RealtekVTDNext-STAGE2B";
    memcpy(frame + 14, marker, sizeof(marker) - 1);
    return true;
}

bool configureOneShotTX(RegisterIO &io) {
    uint32_t txConfig = 0;
    uint16_t mitigation = 0xffff;
    uint8_t maxPacket = 0;
    return io.write16(kInterruptMask, 0) &&
           io.write16(kInterruptMitigate, 0) &&
           io.write8(kMaxTXPacketSize, 0x27) &&
           io.write32(kTXConfig, kTXConfigValue) &&
           io.read16(kInterruptMitigate, mitigation) && mitigation == 0 &&
           io.read8(kMaxTXPacketSize, maxPacket) && maxPacket == 0x27 &&
           io.read32(kTXConfig, txConfig) &&
           (txConfig & kTXConfigValue) == kTXConfigValue;
}

bool quiesceOwnedBuffer(RegisterIO &io, DMAProvider &dma, Stage2AResult &stage2a,
                        Stage2BResult &result) {
    uint8_t command = 0xff;
    const bool txStopped = io.write16(kInterruptMask, 0) &&
                           io.write8(kChipCommand, 0) &&
                           io.read8(kChipCommand, command) &&
                           !(command & kTXRXEnable);
    dma.memoryBarrier();
    const bool busMasterOff = dma.setBusMaster(false) == DMAStatus::Ok;
    if (busMasterOff) stage2a.busMasterEnabled = false;
    result.chipCommandAfterTX = command;
    result.trafficEnabled = bool(command & kTXRXEnable);
    return txStopped && busMasterOff;
}

void clearDescriptor(volatile uint32_t *descriptor) {
    descriptor[0] = 0;
    descriptor[1] = 0;
    descriptor[2] = 0;
    descriptor[3] = 0;
}

Stage2BResult failBeforeOwn(Stage2BResult result, Stage2BStatus status,
                            const char *stage, DMAProvider &dma) {
    result.status = status;
    result.stage = stage;
    cleanupBuffer(dma, result.txBuffer);
    return result;
}

Stage2BResult failAfterOwn(Stage2BResult result, Stage2BStatus status,
                           const char *stage, RegisterIO &io, DMAProvider &dma,
                           Stage2AResult &stage2a, volatile uint32_t *descriptor) {
    result.status = status;
    result.stage = stage;
    if (!quiesceOwnedBuffer(io, dma, stage2a, result)) {
        result.status = Stage2BStatus::SafeQuiesceFailed;
        result.stage = "tx-safe-quiesce";
        result.unsafeMappingRetained = true;
        return result;
    }
    clearDescriptor(descriptor);
    dma.memoryBarrier();
    dma.syncForDevice(stage2a.tx);
    cleanupBuffer(dma, result.txBuffer);
    return result;
}
}

Stage2BResult runStage2B(RegisterIO &io, DMAProvider &dma, Stage2AResult &stage2a) {
    Stage2BResult result;
    if (!stage2a.complete || !stage2a.busMasterEnabled || !stage2a.tx.mapped ||
        !stage2a.rx.mapped || !dma.dmaReady()) {
        result.stage = "stage2a-incomplete";
        return result;
    }

    result.stage = "ring-register-readback";
    if (!readAndVerifyRingRegisters(io, stage2a, result)) {
        const bool readCompleted = result.txRingRegisterIOVA || result.rxRingRegisterIOVA ||
                                   result.txRingRegisterLow || result.txRingRegisterHigh ||
                                   result.rxRingRegisterLow || result.rxRingRegisterHigh;
        result.status = readCompleted ? Stage2BStatus::RingRegisterMismatch
                                      : Stage2BStatus::RingRegisterIOFailure;
        result.stage = "tx-ring-register-verify";
        return result;
    }

    result.stage = "phy-link-wait";
    if (!waitForLink(io, result)) {
        result.status = Stage2BStatus::LinkReadFailure;
        return result;
    }
    if (!result.phyLink) {
        result.status = Stage2BStatus::LinkDown;
        result.stage = "link-down";
        return result;
    }

    result.stage = "tx-configure";
    if (!configureOneShotTX(io)) {
        result.status = Stage2BStatus::TXConfigurationFailed;
        return result;
    }
    result.txConfigured = true;

    result.txBufferLength = kStage2BFrameLength;
    result.stage = "tx-buffer-allocate";
    auto dmaStatus = dma.allocate(result.txBuffer, kStage2BFrameLength, 16,
                                  DMADirection::ToDevice);
    if (dmaStatus != DMAStatus::Ok)
        return failBeforeOwn(result, Stage2BStatus::BufferAllocationFailed,
                             "tx-buffer-allocate", dma);
    result.stage = "tx-buffer-prepare";
    dmaStatus = dma.prepare(result.txBuffer);
    if (dmaStatus != DMAStatus::Ok)
        return failBeforeOwn(result, Stage2BStatus::BufferPrepareFailed,
                             "tx-buffer-prepare", dma);
    result.stage = "tx-buffer-map";
    dmaStatus = dma.map(result.txBuffer);
    if (dmaStatus != DMAStatus::Ok)
        return failBeforeOwn(result, Stage2BStatus::BufferMapFailed,
                             "tx-buffer-map", dma);
    result.txBufferSegmentCount = uint32_t(result.txBuffer.segmentCount);
    if (result.txBuffer.segmentCount != 1 ||
        result.txBuffer.mappedLength != kStage2BFrameLength ||
        result.txBuffer.segments[0].length != kStage2BFrameLength ||
        result.txBuffer.segments[0].address == 0 ||
        result.txBuffer.segments[0].address > UINT64_MAX - (kStage2BFrameLength - 1))
        return failBeforeOwn(result, Stage2BStatus::BufferSegmentInvalid,
                             "tx-buffer-segment", dma);
    result.txBufferMapped = true;
    result.txBufferIOVA = result.txBuffer.segments[0].address;

    auto *frame = static_cast<uint8_t *>(dma.bytes(result.txBuffer));
    if (!frame)
        return failBeforeOwn(result, Stage2BStatus::BufferAccessFailed,
                             "tx-buffer-access", dma);
    if (!buildFrame(io, frame))
        return failBeforeOwn(result, Stage2BStatus::SourceMACInvalid,
                             "source-mac", dma);
    dma.memoryBarrier();
    if (dma.syncForDevice(result.txBuffer) != DMAStatus::Ok)
        return failBeforeOwn(result, Stage2BStatus::BufferSyncFailed,
                             "tx-buffer-sync", dma);

    auto *descriptorBytes = static_cast<uint8_t *>(dma.bytes(stage2a.tx));
    if (!descriptorBytes)
        return failBeforeOwn(result, Stage2BStatus::DescriptorAccessFailed,
                             "tx-descriptor-access", dma);
    auto *descriptor = reinterpret_cast<volatile uint32_t *>(descriptorBytes);
    result.txDesc0AddressLow = uint32_t(result.txBufferIOVA);
    result.txDesc0AddressHigh = uint32_t(result.txBufferIOVA >> 32);
    result.txDesc0Opts2BeforeOwn = 0;
    result.txDesc0Opts1BeforeOwn = kLastFrag | kStage2BFrameLength;
    descriptor[2] = result.txDesc0AddressLow;
    descriptor[3] = result.txDesc0AddressHigh;
    descriptor[1] = result.txDesc0Opts2BeforeOwn;
    descriptor[0] = result.txDesc0Opts1BeforeOwn;
    dma.memoryBarrier();
    if (dma.syncForDevice(stage2a.tx) != DMAStatus::Ok)
        return failBeforeOwn(result, Stage2BStatus::DescriptorSyncFailed,
                             "tx-descriptor-sync", dma);
    if (joinAddress(descriptor[3], descriptor[2]) != result.txBufferIOVA)
        return failBeforeOwn(result, Stage2BStatus::DescriptorMismatch,
                             "tx-descriptor-verify", dma);

    result.stage = "tx-enable";
    if (!io.write16(kInterruptMask, 0) ||
        !io.read8(kChipCommand, result.chipCommandBeforeTX) ||
        (result.chipCommandBeforeTX & kTXRXEnable) ||
        !io.write8(kChipCommand, kCmdTXEnable) ||
        !io.read8(kChipCommand, result.chipCommandDuringTX) ||
        result.chipCommandDuringTX != kCmdTXEnable) {
        io.write8(kChipCommand, 0);
        io.read8(kChipCommand, result.chipCommandAfterTX);
        result.trafficEnabled = bool(result.chipCommandAfterTX & kTXRXEnable);
        return failBeforeOwn(result, Stage2BStatus::TXEnableFailed,
                             "tx-enable", dma);
    }

    descriptor[0] = result.txDesc0Opts1BeforeOwn | kDescOwn | kFirstFrag;
    result.ownershipPublished = true;
    dma.memoryBarrier();
    if (dma.syncForDevice(stage2a.tx) != DMAStatus::Ok)
        return failAfterOwn(result, Stage2BStatus::DescriptorSyncFailed,
                            "tx-own-sync", io, dma, stage2a, descriptor);
    result.stage = "tx-kick";
    if (!io.write8(kTXPoll, kNormalPriorityQueue))
        return failAfterOwn(result, Stage2BStatus::TXKickFailed,
                            "tx-kick", io, dma, stage2a, descriptor);

    result.stage = "tx-poll";
    for (uint32_t attempt = 1; attempt <= kStage2BPollAttempts; ++attempt) {
        io.delayUs(kStage2BPollIntervalUs);
        if (dma.syncForCPU(stage2a.tx) != DMAStatus::Ok)
            return failAfterOwn(result, Stage2BStatus::PollSyncFailed,
                                "tx-poll-sync", io, dma, stage2a, descriptor);
        dma.memoryBarrier();
        result.txDesc0Opts1After = descriptor[0];
        result.txDesc0Opts2After = descriptor[1];
        result.pollCount = attempt;
        if (!(result.txDesc0Opts1After & kDescOwn)) {
            result.ownershipCleared = true;
            result.txCompleted = true;
            result.packetDMAExercised = true;
            break;
        }
    }
    if (!result.ownershipCleared) {
        result.timeout = true;
        return failAfterOwn(result, Stage2BStatus::TXTimeout,
                            "tx-timeout", io, dma, stage2a, descriptor);
    }

    result.stage = "tx-disable";
    if (!io.write8(kChipCommand, 0) ||
        !io.read8(kChipCommand, result.chipCommandAfterTX) ||
        (result.chipCommandAfterTX & kTXRXEnable)) {
        result.status = Stage2BStatus::TXDisableFailed;
        result.trafficEnabled = true;
        quiesceOwnedBuffer(io, dma, stage2a, result);
        clearDescriptor(descriptor);
        dma.memoryBarrier();
        dma.syncForDevice(stage2a.tx);
        cleanupBuffer(dma, result.txBuffer);
        return result;
    }
    result.trafficEnabled = false;
    clearDescriptor(descriptor);
    dma.memoryBarrier();
    if (dma.syncForDevice(stage2a.tx) != DMAStatus::Ok) {
        cleanupBuffer(dma, result.txBuffer);
        result.status = Stage2BStatus::DescriptorCleanupFailed;
        result.stage = "tx-descriptor-cleanup";
        return result;
    }
    cleanupBuffer(dma, result.txBuffer);
    result.status = Stage2BStatus::Ok;
    result.stage = "stage2b-complete";
    result.complete = true;
    return result;
}

bool teardownStage2B(RegisterIO &io, DMAProvider &dma, Stage2AResult &stage2a,
                     Stage2BResult &result) {
    if (!result.txBuffer.opaque && !result.txBuffer.allocated) return true;
    if (!quiesceOwnedBuffer(io, dma, stage2a, result)) {
        result.unsafeMappingRetained = true;
        return false;
    }
    auto *bytes = static_cast<uint8_t *>(dma.bytes(stage2a.tx));
    if (bytes) {
        clearDescriptor(reinterpret_cast<volatile uint32_t *>(bytes));
        dma.memoryBarrier();
        dma.syncForDevice(stage2a.tx);
    }
    cleanupBuffer(dma, result.txBuffer);
    result.unsafeMappingRetained = false;
    return true;
}

}
