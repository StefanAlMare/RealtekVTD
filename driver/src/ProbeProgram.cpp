// SPDX-License-Identifier: GPL-2.0-only
#include "ProbeProgram.hpp"
#include "GU24Program.hpp"
#include "GU25Program.hpp"
namespace realtek_next {
const char *firmwareName(Chip chip) {return chip==Chip::RTL8411B?"rtl_nic/rtl8411-2.fw":
 chip==Chip::RTL8168EVL?"rtl_nic/rtl8168e-3.fw":
 (chip==Chip::RTL8168H||chip==Chip::RTL8168M)?"rtl_nic/rtl8168h-2.fw":nullptr;}
static ProbeResult runSteps(RegisterIO &io,PCIConfig &pci,uint8_t function,uint16_t vendor,uint16_t device,
                     const uint8_t *firmware,size_t firmwareSize) {
 ProbeResult result;
 uint32_t txconfig;
 if(!io.read32(0x40,txconfig)){result.status=AccessStatus::IOFailure;return result;}
 result.txConfig=txconfig;result.txConfigValid=true;
 result.identity=identify(vendor,device,txconfig,true);
 if(result.identity.status!=ProbeStatus::Supported)return result;
 RegisterAccess r(io,result.identity.linuxMacVersion);HardwareInit hw(r,pci,function);PhyInit phy(r);
 const bool gu24=result.identity.chip==Chip::RTL8168GU_Method24;
 const bool gu25=result.identity.chip==Chip::RTL8168GU_Method25;
 const bool gu=gu24||gu25;
 if(gu&&(firmware||firmwareSize)){
  result.stage=gu25?"gu25-unexpected-external-firmware":"gu24-unexpected-external-firmware";
  result.status=AccessStatus::InvalidFirmware;return result;
 }
 FirmwareProgram program;
#define STAGE(name,call) do{result.stage=name;result.status=(call);if(result.status!=AccessStatus::Ok)return result;}while(0)
 if(firmware)STAGE("firmware-validate",program.parse(firmware,firmwareSize));
 result.hardwareTouched=true;
 STAGE("pre-init",gu?quiesceGU24(r):hw.preInitialize(result.identity.chip));
 STAGE("reset",hw.reset());
 if(gu24){
  STAGE("gu24-exit-oob",exitOOBGU24(r));
  STAGE("gu24-mac-mcu",applyMACPatchGU24(r));result.gu24MACPatchVerified=true;
 }else if(gu25){
  STAGE("gu25-exit-oob",exitOOBGU24(r)); // Mieze shares these 24/25 OOB steps.
  STAGE("gu25-mac-mcu",applyMACPatchGU25(r));result.gu25MACPatchVerified=true;
  STAGE("gu25-ocp-power",disableOCPPowerSavingGU25(r,&result.phyConfig));
  STAGE("gu25-ephy",configureEPHYGU25(r));
 }
 STAGE("phy-wake",phy.wake());
 // Start the legacy firmware transaction with a settled PHY reset.
 if(result.identity.linuxMacVersion==34||gu){
  STAGE("phy-reset",phy.softReset());result.phyResetDiag=capturePHYDiagnostic(r);
 }
 if(firmware){
  PHYFirmwareTarget targetPHY(r);MACFirmwareTarget targetMAC(r);
  STAGE("firmware-execute",program.execute(targetPHY,targetMAC,io));result.firmwareApplied=true;
  STAGE("firmware-page-restore",r.restoreStatus());
  // Wait for any reset initiated by firmware, before subsequent PHY accesses.
  result.stage="firmware-reset-wait";
  bool ready=false;
  for(unsigned n=0;n<12;++n){r.delay(50000);auto v=r.mdioRead(0);
   if(v.status!=AccessStatus::Ok){result.status=v.status;return result;}
   if(!(v.value&0x8000)){ready=true;break;}}
  if(!ready){result.status=AccessStatus::Timeout;return result;}
 }
 if(gu25){STAGE("gu25-phy-mcu",applyPHYMCUGU25(r,&result.phyConfig,&result.identity));result.gu25PHYMCUApplied=true;}
 STAGE("phy-configure",gu24?configurePHYGU24(r):gu25?configurePHYGU25(r,&result.phyConfig,&result.identity,result.gu25MACPatchVerified&&result.gu25PHYMCUApplied):phy.configure(result.identity.chip));
 result.phyInitDiag=capturePHYDiagnostic(r);
 // rtl8169_init_phy also performs genphy_soft_reset after the generation's config.
 // Preserve the original MAC44/46 order and apply this final reset to MAC34.
 if(!gu){STAGE("phy-reset",phy.softReset());result.phyResetDiag=capturePHYDiagnostic(r);}
 STAGE("copper-capabilities",phy.readCopperCapabilities(result.autoneg));
 if(gu&&(result.autoneg.capabilities.bmsr==0xffff||
    result.autoneg.capabilities.bmsrFirst==0xffff||result.autoneg.capabilities.estatus==0xffff||
    !result.autoneg.capabilities.gigabitAdvertisement(false))){
  result.stage=gu25?"gu25-gigabit-required":"gu24-gigabit-required";result.status=AccessStatus::Unsupported;return result;
 }
 STAGE("copper-autoneg-configure",gu?configureCopperAutoneg(r,result.autoneg,false):phy.configureCopperAutoneg(result.autoneg));
 STAGE("mac-configure",gu24?configureMACGU24(r,pci):gu25?configureMACGU25(r,pci,&result.macConfig):hw.configureMAC(result.identity.chip));
 if(result.identity.linuxMacVersion==46||result.identity.linuxMacVersion==34)STAGE("eee-policy",phy.enforceEEEOff());
 STAGE("phy-status",phy.readState(result.phy));
 if(gu&&(!result.phy.id1||result.phy.id1==0xffff||!result.phy.id2||result.phy.id2==0xffff)){
  result.stage=gu25?"gu25-invalid-phy-id":"gu24-invalid-phy-id";result.status=AccessStatus::VerificationFailed;return result;
 }
 if(result.identity.linuxMacVersion==46||result.identity.linuxMacVersion==34||gu){
  result.stage="tx-lpi-readback";auto v=r.eriRead(0x1b0);result.status=v.status;if(v.status!=AccessStatus::Ok)return result;
  result.eri1b0=v.value;if((v.value&3)||result.phy.eeeAdvertisement){result.status=AccessStatus::VerificationFailed;return result;}
 }
 STAGE("tx-rx-stopped",hw.verifyStopped());
 result.stage=gu24?"gu24-stage1-complete":gu25?"gu25-stage1-complete":firmware?"stage1-complete":"firmware-missing-partial-probe";
 result.complete=gu||bool(firmware);return result;
#undef STAGE
}
ProbeResult runProbe(RegisterIO &io,PCIConfig &pci,uint8_t function,uint16_t vendor,uint16_t device,
                     const uint8_t *firmware,size_t firmwareSize) {
 auto result=runSteps(io,pci,function,vendor,device,firmware,firmwareSize);
 // Even an early wait or firmware failure leaves the traffic engine stopped.
 if(result.identity.status==ProbeStatus::Supported&&
    (!isGUProfile(result.identity.chip)||result.hardwareTouched)) {
  const bool masked=io.write16(0x3c,0),stopped=io.write8(0x37,0);
  uint8_t command=255;const bool read=io.read8(0x37,command);
  if((!masked||!stopped||!read||(command&12))&&result.status==AccessStatus::Ok){
   result.status=AccessStatus::VerificationFailed;result.stage="final-stop";result.complete=false;
  }
 }
 return result;
}
}
