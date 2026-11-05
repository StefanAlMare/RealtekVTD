// SPDX-License-Identifier: GPL-2.0-only
#pragma once
namespace realtek_next {
enum class DMAMode { Unavailable, DeviceMapper, NoIOMMU };
inline DMAMode selectDMAMode(bool deviceMapper,bool systemMapperOrPending,bool iommuParent,bool appleVTD){
 if(deviceMapper)return DMAMode::DeviceMapper;
 if(systemMapperOrPending||iommuParent||appleVTD)return DMAMode::Unavailable;
 return DMAMode::NoIOMMU;
}
}
