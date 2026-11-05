// SPDX-License-Identifier: GPL-2.0-only
#include "Stage2GProgram.hpp"
#include <string.h>

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
constexpr uint8_t kCmdRXEnable = 0x0c;
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
    return kStage2GRXBufferLength |
           (index + 1 == kStage2GRXDescriptorCount ? kRingEnd : 0);
}

uint64_t joinAddress(uint32_t high, uint32_t low) {
    return (uint64_t(high) << 32) | low;
}

void cleanupBuffer(DMAProvider &dma, DMABuffer &buffer) {
    if (buffer.mapped) dma.unmap(buffer);
    if (buffer.prepared) dma.complete(buffer);
    if (buffer.allocated || buffer.opaque) dma.release(buffer);
}

void cleanupBuffers(DMAProvider &dma, Stage2GResult &result) {
    for (uint32_t i = 0; i < kStage2GRXDescriptorCount; ++i)
        cleanupBuffer(dma, result.rxBuffers[i]);
    cleanupBuffer(dma, result.txBuffer);
}

void clearDescriptors(volatile uint32_t *ring) {
    if (!ring) return;
    for (uint32_t i = 0; i < kStage2GRXDescriptorCount; ++i) {
        ring[i * 4 + 0] = 0;
        ring[i * 4 + 1] = 0;
        ring[i * 4 + 2] = 0;
        ring[i * 4 + 3] = 0;
    }
}

bool stopRX(RegisterIO &io, Stage2GResult &result) {
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

bool configureRX(RegisterIO &io, Stage2GResult &result) {
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
                Stage2GResult &result, bool initial) {
    ++result.diagnosticSamples;
    uint8_t command = 0xff, phy = 0xff;
    uint16_t interrupt = 0, pciCommand = 0;
    if (io.read8(kChipCommand, command)) {
        result.chipCommandLast = command;
        if (initial) result.chipCommandAtStart = command;
        if ((command & kCmdRXEnable)!=kCmdRXEnable) ++result.rxCommandLostCount;
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
    for (uint32_t i = 0; i < kStage2GRXDescriptorCount; ++i) {
        result.lastObservedOpts1[i] = ring[i * 4];
        result.lastObservedAddress[i] = (uint64_t(ring[i*4+3]) << 32) | ring[i*4+2];
        if (ring[i*4] & kDescOwn) result.ownMaskLast |= 1U << i;
    }
}

bool prepareTX(RegisterIO &io, DMAProvider &dma, Stage2AResult &a, Stage2GResult &r) {
    uint32_t low=0,high=0,macLow=0,config=0;uint16_t macHigh=0;
    if (!a.tx.mapped || !io.read32(0x20,low) || !io.read32(0x24,high) ||
        joinAddress(high,low)!=a.tx.segments[0].address || a.tx.segmentCount!=1 ||
        !io.read32(0,macLow) || !io.read16(4,macHigh)) return false;
    uint8_t mac[6]={uint8_t(macLow),uint8_t(macLow>>8),uint8_t(macLow>>16),
                   uint8_t(macLow>>24),uint8_t(macHigh),uint8_t(macHigh>>8)};
    bool nonzero=false;for(unsigned i=0;i<6;++i)nonzero|=mac[i]!=0;
    if (!nonzero || (mac[0]&1)) return false;
    constexpr uint32_t size=kStage2GTXRingCount*kStage2GTXFrameLength;
    if(dma.allocate(r.txBuffer,size,16,DMADirection::ToDevice)!=DMAStatus::Ok ||
       dma.prepare(r.txBuffer)!=DMAStatus::Ok || dma.map(r.txBuffer)!=DMAStatus::Ok) return false;
    if(r.txBuffer.segmentCount!=1 || r.txBuffer.mappedLength!=size ||
       r.txBuffer.segments[0].length!=size || !r.txBuffer.segments[0].address ||
       r.txBuffer.segments[0].address>UINT64_MAX-(size-1)) return false;
    r.txBufferIOVA=r.txBuffer.segments[0].address;
    auto bytes=static_cast<uint8_t*>(dma.bytes(r.txBuffer));
    r.txRing=static_cast<volatile uint32_t*>(dma.bytes(a.tx));
    if(!bytes || !r.txRing)return false;
    const char marker[]="RealtekVTDNext-STAGE2G";
    for(unsigned i=0;i<kStage2GTXRingCount;++i){
        uint8_t *f=bytes+i*kStage2GTXFrameLength;memset(f,0,kStage2GTXFrameLength);
        memset(f,0xff,6);memcpy(f+6,mac,6);f[12]=0x88;f[13]=0xb5;
        memcpy(f+14,marker,sizeof(marker)-1);f[43]=uint8_t(i);
        for(unsigned j=0;j<4;++j)r.txRing[i*4+j]=0;
    }
    dma.memoryBarrier();
    constexpr uint32_t txconfig=(3U<<24)|(7U<<8)|(1U<<7);
    uint8_t maxPacket=0;
    return dma.syncForDevice(r.txBuffer)==DMAStatus::Ok &&
        dma.syncForDevice(a.tx)==DMAStatus::Ok && io.write16(0x3e,0xffff) && io.write8(0xec,0x27) &&
        io.read8(0xec,maxPacket) && maxPacket==0x27 &&
        io.write32(0x40,txconfig) && io.read32(0x40,config) && (config&txconfig)==txconfig;
}

bool serviceTX(RegisterIO &io, DMAProvider &dma, Stage2AResult &a, Stage2GResult &r,
               unsigned attempt) {
    if(dma.syncForCPU(a.tx)!=DMAStatus::Ok){r.status=Stage2GStatus::TXSyncFailed;return false;}
    dma.memoryBarrier();
    uint16_t interrupt=0;
    if(!io.read16(0x3e,interrupt)){r.status=Stage2GStatus::TXSyncFailed;return false;}
    r.txErrorBits|=interrupt & 0x0008; // Linux r8169 IntrStatus TxErr.
    if(r.txErrorBits){r.status=Stage2GStatus::TXDescriptorError;return false;}
    for(unsigned i=0;i<kStage2GTXRingCount;++i){
        if(r.txPending[i] && !(r.txRing[i*4]&kDescOwn)){
            r.txCompletionMask |= 1U << r.txSequence[i];
            ++r.txCompleted;++r.txDescriptorCompletions[i];r.txPending[i]=false;
        }
    }
    r.txMayOwn=r.txCompleted!=r.txSubmitted;
    if(r.txSubmitted<kStage2GTXTarget && attempt%100==1){
        const unsigned sequence=r.txSubmitted, i=sequence%kStage2GTXRingCount;
        // Reuse only after this descriptor's OWN was returned and CPU-synchronized.
        if(r.txPending[i] || (r.txRing[i*4]&kDescOwn))return true;
        auto *bytes=static_cast<uint8_t*>(dma.bytes(r.txBuffer));
        if(!bytes){r.status=Stage2GStatus::TXPrepareFailed;return false;}
        bytes[i*kStage2GTXFrameLength+43]=uint8_t(sequence);
        dma.memoryBarrier();
        if(dma.syncForDevice(r.txBuffer)!=DMAStatus::Ok){r.status=Stage2GStatus::TXSyncFailed;return false;}
        const uint64_t address=r.txBufferIOVA+uint64_t(i)*kStage2GTXFrameLength;
        r.txRing[i*4+2]=uint32_t(address);r.txRing[i*4+3]=uint32_t(address>>32);
        r.txRing[i*4+1]=0;
        r.txRing[i*4]=kDescOwn|kFirstFrag|kLastFrag|kStage2GTXFrameLength|
                       (i+1==kStage2GTXRingCount?kRingEnd:0);
        if(r.txSubmissionCount[i])++r.txRearmed;
        ++r.txSubmissionCount[i];r.txSequence[i]=sequence;r.txPending[i]=true;
        ++r.txSubmitted;r.txMayOwn=true;
        dma.memoryBarrier();
        if(dma.syncForDevice(a.tx)!=DMAStatus::Ok){r.status=Stage2GStatus::TXSyncFailed;return false;}
        if(!io.write8(0x38,0x40)){r.status=Stage2GStatus::TXKickFailed;return false;}
    }
    return true;
}

bool anyDeviceOwned(volatile uint32_t *ring) {
    if (!ring) return false;
    for (uint32_t i = 0; i < kStage2GRXDescriptorCount; ++i)
        if (ring[i * 4] & kDescOwn) return true;
    return false;
}

bool safeQuiesce(RegisterIO &io, DMAProvider &dma, Stage2AResult &stage2a,
                 Stage2GResult &result, bool deviceMayOwn) {
    const bool stopped = stopRX(io, result);
    if (stopped && !deviceMayOwn) return true;
    dma.memoryBarrier();
    const bool busMasterOff = dma.setBusMaster(false) == DMAStatus::Ok;
    if (busMasterOff) stage2a.busMasterEnabled = false;
    return busMasterOff;
}

Stage2GResult failBeforeOwn(Stage2GResult &result, Stage2GStatus status,
                            const char *stage, DMAProvider &dma,
                            Stage2AResult &stage2a,
                            volatile uint32_t *ring) {
    result.status = status;
    result.stage = stage;
    if (result.txRing) {
        for (unsigned i=0;i<kStage2GTXRingCount*4;++i) result.txRing[i]=0;
        dma.memoryBarrier(); dma.syncForDevice(stage2a.tx);
        result.txRing=nullptr;
    }
    clearDescriptors(ring);
    if (ring) {
        dma.memoryBarrier();
        dma.syncForDevice(stage2a.rx);
    }
    cleanupBuffers(dma, result);
    return result;
}

Stage2GResult failAfterOwn(Stage2GResult &result, Stage2GStatus status,
                           const char *stage, RegisterIO &io, DMAProvider &dma,
                           Stage2AResult &stage2a,
                           volatile uint32_t *ring) {
    result.status = status;
    result.stage = stage;
    const bool deviceMayOwn = anyDeviceOwned(ring) || result.txMayOwn;
    if (!safeQuiesce(io, dma, stage2a, result, deviceMayOwn)) {
        result.status = Stage2GStatus::SafeQuiesceFailed;
        result.stage = "rx-recycle-safe-quiesce";
        result.unsafeMappingRetained = true;
        return result;
    }
    if (result.txRing) {
        for (unsigned i=0;i<kStage2GTXRingCount*4;++i) result.txRing[i]=0;
        dma.memoryBarrier(); dma.syncForDevice(stage2a.tx);
        result.txRing=nullptr;
    }
    clearDescriptors(ring);
    dma.memoryBarrier();
    dma.syncForDevice(stage2a.rx);
    cleanupBuffers(dma, result);
    return result;
}
}

static Stage2GResult runRecycleSession(RegisterIO &io, DMAProvider &dma,
                         Stage2AResult &stage2a, PCIConfig *pci) {
    Stage2GResult result;
    if (!stage2a.complete || !stage2a.busMasterEnabled ||
        !stage2a.rx.mapped || !dma.dmaReady()) {
        result.stage = "stage2a-incomplete";
        return result;
    }

    result.stage = "rx-recycle-ring-verify";
    uint32_t low = 0, high = 0;
    if (!io.read32(kRXAddressLow, low) || !io.read32(kRXAddressHigh, high)) {
        result.status = Stage2GStatus::RingRegisterIOFailure;
        return result;
    }
    result.rxRingRegisterIOVA = joinAddress(high, low);
    if (stage2a.rx.segmentCount != 1 ||
        result.rxRingRegisterIOVA != stage2a.rx.segments[0].address) {
        result.status = Stage2GStatus::RingRegisterMismatch;
        return result;
    }

    auto *ringBytes = static_cast<uint8_t *>(dma.bytes(stage2a.rx));
    if (!ringBytes) {
        result.status = Stage2GStatus::DescriptorAccessFailed;
        result.stage = "rx-recycle-descriptor-access";
        return result;
    }
    auto *ring = reinterpret_cast<volatile uint32_t *>(ringBytes);
    if (result.txRing) {
        for (unsigned i=0;i<kStage2GTXRingCount*4;++i) result.txRing[i]=0;
        dma.memoryBarrier(); dma.syncForDevice(stage2a.tx);
        result.txRing=nullptr;
    }
    clearDescriptors(ring);

    for (uint32_t i = 0; i < kStage2GRXDescriptorCount; ++i) {
        result.stage = "rx-recycle-buffer-allocate";
        if (dma.allocate(result.rxBuffers[i], kStage2GRXBufferLength, 16,
                         DMADirection::FromDevice) != DMAStatus::Ok)
            return failBeforeOwn(result, Stage2GStatus::BufferAllocationFailed,
                                 result.stage, dma, stage2a, ring);
        result.stage = "rx-recycle-buffer-prepare";
        if (dma.prepare(result.rxBuffers[i]) != DMAStatus::Ok)
            return failBeforeOwn(result, Stage2GStatus::BufferPrepareFailed,
                                 result.stage, dma, stage2a, ring);
        result.stage = "rx-recycle-buffer-map";
        if (dma.map(result.rxBuffers[i]) != DMAStatus::Ok)
            return failBeforeOwn(result, Stage2GStatus::BufferMapFailed,
                                 result.stage, dma, stage2a, ring);
        auto &buffer = result.rxBuffers[i];
        result.bufferSegmentCount[i] = uint32_t(buffer.segmentCount);
        if (buffer.segmentCount != 1 ||
            buffer.mappedLength != kStage2GRXBufferLength ||
            buffer.segments[0].length != kStage2GRXBufferLength ||
            !buffer.segments[0].address ||
            buffer.segments[0].address > UINT64_MAX - (kStage2GRXBufferLength - 1))
            return failBeforeOwn(result, Stage2GStatus::BufferSegmentInvalid,
                                 "rx-recycle-buffer-segment", dma, stage2a,
                                 ring);
        result.bufferIOVA[i] = buffer.segments[0].address;
        if (!dma.bytes(buffer))
            return failBeforeOwn(result, Stage2GStatus::BufferAccessFailed,
                                 "rx-recycle-buffer-access", dma, stage2a,
                                 ring);
        if (dma.zero(buffer) != DMAStatus::Ok ||
            dma.syncForDevice(buffer) != DMAStatus::Ok)
            return failBeforeOwn(result, Stage2GStatus::BufferSyncFailed,
                                 "rx-recycle-buffer-device-sync", dma,
                                 stage2a, ring);
        ring[i * 4 + 2] = uint32_t(result.bufferIOVA[i]);
        ring[i * 4 + 3] = uint32_t(result.bufferIOVA[i] >> 32);
        ring[i * 4 + 1] = 0;
        ring[i * 4 + 0] = descriptorLength(i);
    }
    dma.memoryBarrier();
    if (dma.syncForDevice(stage2a.rx) != DMAStatus::Ok)
        return failBeforeOwn(result, Stage2GStatus::DescriptorSyncFailed,
                             "rx-recycle-descriptor-sync", dma, stage2a,
                             ring);

    result.stage = "duplex-tx-prepare";
    if (!prepareTX(io,dma,stage2a,result))
        return failBeforeOwn(result,Stage2GStatus::TXPrepareFailed,result.stage,dma,stage2a,ring);
    result.stage = "rx-recycle-configure";
    if (!configureRX(io, result))
        return failBeforeOwn(result, Stage2GStatus::RXConfigurationFailed,
                             result.stage, dma, stage2a, ring);
    if (!io.read8(kChipCommand, result.chipCommandBeforeRX) ||
        (result.chipCommandBeforeRX & kTXRXEnable))
        return failBeforeOwn(result, Stage2GStatus::RXEnableFailed,
                             "rx-recycle-command-precheck", dma, stage2a,
                             ring);

    for (uint32_t i = 0; i < kStage2GRXDescriptorCount; ++i)
        ring[i * 4] = descriptorLength(i) | kDescOwn;
    dma.memoryBarrier();
    if (dma.syncForDevice(stage2a.rx) != DMAStatus::Ok)
        return failAfterOwn(result, Stage2GStatus::DescriptorSyncFailed,
                            "rx-recycle-own-sync", io, dma, stage2a, ring);

    result.stage = "rx-recycle-enable";
    if (!io.write8(kChipCommand, kCmdRXEnable) ||
        !io.read8(kChipCommand, result.chipCommandDuringRX) ||
        result.chipCommandDuringRX != kCmdRXEnable)
        return failAfterOwn(result, Stage2GStatus::RXEnableFailed,
                            result.stage, io, dma, stage2a, ring);

    result.combinedEngineObserved = result.chipCommandDuringRX==12;
    snapshotRX(io, pci, ring, result, true);
    result.stage = "rx-recycle-poll";
    for (uint32_t attempt = 1; attempt <= kStage2GRXPollAttempts; ++attempt) {
        io.delayUs(kStage2GRXPollIntervalUs);
        if (dma.syncForCPU(stage2a.rx) != DMAStatus::Ok)
            return failAfterOwn(result, Stage2GStatus::PollSyncFailed,
                                "rx-recycle-poll-sync", io, dma, stage2a,
                                ring);
        dma.memoryBarrier();
        result.pollCount = attempt;
        if (attempt == 1 || attempt % 100 == 0)
            snapshotRX(io, pci, ring, result, false);
        if (!serviceTX(io,dma,stage2a,result,attempt))
            return failAfterOwn(result,result.status,"duplex-tx-service",io,dma,stage2a,ring);
        bool rearmed = false;
        for (uint32_t i = 0; i < kStage2GRXDescriptorCount; ++i) {
            if (result.completionCount[i] >= kStage2GRXCycles) continue;
            const uint32_t opts1 = ring[i * 4];
            if (opts1 & kDescOwn) continue;
            const uint32_t errorBits = opts1 & kRXErrorMask;
            const uint32_t length = opts1 & kRXLengthMask;
            result.lastDescriptorOpts1[i] = opts1;
            result.lastDescriptorErrorBits[i] = errorBits;
            const bool valid = (opts1 & (kFirstFrag | kLastFrag)) ==
                                   (kFirstFrag | kLastFrag) &&
                               !errorBits && length >= 4 &&
                               length <= kStage2GRXBufferLength;
            if (!valid)
                return failAfterOwn(result, Stage2GStatus::RXDescriptorInvalid,
                                    "rx-recycle-descriptor-result", io, dma,
                                    stage2a, ring);
            if (dma.syncForCPU(result.rxBuffers[i]) != DMAStatus::Ok)
                return failAfterOwn(result, Stage2GStatus::RXBufferSyncFailed,
                                    "rx-recycle-buffer-cpu-sync", io, dma,
                                    stage2a, ring);
            ++result.completionCount[i];
            ++result.completedCount;
            if (!result.firstCompletionPoll) result.firstCompletionPoll = attempt;
            result.packetDMAExercised = true;
            if (result.completionCount[i] < kStage2GRXCycles) {
                result.firstCycleMask |= 1U << i;
                if (result.firstCycleMask == 15 && !result.firstCycleCompletePoll)
                    result.firstCycleCompletePoll = attempt;
                if (result.completionCount[i]==1) result.firstReceivedLength[i] = length;
                if (dma.zero(result.rxBuffers[i]) != DMAStatus::Ok ||
                    dma.syncForDevice(result.rxBuffers[i]) != DMAStatus::Ok)
                    return failAfterOwn(result, Stage2GStatus::RXRearmFailed,
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
                return failAfterOwn(result, Stage2GStatus::RXRearmFailed,
                                    "rx-recycle-descriptor-rearm", io, dma,
                                    stage2a, ring);
        }
        if (result.completedCount == kStage2GRXTargetCount && result.txCompleted==kStage2GTXTarget) break;
    }
    snapshotRX(io, pci, ring, result, false);
    if (result.completedCount != kStage2GRXTargetCount || result.txCompleted!=kStage2GTXTarget) {
        result.timeout = true;
        return failAfterOwn(result, result.txCompleted!=kStage2GTXTarget?Stage2GStatus::TXTimeout:Stage2GStatus::RXTimeout,
                            "rx-recycle-timeout", io, dma, stage2a, ring);
    }

    if (result.rxCommandLostCount || result.busMasterLostCount)
        return failAfterOwn(result, Stage2GStatus::RXEnableFailed,
                            "duplex-engine-lost", io, dma, stage2a, ring);
    result.stage = "rx-recycle-disable";
    if (!stopRX(io, result))
        return failAfterOwn(result, Stage2GStatus::RXDisableFailed,
                            result.stage, io, dma, stage2a, ring);
    if (result.txRing) {
        for (unsigned i=0;i<kStage2GTXRingCount*4;++i) result.txRing[i]=0;
        dma.memoryBarrier(); dma.syncForDevice(stage2a.tx);
        result.txRing=nullptr;
    }
    clearDescriptors(ring);
    dma.memoryBarrier();
    if (dma.syncForDevice(stage2a.rx) != DMAStatus::Ok) {
        cleanupBuffers(dma, result);
        result.status = Stage2GStatus::DescriptorCleanupFailed;
        result.stage = "rx-recycle-descriptor-cleanup";
        return result;
    }
    cleanupBuffers(dma, result);
    result.status = Stage2GStatus::Ok;
    result.stage = "stage2g-complete";
    result.complete = true;
    return result;
}

Stage2GResult runStage2G(RegisterIO &io, DMAProvider &dma,
                         Stage2AResult &stage2a,
                         const Stage2DResult &stage2d, PCIConfig *pci) {
    if (!stage2d.complete) {
        Stage2GResult result; result.stage = "stage2d-incomplete";
        return result;
    }
    return runRecycleSession(io, dma, stage2a, pci);
}

Stage2GResult runStage2GFromC(RegisterIO &io, DMAProvider &dma,
                            Stage2AResult &stage2a,
                            const Stage2CResult &stage2c, PCIConfig *pci) {
    if (!stage2c.complete) {
        Stage2GResult result;
        result.status = Stage2GStatus::Stage2CIncomplete;
        result.stage = "stage2c-incomplete";
        return result;
    }
    return runRecycleSession(io, dma, stage2a, pci);
}

Stage2GResult runStage2GFromA(RegisterIO &io, DMAProvider &dma,
                            Stage2AResult &stage2a, PCIConfig *pci) {
    Stage2GResult result;
    if (!stage2a.complete || !stage2a.busMasterEnabled || !dma.dmaReady()) {
        result.stage="stage2a-incomplete";return result;
    }
    RegisterAccess access(io);
    for (unsigned attempt=1;attempt<=kStage2BLinkPollAttempts;++attempt) {
        access.restorePHYBase();
        auto first=access.mdioRead(1);auto second=access.mdioRead(1);
        if(first.status!=AccessStatus::Ok || second.status!=AccessStatus::Ok){
            result.stage="duplex-link-read";result.status=Stage2GStatus::LinkReadFailed;return result;
        }
        result.linkPollCount=attempt;
        if(second.value&4){
            auto session=runRecycleSession(io,dma,stage2a,pci);
            session.linkPollCount=attempt;return session;
        }
        if(attempt!=kStage2BLinkPollAttempts)io.delayUs(kStage2BLinkPollIntervalUs);
    }
    result.stage="duplex-link-down";result.status=Stage2GStatus::LinkDown;return result;
}

bool teardownStage2G(RegisterIO &io, DMAProvider &dma, Stage2AResult &stage2a,
                     Stage2GResult &result) {
    bool hasBuffer = result.txBuffer.opaque || result.txBuffer.allocated;
    for (uint32_t i = 0; i < kStage2GRXDescriptorCount; ++i)
        hasBuffer = hasBuffer || result.rxBuffers[i].opaque ||
                    result.rxBuffers[i].allocated;
    if (!hasBuffer) return true;
    auto *bytes = static_cast<uint8_t *>(dma.bytes(stage2a.rx));
    auto *ring = bytes ? reinterpret_cast<volatile uint32_t *>(bytes) : nullptr;
    if (!safeQuiesce(io, dma, stage2a, result, (anyDeviceOwned(ring) || result.txMayOwn))) {
        result.unsafeMappingRetained = true;
        return false;
    }
    if (result.txRing) {
        for (unsigned i=0;i<kStage2GTXRingCount*4;++i) result.txRing[i]=0;
        dma.memoryBarrier(); dma.syncForDevice(stage2a.tx);
        result.txRing=nullptr;
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
