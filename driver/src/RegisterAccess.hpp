// SPDX-License-Identifier: GPL-2.0-only
// Hardware protocol reference: Linux 3cdeaef1754ab8155cdd828c958ada3d36e9ad9a.
#pragma once
#include "RegisterWaitDiagnostic.hpp"
#include <stdint.h>
#include "HardwareInterfaces.hpp"
namespace realtek_next {
enum class AccessStatus { Ok, InvalidAddress, IOFailure, Timeout, InvalidFirmware, BudgetExceeded, Unsupported, VerificationFailed };
struct Read32 { AccessStatus status; uint32_t value; };
struct Read16 { AccessStatus status; uint16_t value; };
// Backend supplies ordered host-endian MMIO and delay. Caller must serialize
// all transactions, including page selection, on one device workloop/lock.
class RegisterIO : public DelayProvider {
public:
    virtual ~RegisterIO() = default;
    virtual bool read8(uint16_t, uint8_t&) { return false; }
    virtual bool read16(uint16_t, uint16_t&) { return false; }
    virtual bool write8(uint16_t, uint8_t) { return false; }
    virtual bool write16(uint16_t, uint16_t) { return false; }
    virtual bool read32(uint16_t offset, uint32_t &value) = 0;
    virtual bool write32(uint16_t offset, uint32_t value) = 0;
    virtual void delayUs(unsigned us) = 0;
};
class RegisterAccess {
    RegisterIO &io;
    DelayProvider &clock;
    uint32_t phyBase = 0xa400;
    bool legacyPHYAR = false;
    AccessStatus restoreFailure = AccessStatus::Ok;
    RegisterWaitDiagnostic *waitDiagnostic = nullptr;
    AccessStatus legacyWrite(unsigned reg, uint16_t value);
    Read16 legacyRead(unsigned reg);
    Read16 legacyMMDRead(unsigned device, uint16_t reg);
    AccessStatus legacyMMDWrite(unsigned device, uint16_t reg, uint16_t value);
    static constexpr uint32_t flag = 0x80000000U;
    static bool valid(uint32_t address) { return !(address & 0xffff0001U); }
    AccessStatus waitPHY(bool high,uint32_t address,uint32_t command) {
        if(waitDiagnostic){waitDiagnostic->begin("PHY-OCP",address,0xb8,flag,high?flag:0,10,25);waitDiagnostic->commandAvailable=true;waitDiagnostic->command=command;}
        for (unsigned i = 0; i < 10; ++i) {
            if(waitDiagnostic)waitDiagnostic->attempts=i+1;
            uint32_t value;
            if (!io.read32(0xb8, value)){if(waitDiagnostic)waitDiagnostic->status=unsigned(AccessStatus::IOFailure);return AccessStatus::IOFailure;}
            if(waitDiagnostic)waitDiagnostic->sample(value);
            if (value == 0xffffffffU){if(waitDiagnostic)waitDiagnostic->status=unsigned(AccessStatus::IOFailure);return AccessStatus::IOFailure;}
            if (bool(value & flag) == high) return AccessStatus::Ok;
            clock.delayUs(25);
        }
        if(waitDiagnostic)waitDiagnostic->status=unsigned(AccessStatus::Timeout);
        return AccessStatus::Timeout;
    }
    bool mdioAddress(unsigned reg, uint32_t &address) const {
        if (reg > 30) return false;
        const int relative = phyBase == 0xa400 ? int(reg) : int(reg) - 16;
        const int64_t candidate = int64_t(phyBase) + relative * 2;
        if (candidate < 0 || candidate > 0xffff) return false;
        address = uint32_t(candidate);
        return valid(address);
    }
public:
    explicit RegisterAccess(RegisterIO &backend, unsigned macVersion=44) : io(backend), clock(backend), legacyPHYAR(macVersion==34) {}
    RegisterAccess(RegisterIO &backend, DelayProvider &timing) : io(backend), clock(timing) {}
    RegisterIO &mmio() { return io; }
    RegisterWaitDiagnostic *observeWaits(RegisterWaitDiagnostic *next){auto old=waitDiagnostic;waitDiagnostic=next;return old;}
    void delay(unsigned us) { clock.delayUs(us); }
    void restorePHYBase() {
        phyBase = 0xa400;
        if(legacyPHYAR && restoreFailure==AccessStatus::Ok) restoreFailure=legacyWrite(31,0);
    }
    bool usesLegacyPHYAR() const { return legacyPHYAR; }
    AccessStatus restoreStatus() const { return restoreFailure; }
    uint32_t currentPHYBase() const { return phyBase; }
    void setFirmwareOCPBase(uint32_t base) { phyBase = base; }
    AccessStatus ephyWrite(unsigned reg, uint16_t data);
    Read16 ephyRead(unsigned reg);
    AccessStatus ephyModify(unsigned reg, uint16_t clear, uint16_t set);
    AccessStatus eriWrite(unsigned address, uint8_t mask, uint32_t data);
    Read32 eriRead(unsigned address);
    AccessStatus eriModify(unsigned address, uint32_t clear, uint32_t set);
    AccessStatus eriSetBits(unsigned address, uint32_t bits) { return eriModify(address,0,bits); }
    AccessStatus eriClearBits(unsigned address, uint32_t bits) { return eriModify(address,bits,0); }
    AccessStatus phyModify(unsigned address, uint16_t clear, uint16_t set);
    Read16 pagedRead(uint16_t page, unsigned reg);
    AccessStatus pagedWrite(uint16_t page, unsigned reg, uint16_t data);
    AccessStatus pagedModify(uint16_t page, unsigned reg, uint16_t clear, uint16_t set);
    AccessStatus phyParameter(uint16_t parameter, uint16_t clear, uint16_t set);
    AccessStatus wait32(uint16_t offset, uint32_t mask, bool high, unsigned interval, unsigned attempts);
    AccessStatus macWrite(uint32_t address, uint16_t data) {
        if (!valid(address)) return AccessStatus::InvalidAddress;
        return io.write32(0xb0, flag | (address << 15) | data)
            ? AccessStatus::Ok : AccessStatus::IOFailure;
    }
    Read16 macRead(uint32_t address) {
        if (!valid(address)) return {AccessStatus::InvalidAddress, 0};
        uint32_t value;
        if (!io.write32(0xb0, address << 15) || !io.read32(0xb0, value)
            || value == 0xffffffffU) return {AccessStatus::IOFailure, 0};
        return {AccessStatus::Ok, uint16_t(value)};
    }
    AccessStatus macModify(uint32_t address, uint16_t clear, uint16_t set) {
        const auto r = macRead(address);
        if (r.status != AccessStatus::Ok) return r.status;
        return macWrite(address, uint16_t((r.value & ~clear) | set));
    }
    AccessStatus phyWrite(uint32_t address, uint16_t data) {
        if(legacyPHYAR) {
            if(address==0xa5d0) return legacyMMDWrite(7,60,data);
            if(address>=0xa400 && address<=0xa43c && !(address&1)) return pagedWrite(0,(address-0xa400)/2,data);
            return AccessStatus::Unsupported;
        }
        if (!valid(address)) return AccessStatus::InvalidAddress;
        if (!io.write32(0xb8, flag | (address << 15) | data)) return AccessStatus::IOFailure;
        return waitPHY(false,address,flag | (address << 15) | data);
    }
    Read16 phyRead(uint32_t address) {
        if(legacyPHYAR) {
            if(address==0xa5d0) return legacyMMDRead(7,60);
            if(address>=0xa400 && address<=0xa43c && !(address&1)) return pagedRead(0,(address-0xa400)/2);
            return {AccessStatus::Unsupported,0};
        }
        if (!valid(address)) return {AccessStatus::InvalidAddress, 0};
        if (!io.write32(0xb8, address << 15)) return {AccessStatus::IOFailure, 0};
        const auto status = waitPHY(true,address,address << 15);
        if (status != AccessStatus::Ok) return {status, 0};
        uint32_t value;
        if (!io.read32(0xb8, value) || value == 0xffffffffU)
            return {AccessStatus::IOFailure, 0};
        return {AccessStatus::Ok, uint16_t(value)};
    }
    // MAC34 uses PHYAR. The original MAC44/46 OCP path is unchanged.
    AccessStatus mdioWrite(unsigned reg, uint16_t value) {
        if(legacyPHYAR) return restoreFailure==AccessStatus::Ok?legacyWrite(reg,value):restoreFailure;
        if (reg == 31) {
            if (value > 0xfff) return AccessStatus::InvalidAddress;
            phyBase = value ? uint32_t(value) << 4 : 0xa400;
            return AccessStatus::Ok;
        }
        uint32_t address;
        return mdioAddress(reg, address) ? phyWrite(address, value) : AccessStatus::InvalidAddress;
    }
    Read16 mdioRead(unsigned reg) {
        if(legacyPHYAR) return restoreFailure==AccessStatus::Ok?legacyRead(reg):Read16{restoreFailure,0};
        if (reg == 31) return {AccessStatus::Ok, uint16_t(phyBase == 0xa400 ? 0 : phyBase >> 4)};
        uint32_t address;
        return mdioAddress(reg, address) ? phyRead(address) : Read16{AccessStatus::InvalidAddress, 0};
    }
};
}
