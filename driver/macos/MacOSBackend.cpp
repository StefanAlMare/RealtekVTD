// SPDX-License-Identifier: GPL-2.0-only
#include "MacOSBackend.hpp"
#include <libkern/c++/OSIterator.h>
#include "../src/TXSnapshot.hpp"
#include <IOKit/IOLib.h>
using realtek_next::DMABuffer;
using realtek_next::DMADirection;
using realtek_next::DMAStatus;
namespace {
struct NativeDMABuffer {
 IOMemoryDescriptor *memory=nullptr;
 bool memoryPrepared=false;
 IODMACommand *command=nullptr;
 bool descriptorSet=false;
 bool commandPrepared=false;
};
static NativeDMABuffer *native(DMABuffer &buffer){return static_cast<NativeDMABuffer*>(buffer.opaque);}
}
static inline void ioFence(){__asm__ __volatile__("mfence":::"memory");}
bool MacOSBackend::valid(uint16_t off,unsigned width) const {return base&&off<=length&&width<=length-off&&!(off&(width-1));}
bool MacOSBackend::permittedWrite(uint16_t off,unsigned width,uint32_t value) const {
 if(!valid(off,width))return false;
 const bool descriptorRegister=width==4&&(off==0x20||off==0x24||off==0xe4||off==0xe8);
 if(!descriptorRegister&&((off<0x28&&off+width>0x20)||(off<0xec&&off+width>0xe4)))return false;
 // STAGE2F permits combined TX/RX only through an exact byte write.
 // Wider writes carrying CmdRxEnb remain prohibited.
 if(off<=0x37&&off+width>0x37){
  const uint8_t command=uint8_t(value>>(8*(0x37-off)));
  if((command&0x08)&&!(off==0x37&&width==1&&(command==0x08||command==0x0c)))return false;
 }
 if(off<=0x3c&&off+width>0x3c&&value&&!(off==0x3c&&width==2&&value==0x000f))return false;
 return true;
}
bool MacOSBackend::attach(IOPCIDevice *device){
 pci=device;savedCommand=pci->configRead16(UInt8(4));commandSaved=true;
 // Stage 1 explicitly excludes all device DMA, including inherited BME.
 pci->configWrite16(UInt8(4),uint16_t((savedCommand|2)&~4));
 if((pci->configRead16(UInt8(4))&6)!=2){detach();return false;}
 pci->findPCICapability(kIOPCIPowerManagementCapability,&pmOffset);
 if(pmOffset){savedPMCSR=pci->configRead16(UInt8(pmOffset+4));pmSaved=true;
  pci->configWrite16(UInt8(pmOffset+4),uint16_t(savedPMCSR&~0x8003));IOSleep(10);
  if(pci->configRead16(UInt8(pmOffset+4))&3){detach();return false;}}
 for(unsigned i=0;i<6;++i){const UInt8 reg=UInt8(0x10+4*i);UInt32 bar=pci->configRead32(reg);
  if((bar&1)==0&&(bar&~15U)){auto map=pci->mapDeviceMemoryWithRegister(reg,kIOMapInhibitCache);
   if(map){if(map->getLength()>=256){mapping=map;break;}map->release();}}
  if(!(bar&1)&&((bar>>1)&3)==2)++i;
 }
 if(!mapping){detach();return false;}
 base=reinterpret_cast<volatile uint8_t*>(mapping->getVirtualAddress());length=mapping->getLength();return base;
}
bool MacOSBackend::ensureD0(){
 if(stopSafetyRetained)return false;
 if(!pci||pci->configRead16(UInt8(0))!=0x10ec)return false;
 if(pmOffset){const uint16_t value=pci->configRead16(UInt8(pmOffset+4));
  if(value==0xffff)return false;
  pci->configWrite16(UInt8(pmOffset+4),uint16_t(value&~0x8003U));IOSleep(10);
  if(pci->configRead16(UInt8(pmOffset+4))&3)return false;
 }
 const uint16_t command=pci->configRead16(UInt8(4));
 pci->configWrite16(UInt8(4),uint16_t((command|2U)&~4U));
 return (pci->configRead16(UInt8(4))&6U)==2U;
}
bool MacOSBackend::initializeDMA(){
 if(!pci)return false;if(dmaReady())return true;
 mapper=IOMapper::copyMapperForDevice(pci);
 if(mapper){dmaMode=realtek_next::DMAMode::DeviceMapper;return true;}
 // A nonzero gSystem includes unresolved/pending sentinel states: never fall back then.
 // An AppleVTD service without a per-device mapper also remains an error.
 auto services=IORegistryIterator::iterateOver(gIOServicePlane,kIORegistryIterateRecursively);
 if(!services)return false;
 bool appleVTD=false;
 while(auto entry=services->getNextObject())if(entry->metaCast("AppleVTD")){appleVTD=true;break;}
 const bool registryValid=services->isValid();services->release();
 if(!registryValid)return false;
 const bool systemMapperOrPending=IOMapper::gSystem!=nullptr;
 const bool iommuParent=pci->getProperty("iommu-parent")!=nullptr;
 dmaMode=realtek_next::selectDMAMode(false,systemMapperOrPending,iommuParent,appleVTD);
 IOLog("RealtekVTDNext DMA policy: system-or-pending=%u iommu-parent=%u AppleVTD=%u selected=%s\n",
       systemMapperOrPending,iommuParent,appleVTD,dmaModeName());
 return dmaReady();
}
bool MacOSBackend::releaseTXPool(){
 if(stopSafetyRetained)return false;
 if(txPool.ready()&&setBusMaster(false)!=DMAStatus::Ok)return false;
 return txPool.shutdown(*this);
}
void MacOSBackend::releaseDMAProvider(){if(mapper){mapper->release();mapper=nullptr;}dmaMode=realtek_next::DMAMode::Unavailable;}
void MacOSBackend::detach(){
 if(stopSafetyRetained)return;
 releaseDMAProvider();
 base=nullptr;length=0;if(mapping){mapping->release();mapping=nullptr;}
 if(pci&&pmSaved)pci->configWrite16(UInt8(pmOffset+4),uint16_t(savedPMCSR&~0x8000));
 if(pci&&commandSaved)pci->configWrite16(UInt8(4),savedCommand);
 pci=nullptr;commandSaved=false;pmSaved=false;pmOffset=0;
}
bool MacOSBackend::read8(uint16_t o,uint8_t &v){if(!valid(o,1))return false;ioFence();v=base[o];ioFence();return true;}
bool MacOSBackend::read16(uint16_t o,uint16_t &v){if(!valid(o,2))return false;ioFence();v=*reinterpret_cast<volatile uint16_t*>(base+o);ioFence();return true;}
bool MacOSBackend::read32(uint16_t o,uint32_t &v){if(!valid(o,4))return false;ioFence();v=*reinterpret_cast<volatile uint32_t*>(base+o);ioFence();return true;}
bool MacOSBackend::write8(uint16_t o,uint8_t v){if(!permittedWrite(o,1,v))return false;ioFence();base[o]=v;ioFence();return true;}
bool MacOSBackend::write16(uint16_t o,uint16_t v){if(!permittedWrite(o,2,v))return false;ioFence();*reinterpret_cast<volatile uint16_t*>(base+o)=v;ioFence();return true;}
bool MacOSBackend::write32(uint16_t o,uint32_t v){
 if(!permittedWrite(o,4,v))return false;
 ioFence();*reinterpret_cast<volatile uint32_t*>(base+o)=v;ioFence();return true;
}
void MacOSBackend::delayUs(unsigned us){if(us>=1000){IOSleep(us/1000);us%=1000;}if(us)IODelay(us);}
bool MacOSBackend::readConfig8(uint16_t o,uint8_t &v){if(!pci||o>=4096)return false;v=pci->extendedConfigRead8(o);return true;}
bool MacOSBackend::readConfig16(uint16_t o,uint16_t &v){if(!pci||o>4094||(o&1))return false;v=pci->extendedConfigRead16(o);return true;}
bool MacOSBackend::readConfig32(uint16_t o,uint32_t &v){if(!pci||o>4092||(o&3))return false;v=pci->extendedConfigRead32(o);return true;}
bool MacOSBackend::writeConfig8(uint16_t o,uint8_t v){if(!pci||o>=4096)return false;pci->extendedConfigWrite8(o,v);uint8_t got;return readConfig8(o,got)&&got==v;}
bool MacOSBackend::writeConfig16(uint16_t o,uint16_t v){if(!pci||o>4094||(o&1))return false;pci->extendedConfigWrite16(o,v);uint16_t got;return readConfig16(o,got)&&got==v;}
bool MacOSBackend::writeConfig32(uint16_t o,uint32_t v){if(!pci||o>4092||(o&3))return false;pci->extendedConfigWrite32(o,v);uint32_t got;return readConfig32(o,got)&&got==v;}

DMAStatus MacOSBackend::allocate(DMABuffer &buffer,uint64_t size,uint32_t alignment,DMADirection direction){
 if(buffer.opaque||!size||!alignment||(alignment&(alignment-1)))return DMAStatus::InvalidState;
 auto holder=new NativeDMABuffer;
 if(!holder)return DMAStatus::AllocationFailed;
 IOOptionBits ioDirection=kIODirectionInOut;
 if(direction==DMADirection::ToDevice)ioDirection=kIODirectionOut;
 else if(direction==DMADirection::FromDevice)ioDirection=kIODirectionIn;
 // No-IOMMU hardware needs one bus-contiguous segment for rings and TX arena.
 // This bounded allocation option is exclusive to the verified no-mapper mode.
 if(!dmaReady()||!realtek_next::networkAllocationSizeAllowed(size,alignment,direction,gu25JumboAllocationWindow)){
  delete holder;return DMAStatus::InvalidState;
 }
 if(dmaMode==realtek_next::DMAMode::NoIOMMU)ioDirection|=kIOMemoryPhysicallyContiguous;
 holder->memory=IOBufferMemoryDescriptor::withOptions(ioDirection,vm_size_t(size),alignment);
 if(!holder->memory||holder->memory->getLength()!=size){
  if(holder->memory)holder->memory->release();delete holder;return DMAStatus::AllocationFailed;
 }
 buffer.opaque=holder;buffer.requestedLength=size;buffer.allocated=true;return DMAStatus::Ok;
}
DMAStatus MacOSBackend::prepare(DMABuffer &buffer){
 auto holder=native(buffer);if(!holder||!holder->memory||!dmaReady()||buffer.prepared)return DMAStatus::InvalidState;
 holder->command=IODMACommand::withSpecification(kIODMACommandOutputHost64,64,buffer.requestedLength,
  IODMACommand::kMapped,buffer.requestedLength,holder->memoryPrepared?1:realtek_next::kDescriptorRingAlignment,mapper);
 if(!holder->command)return DMAStatus::PrepareFailed;
 if(holder->command->setMemoryDescriptor(holder->memory,false)!=kIOReturnSuccess)return DMAStatus::PrepareFailed;
 holder->descriptorSet=true;
 if(holder->command->prepare(0,buffer.requestedLength)!=kIOReturnSuccess)return DMAStatus::PrepareFailed;
 holder->commandPrepared=true;buffer.prepared=true;return DMAStatus::Ok;
}
DMAStatus MacOSBackend::map(DMABuffer &buffer){
 auto holder=native(buffer);if(!holder||!holder->command||!buffer.prepared||buffer.mapped)return DMAStatus::InvalidState;
 IODMACommand::Segment64 segments[4]{};UInt64 offset=0;UInt32 count=4;
 if(holder->command->gen64IOVMSegments(&offset,segments,&count)!=kIOReturnSuccess)
  return DMAStatus::MapFailed;
 buffer.segmentCount=count;buffer.mappedLength=offset;
 for(UInt32 i=0;i<count&&i<4;++i){buffer.segments[i].address=segments[i].fIOVMAddr;buffer.segments[i].length=segments[i].fLength;}
 buffer.mapped=true;return DMAStatus::Ok;
}
DMAStatus MacOSBackend::zero(DMABuffer &buffer){
 auto holder=native(buffer);if(!holder||!holder->memory||!buffer.mapped)return DMAStatus::InvalidState;
 auto bufferMemory=OSDynamicCast(IOBufferMemoryDescriptor,holder->memory);
 void *bytes=bufferMemory?bufferMemory->getBytesNoCopy():nullptr;if(!bytes)return DMAStatus::ZeroFailed;
 bzero(bytes,size_t(buffer.requestedLength));return DMAStatus::Ok;
}
void *MacOSBackend::bytes(DMABuffer &buffer){
 if(txPool.owns(buffer))return txPool.bytes(buffer);
 auto holder=native(buffer);auto memory=holder?OSDynamicCast(IOBufferMemoryDescriptor,holder->memory):nullptr;
 return memory?memory->getBytesNoCopy():nullptr;
}
DMAStatus MacOSBackend::syncForDevice(DMABuffer &buffer){
 if(txPool.owns(buffer))return txPool.synchronize(*this);
 auto holder=native(buffer);if(!holder||!holder->command||!buffer.mapped)return DMAStatus::InvalidState;
 return holder->command->synchronize(kIODirectionOut)==kIOReturnSuccess?DMAStatus::Ok:DMAStatus::SyncFailed;
}
DMAStatus MacOSBackend::syncForCPU(DMABuffer &buffer){
 auto holder=native(buffer);if(!holder||!holder->command||!buffer.mapped)return DMAStatus::InvalidState;
 return holder->command->synchronize(kIODirectionIn)==kIOReturnSuccess?DMAStatus::Ok:DMAStatus::SyncFailed;
}
void MacOSBackend::memoryBarrier(){__asm__ __volatile__("mfence":::"memory");}
void MacOSBackend::unmap(DMABuffer &buffer){
 if(txPool.owns(buffer)){buffer.mapped=false;return;}
 auto holder=native(buffer);if(!holder)return;
 if(holder->commandPrepared&&holder->command){holder->command->complete();holder->commandPrepared=false;}
 buffer.mapped=false;buffer.segmentCount=0;buffer.mappedLength=0;
}
void MacOSBackend::complete(DMABuffer &buffer){
 if(txPool.owns(buffer)){buffer.prepared=false;return;}
 auto holder=native(buffer);if(!holder)return;
 if(holder->commandPrepared&&holder->command){holder->command->complete();holder->commandPrepared=false;}
 if(holder->descriptorSet&&holder->command){holder->command->clearMemoryDescriptor(false);holder->descriptorSet=false;}
 if(holder->memoryPrepared){holder->memory->complete(kIODirectionOut);holder->memoryPrepared=false;}
 buffer.prepared=false;
}
void MacOSBackend::release(DMABuffer &buffer){
 if(txPool.release(buffer))return;
 auto holder=native(buffer);if(!holder)return;
 if(holder->commandPrepared&&holder->command)holder->command->complete();
 if(holder->descriptorSet&&holder->command)holder->command->clearMemoryDescriptor(false);
 if(holder->command)holder->command->release();
 if(holder->memoryPrepared)holder->memory->complete(kIODirectionOut);
 if(holder->memory)holder->memory->release();delete holder;
 buffer=DMABuffer{};
}
DMAStatus MacOSBackend::setBusMaster(bool enable){
 if(!pci)return DMAStatus::InvalidState;
 uint16_t command=pci->configRead16(UInt8(4));
 command=enable?uint16_t(command|6):uint16_t(command&~4U);
 pci->configWrite16(UInt8(4),command);
 const bool state=bool(pci->configRead16(UInt8(4))&4);
 return state==enable?DMAStatus::Ok:DMAStatus::BusMasterFailed;
}

// Snapshot outside the output gate. The network core retains the private DMA
// allocation until LS ownership returns, or until BME-off is verified at teardown.
namespace {
struct PacketCopyContext {mbuf_t packet;realtek_next::JTXCopyEvidence *evidence;};
bool copyPacket(void *context,void *destination,size_t size){
 auto &copy=*static_cast<PacketCopyContext*>(context);
 const bool ok=mbuf_copydata(copy.packet,0,size,destination)==0;
 if(copy.evidence){copy.evidence->attempted=true;copy.evidence->copied=ok;}return ok;
}
}
DMAStatus MacOSBackend::mapPacket(DMABuffer &buffer,mbuf_t packet,realtek_next::JTXCopyEvidence *evidence){
 if(!dmaReady()||!packet)return DMAStatus::InvalidState;
 PacketCopyContext context{packet,evidence};
 return txPool.snapshot(buffer,mbuf_pkthdr_len(packet),copyPacket,&context);
}
