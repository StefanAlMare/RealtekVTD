// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "RegisterAccess.hpp"
namespace realtek_next {
// r8169 f71ff7d40652 restores TX-LPI containment at MAC_VER_46 link-up.
// This MAC-only RMW is allowed with the engines running. It does not repeat
// PHY advertisement, AN, firmware, reset, DMA or the stopped EEE transaction.
struct MAC46LinkLPI {
 enum:uint32_t {Before=1,After=2};
 uint64_t attempts=0,failures=0;
 uint32_t valid=0,before=0,after=0,epoch=0;
 uint8_t phy=0;
 bool attempted=false,written=false,verified=false;
 AccessStatus status=AccessStatus::Unsupported;
 const char *stage="not-run";
 void invalidate(){valid=before=after=epoch=0;phy=0;attempted=written=verified=false;status=AccessStatus::Unsupported;stage="not-run";}
 bool apply(RegisterAccess&r,uint32_t generation,uint8_t observedPHY){
  invalidate();attempted=true;epoch=generation;phy=observedPHY;
  if(attempts!=UINT64_MAX)++attempts;
  auto fail=[&](AccessStatus s,const char*where)->bool{
   status=s;stage=where;if(failures!=UINT64_MAX)++failures;return false;
  };
  auto value=r.eriRead(0x1b0);
  if(value.status!=AccessStatus::Ok)return fail(value.status,"link-lpi-read");
  if(value.value==0xffffffffU)return fail(AccessStatus::IOFailure,"link-lpi-read-sentinel");
  before=value.value;valid|=Before;
  status=r.eriWrite(0x1b0,15,before&~3U);
  if(status!=AccessStatus::Ok)return fail(status,"link-lpi-write");
  written=true;
  value=r.eriRead(0x1b0);
  if(value.status!=AccessStatus::Ok)return fail(value.status,"link-lpi-readback");
  if(value.value==0xffffffffU)return fail(AccessStatus::IOFailure,"link-lpi-readback-sentinel");
  after=value.value;valid|=After;
  if(after&3U)return fail(AccessStatus::VerificationFailed,"link-lpi-mismatch");
  status=AccessStatus::Ok;stage="link-lpi-verified-off";verified=true;return true;
 }
 template<class Sink>void write(Sink&s,bool eligible)const{
  s.setProperty("MAC46LPI1Schema",1,32);s.setProperty("MAC46LPI1Eligible",eligible);
  s.setProperty("MAC46LPI1Attempted",attempted);s.setProperty("MAC46LPI1Written",written);
  s.setProperty("MAC46LPI1Verified",verified);s.setProperty("MAC46LPI1Valid",valid,32);
  s.setProperty("MAC46LPI1Before",before,32);s.setProperty("MAC46LPI1After",after,32);
  s.setProperty("MAC46LPI1Epoch",epoch,32);s.setProperty("MAC46LPI1PHY",phy,8);
  s.setProperty("MAC46LPI1Status",unsigned(status),32);s.setProperty("MAC46LPI1Stage",stage);
  s.setProperty("MAC46LPI1Attempts",attempts,64);s.setProperty("MAC46LPI1Failures",failures,64);
 }
};
}
