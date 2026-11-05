#!/usr/bin/env python3
"""Observe identity, DMA mode and media reporting; never certify on-wire PAUSE."""
import argparse, json, pathlib, plistlib, subprocess, sys
VERSION='0.20.3-MEDIA1'
TABLE=((0,0,0,0),(0,3,0,3),(0,0,0,1),(0,3,2,3))

def evaluate(d,machine,vtd):
    errors=[]
    def require(ok,message):
        if not ok: errors.append(message)
    require(d.get('DriverVersion')==VERSION,'Exact MEDIA1 driver required')
    require(d.get('Stage3BEnabled') is True,'Controller must be enabled')
    profile,xid,mac=('RTL8411B-MAC_VER_44',0x5c8,44) if machine=='asus' else ('RTL8168H-MAC_VER_46',0x541,46)
    require((d.get('ChipProfile'),d.get('XID'),d.get('MACVersion'))==(profile,xid,mac),'Unexpected chip identity')
    if machine=='asus': require((d.get('PCISubsystemVendor'),d.get('PCISubsystemDevice'),d.get('PCIRevision'))==(0x1043,0x200f,0x12),'ASUS identity mismatch')
    # Dell board/subsystem/revision must be recorded from its own first capture.
    for key in ['PCISubsystemVendor','PCISubsystemDevice','PCIRevision']:
        require(type(d.get(key)) is int,'Missing PCI identity: '+key)
    require(d.get('DeviceMapperPresent') is (vtd=='on'),'Device mapper disagrees with requested mode')
    require(d.get('DMAMode')==('device-mapper-native64' if vtd=='on' else 'no-iommu-native64'),'Unexpected DMA mode')
    require(d.get('Stage3BTXWatchdog') is False,'TX watchdog is set or unknown')
    require(d.get('Stage3BUnsafeMappingRetained') is False,'Unsafe mapping retention is set or unknown')
    require(d.get('Media1LinkReportSucceeded') is True,'Link report did not succeed')
    require(d.get('PauseReportOnly') is True and d.get('PauseEffectiveVerified') is False,'Reporting must remain separate from effective PAUSE proof')
    require(type(d.get('PauseObservationSequence')) is int and d['PauseObservationSequence']>0,'Missing observation sequence')
    valid=d.get('PauseNegotiatedValid') is True
    tx=d.get('PauseTxNegotiated') is True;rx=d.get('PauseRxNegotiated') is True
    if valid:
        require(d.get('PauseObservationValidMask',0)&0x11f==0x11f and d.get('PauseObservationReadFailures')==0,'Incomplete PAUSE observation')
        anar=d.get('PauseANAR',0);peer=d.get('PauseANLPAR',0);bmcr=d.get('PauseBMCR',0);bmsr=d.get('PauseBMSRSecond',0);raw=d.get('PausePHYStatusRaw',0)
        require(all(type(x) is int and x!=0xffff for x in [anar,peer,bmcr,bmsr,d.get('PauseBMSRFirst',0xffff)]),'Invalid register values')
        require(bool(bmcr&0x1000) and not bmcr&0xce00 and bmsr&0x24==0x24 and not bmsr&0x10,'AN/link not complete and stable')
        require(anar&0x1f==1 and peer&0x1f==1 and peer&0x4000 and not peer&0x2000,'Partner exchange is invalid')
        require(raw!=0xff and raw&3==3 and raw&0x1c in (4,8,16),'Full-duplex link is invalid')
        outcome=TABLE[(anar>>10)&3][(peer>>10)&3]
        require((int(tx)|(int(rx)<<1))==outcome,'Negotiated PAUSE directions disagree with IEEE table')
        require(d.get('PauseNegotiatedState')==('symmetric' if outcome==3 else 'tx-only' if outcome==1 else 'rx-only' if outcome==2 else 'none'),'PAUSE state label mismatch')
        require(d.get('PausePHYStatusValid') is True and d.get('PausePHYStatusAgreementValid') is True,'Missing PHYstatus comparison')
        require(d.get('PausePHYStatusTx') is bool(raw&0x40) and d.get('PausePHYStatusRx') is bool(raw&0x20),'PHYstatus direction decode mismatch')
        agreement=tx==bool(raw&0x40) and rx==bool(raw&0x20)
        require(d.get('PausePHYStatusAgreement') is agreement,'PHYstatus comparison mismatch')
        flow=outcome==3 and agreement
        require(d.get('PauseMediumFlowControl') is flow,'Generic FLOW must require symmetric PAUSE and agreeing PHYstatus')
        active=d.get('IOActiveMedium','')
        medium=d.get('IOMediumDictionary',{}).get(active,{})
        expected_type={4:0x23,8:0x26,16:0x30}.get(raw&0x1c,0)|0x100000|(0x400000 if flow else 0)
        require(medium.get('Type')==expected_type,'IOActiveMedium does not reference the expected published type/FLOW flag')
        require(medium.get('Speed')=={4:10000000,8:100000000,16:1000000000}.get(raw&0x1c),'Published medium speed mismatch')
        require(d.get('IOLinkSpeed')==medium.get('Speed') and d.get('IOLinkStatus')==3,'Active link speed/status mismatch')
    else:
        require(not tx and not rx and d.get('PauseMediumFlowControl') is False,'Unknown PAUSE state must clear negotiated directions/FLOW')
    return errors

def main():
    p=argparse.ArgumentParser();p.add_argument('--machine',choices=['asus','dell'],required=True);p.add_argument('--vtd',choices=['on','off'],required=True);p.add_argument('--input',type=pathlib.Path);p.add_argument('--output',type=pathlib.Path)
    args=p.parse_args();d={}
    try:
        if args.input:
            data=json.loads(args.input.read_text());d=data.get('properties',data)
        else:
            result=subprocess.run(['/usr/sbin/ioreg','-a','-r','-c','RealtekVTDNextEthernet'],capture_output=True,check=True)
            nodes=[]
            def walk(v):
                if isinstance(v,dict):
                    if 'DriverVersion' in v:nodes.append(v)
                    for child in v.get('IORegistryEntryChildren',[]):walk(child)
                elif isinstance(v,list):
                    for child in v:walk(child)
            walk(plistlib.loads(result.stdout))
            if len(nodes)!=1:raise RuntimeError('Expected exactly one driver controller')
            d=nodes[0]
        errors=evaluate(d,args.machine,args.vtd)
    except Exception as exc:errors=[str(exc)]
    scalar={k:v for k,v in d.items() if isinstance(v,(str,int,bool))}
    if isinstance(d.get('IOMediumDictionary'),dict):scalar['IOMediumDictionary']=d['IOMediumDictionary']
    report={'machine':args.machine,'applevtd_requested':args.vtd,'reporting_check_pass':not errors,'errors':errors,
        'effective_flow_control':'NOT_MEASURED','hardware_acceptance':'PENDING separate RX/TX PAUSE measurements','properties':scalar}
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2)+'\n')
    print('PASS: reporting sample; effective PAUSE NOT MEASURED' if not errors else 'NOT PASS: '+'; '.join(errors))
    return 0 if not errors else 1
if __name__=='__main__':sys.exit(main())
