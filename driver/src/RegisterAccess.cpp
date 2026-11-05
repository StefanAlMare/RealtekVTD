// SPDX-License-Identifier: GPL-2.0-only
#include "RegisterAccess.hpp"
namespace realtek_next {
AccessStatus RegisterAccess::legacyWrite(unsigned reg,uint16_t value) {
 if(reg>31)return AccessStatus::InvalidAddress;
 if(!io.write32(0x60,0x80000000U|(reg<<16)|value))return AccessStatus::IOFailure;
 const auto status=wait32(0x60,0x80000000U,false,25,20);
 clock.delayUs(20);return status;
}
Read16 RegisterAccess::legacyRead(unsigned reg) {
 if(reg>31)return {AccessStatus::InvalidAddress,0};
 if(!io.write32(0x60,reg<<16))return {AccessStatus::IOFailure,0};
 auto status=wait32(0x60,0x80000000U,true,25,20);uint32_t value=0;
 if(status==AccessStatus::Ok&&(!io.read32(0x60,value)||value==0xffffffffU))status=AccessStatus::IOFailure;
 clock.delayUs(20);return {status,status==AccessStatus::Ok?uint16_t(value):uint16_t(0)};
}
Read16 RegisterAccess::legacyMMDRead(unsigned device,uint16_t reg) {
 if(device>31)return {AccessStatus::InvalidAddress,0};
 const auto page=mdioRead(31);if(page.status!=AccessStatus::Ok)return page;
 auto status=mdioWrite(31,0);Read16 control{status,0},result{status,0};
 if(status==AccessStatus::Ok)control=mdioRead(13);
 status=control.status;
 if(status==AccessStatus::Ok)status=mdioWrite(13,uint16_t(device));
 if(status==AccessStatus::Ok)status=mdioWrite(14,reg);
 if(status==AccessStatus::Ok)status=mdioWrite(13,uint16_t(0x4000|device));
 if(status==AccessStatus::Ok)result=mdioRead(14);else result={status,0};
 const auto restoredControl=control.status==AccessStatus::Ok?mdioWrite(13,control.value):control.status;
 const auto restoredPage=mdioWrite(31,page.value);
 if(restoredControl!=AccessStatus::Ok||restoredPage!=AccessStatus::Ok)restoreFailure=AccessStatus::IOFailure;
 if(result.status==AccessStatus::Ok)result.status=restoreFailure;
 return result;
}
AccessStatus RegisterAccess::legacyMMDWrite(unsigned device,uint16_t reg,uint16_t value) {
 if(device>31)return AccessStatus::InvalidAddress;
 const auto page=mdioRead(31);if(page.status!=AccessStatus::Ok)return page.status;
 auto status=mdioWrite(31,0);Read16 control{status,0};
 if(status==AccessStatus::Ok)control=mdioRead(13);
 status=control.status;
 if(status==AccessStatus::Ok)status=mdioWrite(13,uint16_t(device));
 if(status==AccessStatus::Ok)status=mdioWrite(14,reg);
 if(status==AccessStatus::Ok)status=mdioWrite(13,uint16_t(0x4000|device));
 if(status==AccessStatus::Ok)status=mdioWrite(14,value);
 const auto restoredControl=control.status==AccessStatus::Ok?mdioWrite(13,control.value):control.status;
 const auto restoredPage=mdioWrite(31,page.value);
 if(restoredControl!=AccessStatus::Ok||restoredPage!=AccessStatus::Ok)restoreFailure=AccessStatus::IOFailure;
 return status==AccessStatus::Ok?restoreFailure:status;
}
AccessStatus RegisterAccess::wait32(uint16_t off,uint32_t mask,bool high,unsigned interval,unsigned attempts) {
 if(waitDiagnostic)waitDiagnostic->begin(off==0x74?"ERI":"MMIO32",off,off,mask,high?mask:0,attempts,interval);
 for(unsigned n=0;n<attempts;++n) {
  if(waitDiagnostic)waitDiagnostic->attempts=n+1;
  uint32_t v;
  if(!io.read32(off,v)){if(waitDiagnostic)waitDiagnostic->status=unsigned(AccessStatus::IOFailure);return AccessStatus::IOFailure;}
  if(waitDiagnostic)waitDiagnostic->sample(v);
  if(v==0xffffffffU){if(waitDiagnostic)waitDiagnostic->status=unsigned(AccessStatus::IOFailure);return AccessStatus::IOFailure;}
  if(bool(v&mask)==high) return AccessStatus::Ok;
  clock.delayUs(interval);
 }
 if(waitDiagnostic)waitDiagnostic->status=unsigned(AccessStatus::Timeout);
 return AccessStatus::Timeout;
}
AccessStatus RegisterAccess::ephyWrite(unsigned reg,uint16_t data) {
 if(reg>31) return AccessStatus::InvalidAddress;
 if(!io.write32(0x80,0x80000000U|(reg<<16)|data)) return AccessStatus::IOFailure;
 const auto s=wait32(0x80,0x80000000U,false,10,100);
 clock.delayUs(10); return s;
}
Read16 RegisterAccess::ephyRead(unsigned reg) {
 if(reg>31) return {AccessStatus::InvalidAddress,0};
 if(!io.write32(0x80,reg<<16)) return {AccessStatus::IOFailure,0};
 auto s=wait32(0x80,0x80000000U,true,10,100); uint32_t v=0;
 if(s==AccessStatus::Ok&&!io.read32(0x80,v)) s=AccessStatus::IOFailure;
 if(v==0xffffffffU) s=AccessStatus::IOFailure;
 return {s,s==AccessStatus::Ok?uint16_t(v):uint16_t(0)};
}
AccessStatus RegisterAccess::ephyModify(unsigned reg,uint16_t clear,uint16_t set) {
 auto r=ephyRead(reg); return r.status==AccessStatus::Ok?ephyWrite(reg,uint16_t((r.value&~clear)|set)):r.status;
}
AccessStatus RegisterAccess::eriWrite(unsigned address,uint8_t mask,uint32_t data) {
 if((address&3)||address>0xfff||!mask||(mask&0xf0)) return AccessStatus::InvalidAddress;
 if(!io.write32(0x70,data)||!io.write32(0x74,0x80000000U|(uint32_t(mask)<<12)|address)) return AccessStatus::IOFailure;
 return wait32(0x74,0x80000000U,false,100,100);
}
Read32 RegisterAccess::eriRead(unsigned address) {
 if((address&3)||address>0xfff) return {AccessStatus::InvalidAddress,0};
 if(!io.write32(0x74,0xf000U|address)) return {AccessStatus::IOFailure,0};
 auto s=wait32(0x74,0x80000000U,true,100,100); uint32_t v=0;
 if(s==AccessStatus::Ok&&!io.read32(0x70,v)) s=AccessStatus::IOFailure;
 // All ones is a legal ERI data value; transport reports failed reads separately.
 return {s,s==AccessStatus::Ok?v:0};
}
AccessStatus RegisterAccess::eriModify(unsigned address,uint32_t clear,uint32_t set) {
 auto r=eriRead(address); return r.status==AccessStatus::Ok?eriWrite(address,15,(r.value&~clear)|set):r.status;
}
AccessStatus RegisterAccess::phyModify(unsigned address,uint16_t clear,uint16_t set) {
 auto r=phyRead(address); return r.status==AccessStatus::Ok?phyWrite(address,uint16_t((r.value&~clear)|set)):r.status;
}
Read16 RegisterAccess::pagedRead(uint16_t page,unsigned reg) {
 const auto old=mdioRead(31); if(old.status!=AccessStatus::Ok) return old;
 auto s=mdioWrite(31,page); Read16 r{s,0}; if(s==AccessStatus::Ok) r=mdioRead(reg);
 s=mdioWrite(31,old.value); if(r.status==AccessStatus::Ok) r.status=s; return r;
}
AccessStatus RegisterAccess::pagedWrite(uint16_t page,unsigned reg,uint16_t data) {
 auto old=mdioRead(31); if(old.status!=AccessStatus::Ok) return old.status;
 auto s=mdioWrite(31,page); if(s==AccessStatus::Ok) s=mdioWrite(reg,data);
 auto restore=mdioWrite(31,old.value); return s==AccessStatus::Ok?restore:s;
}
AccessStatus RegisterAccess::pagedModify(uint16_t page,unsigned reg,uint16_t clear,uint16_t set) {
 auto old=mdioRead(31); if(old.status!=AccessStatus::Ok) return old.status;
 auto s=mdioWrite(31,page);
 if(s==AccessStatus::Ok) { auto r=mdioRead(reg); s=r.status; if(s==AccessStatus::Ok) s=mdioWrite(reg,uint16_t((r.value&~clear)|set)); }
 auto restore=mdioWrite(31,old.value); return s==AccessStatus::Ok?restore:s;
}
AccessStatus RegisterAccess::phyParameter(uint16_t parameter,uint16_t clear,uint16_t set) {
 auto old=mdioRead(31); if(old.status!=AccessStatus::Ok) return old.status;
 auto s=mdioWrite(31,0xa43);
 if(s==AccessStatus::Ok) s=mdioWrite(0x13,parameter);
 if(s==AccessStatus::Ok) { auto r=mdioRead(0x14); s=r.status; if(s==AccessStatus::Ok) s=mdioWrite(0x14,uint16_t((r.value&~clear)|set)); }
 auto restore=mdioWrite(31,old.value); return s==AccessStatus::Ok?restore:s;
}
}
