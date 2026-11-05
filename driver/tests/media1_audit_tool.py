#!/usr/bin/env python3
import pathlib,sys,unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
from media1_audit import evaluate
class ReportingAuditTests(unittest.TestCase):
    def fixture(self,machine='asus',vtd='on'):
        return dict(DriverVersion='0.20.3-MEDIA1',Stage3BEnabled=True,
            ChipProfile='RTL8411B-MAC_VER_44' if machine=='asus' else 'RTL8168H-MAC_VER_46',
            XID=0x5c8 if machine=='asus' else 0x541,MACVersion=44 if machine=='asus' else 46,
            PCISubsystemVendor=0x1043 if machine=='asus' else 0x1028,PCISubsystemDevice=0x200f if machine=='asus' else 123,PCIRevision=0x12,
            DeviceMapperPresent=vtd=='on',DMAMode='device-mapper-native64' if vtd=='on' else 'no-iommu-native64',
            Stage3BTXWatchdog=False,Stage3BUnsafeMappingRetained=False,Media1LinkReportSucceeded=True,
            PauseReportOnly=True,PauseEffectiveVerified=False,PauseObservationSequence=2,PauseNegotiatedValid=True,
            PauseObservationValidMask=0x11f,PauseObservationReadFailures=0,PauseANAR=0xde1,PauseANLPAR=0xcde1,
            PauseBMCR=0x1040,PauseBMSRFirst=0x79e9,PauseBMSRSecond=0x79ed,PausePHYStatusRaw=0xf3,
            PauseTxNegotiated=True,PauseRxNegotiated=True,PauseNegotiatedState='symmetric',PausePHYStatusValid=True,
            PausePHYStatusTx=True,PausePHYStatusRx=True,PausePHYStatusAgreementValid=True,PausePHYStatusAgreement=True,
            PauseMediumFlowControl=True,IOActiveMedium='00500030',IOLinkSpeed=1000000000,IOLinkStatus=3,
            IOMediumDictionary={'00500030':{'Type':0x500030,'Speed':1000000000},'00100030':{'Type':0x100030,'Speed':1000000000}})
    def test_four_cells(self):
        for m in ['asus','dell']:
            for v in ['on','off']:self.assertEqual(evaluate(self.fixture(m,v),m,v),[])
    def test_reject_identity_mode_and_false_claims(self):
        for key,value in [('DriverVersion','0.20.2-AUTONEG'),('XID',1),('DMAMode','bypassed'),('DeviceMapperPresent',False),('PauseEffectiveVerified',True),('PauseTxNegotiated',False),('IOActiveMedium','00100030'),('PauseBMCR',0xffff),('PauseBMSRSecond',4),('Stage3BTXWatchdog',True)]:
            d=self.fixture();d[key]=value;self.assertTrue(evaluate(d,'asus','on'),key)
    def test_asymmetric_and_unknown(self):
        d=self.fixture();d.update(PauseANAR=0x801,PauseANLPAR=0x4c01,PausePHYStatusRaw=0x53,PausePHYStatusRx=False,PauseRxNegotiated=False,PauseNegotiatedState='tx-only',PauseMediumFlowControl=False,IOActiveMedium='00100030')
        self.assertEqual(evaluate(d,'asus','on'),[])
        d.update(PauseNegotiatedValid=False,PauseTxNegotiated=False,PauseNegotiatedState='autoneg-incomplete')
        self.assertEqual(evaluate(d,'asus','on'),[])
        d['PauseMediumFlowControl']=True;self.assertTrue(evaluate(d,'asus','on'))
if __name__=='__main__':unittest.main()
