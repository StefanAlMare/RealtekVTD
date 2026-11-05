// SPDX-License-Identifier: GPL-2.0-only
#include "FirmwareProgram.hpp"
namespace realtek_next {
static uint32_t little32(const uint8_t *p) {
 return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
}
uint32_t FirmwareProgram::instruction(size_t pc) const { return little32(bytes+start+pc*4); }
AccessStatus FirmwareProgram::parse(const uint8_t *image,size_t size) {
 bytes=nullptr; start=0; count=0;
 if(!image||size<4||size>1024*1024) return AccessStatus::InvalidFirmware;
 size_t offset=0,words=size/4;
 if(little32(image)==0) {
  if(size<45) return AccessStatus::InvalidFirmware;
  unsigned checksum=0; for(size_t i=0;i<size;++i) checksum+=image[i];
  if(checksum&255) return AccessStatus::InvalidFirmware;
  offset=little32(image+36); words=little32(image+40);
  if(offset<45||offset>size||(offset&3)||!words||words>(size-offset)/4) return AccessStatus::InvalidFirmware;
 } else if(size%4) return AccessStatus::InvalidFirmware;
 for(size_t pc=0;pc<words;++pc) {
  const uint32_t ins=little32(image+offset+pc*4),op=ins>>28,reg=(ins>>16)&4095,data=ins&65535;
  switch(op) {
  case 0:case 1:case 2:case 7:case 8:case 12:case 14: break;
  case 3: if(reg>pc) return AccessStatus::InvalidFirmware; break;
  case 4: if(data>1) return AccessStatus::InvalidFirmware; break;
  case 9: if(words-pc<=2) return AccessStatus::InvalidFirmware; break;
  case 10:case 11:case 13: if(reg>=words-pc-1) return AccessStatus::InvalidFirmware; break;
  default:return AccessStatus::InvalidFirmware;
  }
 }
 bytes=image;start=offset;count=words;return AccessStatus::Ok;
}
AccessStatus FirmwareProgram::execute(FirmwareTarget &phy,FirmwareTarget &mac,DelayProvider &delay,
                                     size_t maxSteps,uint64_t maxDelayUs) const {
 struct Restore { FirmwareTarget &phy,&mac; ~Restore(){phy.restore();mac.restore();} } restore{phy,mac};
 if(!bytes||!count) return AccessStatus::InvalidFirmware;
 FirmwareTarget *target=&phy; uint16_t previous=0; uint64_t reads=0,waited=0; size_t steps=0,pc=0;
 while(pc<count) {
  if(steps++>=maxSteps) return AccessStatus::BudgetExceeded;
  const uint32_t ins=instruction(pc),op=ins>>28,reg=(ins>>16)&4095;
  const uint16_t data=uint16_t(ins); size_t next=pc+1; AccessStatus s=AccessStatus::Ok;
  switch(op) {
  case 0:{auto r=target->read(reg);s=r.status;if(s==AccessStatus::Ok){previous=r.value;++reads;}break;}
  case 1:previous|=data;break;
  case 2:previous&=data;break;
  case 3:next=pc-reg;break;
  case 4:target=data?&mac:&phy;break;
  case 7:reads=0;break;
  case 8:s=target->write(reg,data);break;
  case 9:if(reads==data)++next;break;
  case 10:if(previous==data)next+=reg;break;
  case 11:if(previous!=data)next+=reg;break;
  case 12:s=target->write(reg,previous);break;
  case 13:next+=reg;break;
  case 14:{const uint64_t us=uint64_t(data)*1000;
    if(us>maxDelayUs-waited)return AccessStatus::BudgetExceeded;
    waited+=us;delay.delayUs(unsigned(us));break;}
  default:return AccessStatus::InvalidFirmware;
  }
  if(s!=AccessStatus::Ok)return s;
  pc=next;
 }
 return AccessStatus::Ok;
}
Read16 MACFirmwareTarget::read(unsigned reg) {
 const uint32_t base=r.currentPHYBase();
 if(reg>4095||base+reg>65535) return {AccessStatus::InvalidAddress,0};
 return r.macRead(base+reg);
}
AccessStatus MACFirmwareTarget::write(unsigned reg,uint16_t data) {
 if(reg==31){if(data>4095)return AccessStatus::InvalidAddress;r.setFirmwareOCPBase(uint32_t(data)<<4);return AccessStatus::Ok;}
 const uint32_t base=r.currentPHYBase();
 if(reg>4095||base+reg>65535)return AccessStatus::InvalidAddress;
 return r.macWrite(base+reg,data);
}
}
