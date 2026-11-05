// SPDX-License-Identifier: GPL-2.0-only
#include "DMAProgram.hpp"

namespace realtek_next {
namespace {
constexpr uint16_t kChipCommand = 0x37;
constexpr uint16_t kInterruptMask = 0x3c;
constexpr uint16_t kTXAddressLow = 0x20;
constexpr uint16_t kTXAddressHigh = 0x24;
constexpr uint16_t kRXAddressLow = 0xe4;
constexpr uint16_t kRXAddressHigh = 0xe8;
constexpr uint8_t kTXRXEnable = 0x0c;

DMAStatus validateRing(const DMABuffer &buffer, uint64_t length) {
    if (buffer.segmentCount != 1 || buffer.mappedLength != length ||
        buffer.segments[0].length != length)
        return DMAStatus::SegmentCountInvalid;
    const uint64_t address = buffer.segments[0].address;
    if (!address) return DMAStatus::InvalidState;
    if (address & (kDescriptorRingAlignment - 1))
        return DMAStatus::AlignmentInvalid;
    if (length == 0 || address > UINT64_MAX - (length - 1))
        return DMAStatus::RangeOverflow;
    return DMAStatus::Ok;
}

DMAStatus buildRing(DMAProvider &dma, DMABuffer &buffer, uint64_t length,
                    const char *allocateStage, const char *prepareStage,
                    const char *mapStage, const char *zeroStage,
                    const char *syncStage, Stage2AResult &result) {
    result.stage = allocateStage;
    auto status = dma.allocate(buffer, length, kDescriptorRingAlignment,
                               DMADirection::Bidirectional);
    if (status != DMAStatus::Ok) return status;
    result.stage = prepareStage;
    status = dma.prepare(buffer);
    if (status != DMAStatus::Ok) return status;
    result.stage = mapStage;
    status = dma.map(buffer);
    if (status != DMAStatus::Ok) return status;
    status = validateRing(buffer, length);
    if (status != DMAStatus::Ok) return status;
    result.stage = zeroStage;
    status = dma.zero(buffer);
    if (status != DMAStatus::Ok) return status;
    result.stage = syncStage;
    return dma.syncForDevice(buffer);
}

bool writeAndVerify(RegisterIO &io, uint16_t offset, uint32_t value) {
    uint32_t readback = 0;
    return io.write32(offset, value) && io.read32(offset, readback) && readback == value;
}
}

void teardownStage2A(RegisterIO &io, DMAProvider &dma, Stage2AResult &result) {
    if(result.stopSafetyRetained){
        result.complete=false;result.status=DMAStatus::InvalidState;result.stage="STOPSAFE1-retained";return;
    }
    if (result.busMasterEnabled || result.unsafeMappingRetained) {
        if (dma.setBusMaster(false) != DMAStatus::Ok) {
            result.unsafeMappingRetained = true;
            result.complete = false;
            result.status = DMAStatus::BusMasterFailed;
            result.stage = "bme-off-unconfirmed-retain";
            return; // Never clear addresses or release mappings while DMA may run.
        }
        result.busMasterEnabled = false;
        result.unsafeMappingRetained = false;
    }
    // Stop traffic first and remove stale descriptor addresses before unmapping.
    io.write16(kInterruptMask, 0);
    io.write8(kChipCommand, 0);
    io.write32(kRXAddressHigh, 0);
    io.write32(kRXAddressLow, 0);
    io.write32(kTXAddressHigh, 0);
    io.write32(kTXAddressLow, 0);
    if (result.rx.mapped) dma.unmap(result.rx);
    if (result.rx.prepared) dma.complete(result.rx);
    if (result.rx.allocated || result.rx.opaque) dma.release(result.rx);
    if (result.tx.mapped) dma.unmap(result.tx);
    if (result.tx.prepared) dma.complete(result.tx);
    if (result.tx.allocated || result.tx.opaque) dma.release(result.tx);
    result.complete = false;
}

Stage2AResult runStage2A(RegisterIO &io, DMAProvider &dma, unsigned txCount) {
    Stage2AResult result;
    result.stage = "tx-count-validation";
    if(!txDescriptorCountValid(txCount))return result;
    result.txDescriptorCount=txCount;
    result.stage = "mapper-check";
    result.mapperPresent = dma.deviceMapperPresent();
    if (!dma.dmaReady()) {
        result.status = DMAStatus::MapperMissing;
        return result;
    }

    result.status = buildRing(dma, result.tx, uint64_t(kDescriptorSize)*txCount,
                              "tx-allocate", "tx-prepare", "tx-map",
                              "tx-zero", "tx-sync", result);
    if (result.status != DMAStatus::Ok) {
        teardownStage2A(io, dma, result);
        return result;
    }
    result.status = buildRing(dma, result.rx, kRXRingLength,
                              "rx-allocate", "rx-prepare", "rx-map",
                              "rx-zero", "rx-sync", result);
    if (result.status != DMAStatus::Ok) {
        teardownStage2A(io, dma, result);
        return result;
    }

    result.stage = "register-program";
    uint8_t command = 0xff;
    if (!io.write16(kInterruptMask, 0) || !io.write8(kChipCommand, 0) ||
        !io.read8(kChipCommand, command) || (command & kTXRXEnable)) {
        result.status = DMAStatus::RegisterIOFailure;
        teardownStage2A(io, dma, result);
        return result;
    }
    const uint64_t tx = result.tx.segments[0].address;
    const uint64_t rx = result.rx.segments[0].address;
    if (!writeAndVerify(io, kTXAddressLow, uint32_t(tx)) ||
        !writeAndVerify(io, kTXAddressHigh, uint32_t(tx >> 32)) ||
        !writeAndVerify(io, kRXAddressLow, uint32_t(rx)) ||
        !writeAndVerify(io, kRXAddressHigh, uint32_t(rx >> 32))) {
        result.status = DMAStatus::RegisterVerificationFailed;
        teardownStage2A(io, dma, result);
        return result;
    }

    result.stage = "bus-master";
    // A failed readback can follow a successful PCI write: assume BME until proven OFF.
    result.busMasterEnabled = true;
    result.status = dma.setBusMaster(true);
    if (result.status != DMAStatus::Ok) {
        teardownStage2A(io, dma, result);
        return result;
    }
    if (!io.read8(kChipCommand, command) || (command & kTXRXEnable)) {
        result.status = DMAStatus::RegisterVerificationFailed;
        teardownStage2A(io, dma, result);
        return result;
    }

    result.status = DMAStatus::Ok;
    result.stage = "stage2a-complete";
    result.complete = true;
    return result;
}

}
