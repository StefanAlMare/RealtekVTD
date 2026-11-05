// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IODMACommand.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOMapper.h>
#include <sys/kpi_mbuf.h>
#include "../src/HardwareInterfaces.hpp"
#include "../src/RegisterAccess.hpp"
#include "../src/DMAProgram.hpp"
#include "../src/TXPool.hpp"
#include "../src/NetworkResources.hpp"
#include "../src/DMAMode.hpp"
class MacOSBackend final : public realtek_next::RegisterIO,public realtek_next::PCIConfig,
                           public realtek_next::DMAProvider {
 IOPCIDevice *pci=nullptr;IOMemoryMap *mapping=nullptr;
 IOMapper *mapper=nullptr;
 realtek_next::DMAMode dmaMode=realtek_next::DMAMode::Unavailable;
 realtek_next::TXPool txPool;
 // Startup-scoped permission; never enabled for ordinary packet mapping/allocation.
 bool gu25JumboAllocationWindow=false,stopSafetyRetained=false;
 volatile uint8_t *base=nullptr;size_t length=0;uint16_t savedCommand=0,savedPMCSR=0;
 uint8_t pmOffset=0;bool commandSaved=false,pmSaved=false;
 bool valid(uint16_t offset,unsigned width) const;
 bool permittedWrite(uint16_t offset,unsigned width,uint32_t value) const;
public:
 bool attach(IOPCIDevice *device);
 bool initializeDMA();
 bool dmaReady() const override{return dmaMode!=realtek_next::DMAMode::Unavailable;}
 const char *dmaModeName() const{return dmaMode==realtek_next::DMAMode::DeviceMapper?"device-mapper-native64":dmaMode==realtek_next::DMAMode::NoIOMMU?"no-iommu-native64":"unavailable";}
 // Completed no-mapper selection, never inferred from mapper==nullptr alone.
 // initializeDMA retains pending/system/parent/AppleVTD rejection unchanged.
 bool native64NoIOMMUReady()const{return dmaMode==realtek_next::DMAMode::NoIOMMU&&mapper==nullptr&&!stopSafetyRetained;}
 bool ensureD0();
 bool initializeTXPool(){return txPool.initialize(*this);}
 realtek_next::NetworkResources initializeNetworkResources(realtek_next::Stage2AResult &a,realtek_next::NetworkResult &n,bool jumbo){
  if(gu25JumboAllocationWindow)return {}; // never nest/overlap resource preparation
  const bool extended=realtek_next::gu25JumboResourceShape(jumbo,n.macVersion,a.txDescriptorCount);
  struct WindowReset {bool &flag;~WindowReset(){flag=false;}} reset{gu25JumboAllocationWindow};
  gu25JumboAllocationWindow=extended;
  return realtek_next::prepareNetworkResources(*this,a,n,txPool,jumbo,extended);
 }
 realtek_next::DMAStatus snapshotTX(realtek_next::DMABuffer &b,size_t n,realtek_next::TXCopy copy,void *context){return txPool.snapshot(b,n,copy,context);}
 void retainStopSafety(){stopSafetyRetained=true;}
 bool releaseTXPool();
 bool ownsTXSnapshot(const realtek_next::DMABuffer &b)const{return txPool.owns(b);}
 unsigned txPoolCount()const{return txPool.count();}
 unsigned txPoolLeased()const{return txPool.leased();}
 uint64_t txPoolIOVA()const{return txPool.iova();}
 unsigned txPoolStride()const{return txPool.slotStride();}
 realtek_next::DMAStatus mapPacket(realtek_next::DMABuffer &,mbuf_t,realtek_next::JTXCopyEvidence *evidence=nullptr);
 void releaseDMAProvider();
 void detach();
 bool read8(uint16_t,uint8_t&) override;
 bool read16(uint16_t,uint16_t&) override;
 bool read32(uint16_t,uint32_t&) override;
 bool write8(uint16_t,uint8_t) override;
 bool write16(uint16_t,uint16_t) override;
 bool write32(uint16_t,uint32_t) override;
 void delayUs(unsigned us) override;
 bool readConfig8(uint16_t,uint8_t&) override;
 bool readConfig16(uint16_t,uint16_t&) override;
 bool readConfig32(uint16_t,uint32_t&) override;
 bool writeConfig8(uint16_t,uint8_t) override;
 bool writeConfig16(uint16_t,uint16_t) override;
 bool writeConfig32(uint16_t,uint32_t) override;
 bool deviceMapperPresent() const override { return mapper != nullptr; }
 realtek_next::DMAStatus allocate(realtek_next::DMABuffer &,uint64_t,uint32_t,
                                  realtek_next::DMADirection) override;
 realtek_next::DMAStatus prepare(realtek_next::DMABuffer &) override;
 realtek_next::DMAStatus map(realtek_next::DMABuffer &) override;
 realtek_next::DMAStatus zero(realtek_next::DMABuffer &) override;
 void *bytes(realtek_next::DMABuffer &) override;
 realtek_next::DMAStatus syncForDevice(realtek_next::DMABuffer &) override;
 realtek_next::DMAStatus syncForCPU(realtek_next::DMABuffer &) override;
 void memoryBarrier() override;
 void unmap(realtek_next::DMABuffer &) override;
 void complete(realtek_next::DMABuffer &) override;
 void release(realtek_next::DMABuffer &) override;
 realtek_next::DMAStatus setBusMaster(bool enable) override;
};
