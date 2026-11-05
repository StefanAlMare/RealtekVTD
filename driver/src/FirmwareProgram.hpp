// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <stddef.h>
#include "RegisterAccess.hpp"
namespace realtek_next {
class FirmwareTarget {
public:
 virtual ~FirmwareTarget()=default;
 virtual Read16 read(unsigned reg)=0;
 virtual AccessStatus write(unsigned reg,uint16_t data)=0;
 virtual void restore()=0;
};
class PHYFirmwareTarget : public FirmwareTarget {
 RegisterAccess &r;
public:
 explicit PHYFirmwareTarget(RegisterAccess &access):r(access){}
 Read16 read(unsigned reg) override { return r.mdioRead(reg); }
 AccessStatus write(unsigned reg,uint16_t data) override { return r.mdioWrite(reg,data); }
 void restore() override { r.restorePHYBase(); }
};
class MACFirmwareTarget : public FirmwareTarget {
 RegisterAccess &r;
public:
 explicit MACFirmwareTarget(RegisterAccess &access):r(access){}
 Read16 read(unsigned reg) override;
 AccessStatus write(unsigned reg,uint16_t data) override;
 void restore() override { r.restorePHYBase(); }
 uint32_t currentBase() const { return r.currentPHYBase(); }
};
// Non-owning immutable image: bytes must remain valid through execution.
class FirmwareProgram {
 const uint8_t *bytes=nullptr; size_t start=0,count=0;
 uint32_t instruction(size_t pc) const;
public:
 AccessStatus parse(const uint8_t *image,size_t size);
 AccessStatus execute(FirmwareTarget &phy,FirmwareTarget &mac,DelayProvider &delay,
                      size_t maxSteps=1000000,uint64_t maxDelayUs=10000000) const;
 size_t instructionCount() const { return count; }
};
}
