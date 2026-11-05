#!/usr/bin/env python3
import pathlib,sys,unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
from autoneg_acceptance import evaluate
class Tests(unittest.TestCase):
 def fixture(self,machine='asus'):
  d=dict(DriverVersion='0.20.5-JTX1-DIAG',ChipProfile='RTL8411B-MAC_VER_44',PCISubsystemVendor=0x1043,PCISubsystemDevice=0x200f,PCIRevision=0x12,XID=0x5c8,Stage3BEnabled=True,PHYDiagValidMask=511,PHYDiagReadFailures=0,PHYAutonegConfigured=True,PHYAutonegStatus=0,PHYDiagLink=True,PHYDiagAutonegEnabled=True,PHYDiagAutonegComplete=True,PHYCapabilitiesBMSR=0x79ed,PHYCapabilitiesESTATUS=0x2000,PHYDiagANAR=0xde1,PHYDiagCTRL1000=0x200,PHYDiagFullDuplex=True,PHYDiagSpeedMbps=1000,PHYDiagPartner1000Full=True,Stage3BTXSubmitted=100,Stage3BTXCompleted=100,Stage3BTXWatchdog=False,Stage3BRXDropped=0,DMAMode='device-mapper-native64',DeviceMapperPresent=True)
  if machine=='acer':d.update(ChipProfile='RTL8168H-MAC_VER_46',PCISubsystemVendor=0x1025,PCISubsystemDevice=0x100c,PCIRevision=0x15,XID=0x541,PHYDiagEEEAdvertisement=0)
  return d
 def test_pass(self):
  for machine in ['asus','acer']:self.assertEqual(evaluate(self.fixture(machine),machine),[])
 def test_100_partner(self):
  d=self.fixture();d.update(PHYDiagPartner1000Full=False,PHYDiagSpeedMbps=100);self.assertEqual(evaluate(d,'asus'),[])
 def test_reject(self):
  for k,v in dict(DriverVersion='old',PHYDiagValidMask=255,PHYAutonegConfigured=False,PHYDiagANAR=0xc61,PHYDiagCTRL1000=0,PHYDiagSpeedMbps=10,Stage3BTXCompleted=99,Stage3BTXWatchdog=True,Stage3BRXDropped=1,PHYDiagAutonegComplete=False,DMAMode='no-iommu').items():
   with self.subTest(key=k):d=self.fixture();d[k]=v;self.assertTrue(evaluate(d,'asus'))
  d=self.fixture('acer');d['PHYDiagEEEAdvertisement']=6;self.assertTrue(evaluate(d,'acer'))
if __name__=='__main__':unittest.main()
