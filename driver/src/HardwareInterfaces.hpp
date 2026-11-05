// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <stdint.h>
namespace realtek_next {
class DelayProvider { public: virtual ~DelayProvider() = default; virtual void delayUs(unsigned us)=0; };
class PCIConfig {
public:
 virtual ~PCIConfig()=default;
 virtual bool readConfig8(uint16_t, uint8_t&)=0;
 virtual bool readConfig16(uint16_t, uint16_t&)=0;
 virtual bool readConfig32(uint16_t, uint32_t&)=0;
 virtual bool writeConfig8(uint16_t, uint8_t)=0;
 virtual bool writeConfig16(uint16_t, uint16_t)=0;
 virtual bool writeConfig32(uint16_t, uint32_t)=0;
};
}
