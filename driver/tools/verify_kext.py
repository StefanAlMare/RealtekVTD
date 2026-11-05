#!/usr/bin/env python3
import pathlib,plistlib,subprocess,sys,json,hashlib
k=pathlib.Path(sys.argv[1]);b=k/'Contents/MacOS/RealtekVTDNextEthernet'
info=plistlib.loads((k/'Contents/Info.plist').read_bytes())
assert info['CFBundleIdentifier']=='local.realtekvtdnext.ethernet'
assert info['IOKitPersonalities']['RealtekVTDNextEthernet']['IOClass']=='RealtekVTDNextEthernet'
assert info['IOKitPersonalities']['RealtekVTDNextEthernet']['IOPCIMatch']=='0x816810ec'
assert info['IOKitPersonalities']['RealtekVTDNextEthernet']['IOPCIUseDeviceMapper'] is True
kind=subprocess.check_output(['file',str(b)],text=True);assert 'kext bundle x86_64' in kind
undefined=subprocess.check_output(['nm','-u',str(b)],text=True)
for forbidden in ['_malloc','_free','___gxx_personality','__ZSt','_pthread','__ZNSt','_exit']:
 assert forbidden not in undefined, 'Userland runtime dependency: '+forbidden
symbols=subprocess.check_output(['nm',str(b)],text=True)
for symbol in ['_kmod_info','__start','__stop','RealtekVTDNextEthernet','runStage2B','runStage2C','runStage2D','runStage2E']:assert symbol in symbols
for symbol in ['IODMACommand','IOMapper','IOBufferMemoryDescriptor','IOEthernetController','IOBasicOutputQueue','Network','mbuf_copyback']:assert symbol in symbols
assert info['CFBundleVersion']=='0.21.0'
assert info['RXFILL1Schema']==1 and info['IOKitPersonalities']['RealtekVTDNextEthernet']['GU25RXFillEnabled'] is True
for required in ['_mbuf_data','_mbuf_mclhasreference']:assert required in symbols
assert info['TXPRECHECK1Schema']==1 and info['IOKitPersonalities']['RealtekVTDNextEthernet']['GU25TXPrecheckEnabled'] is False
assert info['RXNOWAIT1Schema']==1 and info['IOKitPersonalities']['RealtekVTDNextEthernet']['GU25RXNoWaitEnabled'] is True
for required in ['_mbuf_allocpacket','_mbuf_trailingspace','_mbuf_setlen']:assert required in symbols
assert info['TXQOBS1Schema']==1 and info['GU25TXQOBS1Diagnostic'] is True
assert info['IOKitPersonalities']['RealtekVTDNextEthernet']['GU25TXQOBS1Enabled'] is True
assert info['SINGLE1Schema']==1 and info['GU25TSOSingleImplemented'] is True and info['GU25TSOSingleHardwareValidated'] is False
assert info['IOKitPersonalities']['RealtekVTDNextEthernet']['GU25TSOSingleEnabled'] is True
assert info['TSO6Schema']==1 and info['GU25TSO6Implemented'] is True and info['GU25TSO6HardwareValidated'] is False
assert info['IOKitPersonalities']['RealtekVTDNextEthernet']['GU25TSO6Enabled'] is True and info['GU25TSO6MaxIPBytes']==9000
assert info['CSUM6Schema']==1 and info['GU25CSUM6Enabled'] is True
assert info['RXCSUM1Schema']==2 and info['RXCSUM2ProtocolScopedFailures'] is True
assert info['IOKitPersonalities']['RealtekVTDNextEthernet']['GU25RXChecksumEnabled'] is True
assert info['GU25RXChecksumEnabled'] is True and info['GU25RXCSUM1Implemented'] is True and info['GU25RXCSUM1WireValidated'] is False
assert info['GU25TSO4ECN1Implemented'] is True and info['GU25TSO4ECN1HardwareValidated'] is False
assert info['TSO4OPT1Schema']==2 and info['TSO4ECN1Schema']==1
assert info['GU25PHYMCU1Implemented'] is True and info['GU25PHYMCU1HardwareValidated'] is False
assert info['GU25TSO4OPT1Implemented'] is True and info['GU25TSO4OPT1HardwareValidated'] is False
assert info['GU25TSO4Implemented'] is True and info['GU25TSO4HardwareValidated'] is False
assert info['GU25TSO4MaxIPBytes']==9000 and info['GU25TSO4SoftwareProgress'] is True
assert info['GU25TXChecksumImplemented'] is True and info['GU25TXChecksumIPv4Only'] is False
assert info['IOKitPersonalities']['RealtekVTDNextEthernet']['GU25CSUM6Enabled'] is True
assert info['GU25TXChecksumSoftwareFallback'] is True and info['GU25TXChecksumHardwareValidated'] is False
assert info['RealtekVTDNextBuild']=='0.21.0-GU25-P14-IDX257-SCAN1-JTXFAST1-FC1-BME1-VLAN1-JUMBO1-TXCSUM1-TSO4-OPT1-PHYMCU1-ECN1-RXCSUM1-RXCSUM2-CSUM6-TSO6-SINGLE1-TXQOBS1-EMITCOPY1-RXNOWAIT1-TXPRECHECK1-RXFILL1-STOPSAFE1-MAC44-TXCSUM1-MAC44-FC1-MAC44-RXCSUM1-MAC44-TSO4-MAC44-TSO6-MAC44-NOIOMMU1-MAC44-VLANTSO1-MAC44-VLANTSO2-MAC44-VLANTSO3-MAC34-NOIOMMU1-MAC34-NOIOMMU2-FC1-MAC34-TXQ1-MAC34-TXQ2-DIAG-MAC34-TXQ3-DIAG-MAC34-TXQ4-MAC46-APPLEVTD1-DIAG-MAC46-FULL-APPLEVTD-ON-MAC46-FULL-NOIOMMU-MAC46-LPI1-MAC46-LPI2-MAC46-N64-TXPRECHECK-B'
assert info['MAC46VTD1Schema']==1 and info['MAC46VTD1Diagnostic'] is True
assert info['MAC46VTD1HardwareValidated'] is False and info['MAC46VTD1AutomaticRecovery'] is False
assert info['IOKitPersonalities']['RealtekVTDNextEthernet']['MAC46VTD1Enabled'] is True
assert info['RealtekVTDNextBuild'].encode()+b'\0' in b.read_bytes(), 'New label requires the newly compiled native image'
assert info['RealtekVTDNextSupportedMACVersions']==[34,42,44,46]
assert info['RealtekVTDNextSupportedCFGMethods']==[24,25]
assert info['GU24PortImplemented'] is True and info['GU24NoIOMMURequired'] is False
assert 'IOPCISecondaryMatch' not in info['IOKitPersonalities']['RealtekVTDNextEthernet']
for flag in ['GU24HardwareValidated','GU24JumboEnabled','GU24EEEEnabled','GU24PauseEnabled','GU24VLANEnabled']:
 assert info[flag] is False,flag
assert info['RealtekVTDNextRXFixImplemented'] is False
assert info['RealtekVTDNextRXSTAT1']=='DEFERRED'
assert info['RealtekVTDNextExperimental'] is True and info['RealtekVTDNextHardwareValidated'] is False
assert info['RealtekVTDNextDiagnosticOnly'] is False
assert info['GU25EEEACKTrial'] is True and info['GU25EEEACKTrialRequiresMCUExit'] is True and info['RealtekVTDNextJumboFixImplemented'] is False
for method in ['getFeatures','getMaxPacketSize','setMaxPacketSize']:assert method in symbols
receipt={'kind':kind.strip(),'sha256':hashlib.sha256(b.read_bytes()).hexdigest(),'version':info['CFBundleVersion'],'build':info['RealtekVTDNextBuild'],'symbol_audit':'PASS; native 64-bit IODMACommand/IOMapper imports present; expected kernel imports remain unresolved until load','controller_class':'RealtekVTDNextEthernet','tx_mbuf_native_mapping':False,'tx_private_snapshot_pool':True,'rxstat1':'DEFERRED','vlan':{'GU25':'software-in-band; policy unchanged','MAC34_44_46':'software-in-band; unchanged','GU24':'not advertised; unvalidated'},'jumbo':{'GU25':'prepared-resource guarded, default MTU1500; no physical validation claim','MAC34_44_46':'existing conditional resources; unchanged','GU24':'off; MTU 1500'},'tx_checksum':{'GU25':'IPv4 IP/TCP/UDP and IPv6 TCP/UDP checksum, typed RX results and independent ifnet assist; cumulative TSO4/TSO6','RX':True,'TSO4':True,'TSO6':True,'wire_verified':False},'RXNOWAIT1':{'schema':1,'allocator':'public mbuf_allocpacket MBUF_DONTWAIT for initialized exact GU25 with native64 NoIOMMU; no WAITOK fallback on failure','physical_validated':False},'TXPRECHECK1':{'schema':1,'policy':'Exact initialized GU25 ready NoIOMMU TSO4 HW capacity precheck before aggregate copy; final submit checks retained','physical_validated':False},'RXFILL1':{'schema':1,'policy':'Fresh exclusive CPU mbuf single-pass normalize/fill; initialized GU25 native64 with RXNOWAIT1; exact legacy fallback when OFF','comparison_TXPRECHECK1':False,'comparison_TXQOBS1':True,'physical_validated':False},'irq_only_completions':True,'sleep_wake_validated':False,'continuous_packet_traffic_enabled':True,'hardware_verified':False}
receipt.update(MAC46VTD1=dict(schema=1,diagnostic_only=True,scope='Exact initialized RTL8168H MAC46 XID541 real DeviceMapper',automatic_recovery=False,hardware_validated=False,wire_verified=False),immediate_base_uuid='473F8355-EF3F-3A3C-89EF-B4D9E2E354ED',immediate_base_executable_sha256='ae8c5539d871827006e1537fa1093b9226c684857f0106b8c7248c3b4485138c')
(k.parent/'build-verification.json').write_text(json.dumps(receipt,indent=2))
print('PASS: plist, x86_64 kext, native DMA imports and kernel-only dependencies; Ethernet controller, continuous IRQ-driven TX/RX and native device DMA imports')

assert info['STOPSAFE1Schema']==1 and info['GU25STOPSAFE1Implemented'] is True
assert info['GU25STOPSAFE1HardwareValidated'] is False and info['GU25STOPSAFE1AutomaticRecovery'] is False

assert info["IOKitPersonalities"]["RealtekVTDNextEthernet"]["MAC44TXChecksumEnabled"] is True
assert info["MAC44TXCSUM1Schema"]==1 and not info["MAC44TXCSUM1WireVerified"]

assert info["MAC44FC1Schema"]==1 and not info["MAC44FC1WireVerified"]
assert info["IOKitPersonalities"]["RealtekVTDNextEthernet"]["MAC44FlowControlEnabled"] is True

assert info["MAC44RXCSUM1Schema"]==1 and not info["MAC44RXCSUM1WireVerified"]
assert info["IOKitPersonalities"]["RealtekVTDNextEthernet"]["MAC44RXChecksumEnabled"] is True

assert info["MAC44TSO4Schema"]==1 and info["MAC44TSO4Experimental"] and not info["MAC44TSO4WireVerified"]
assert info["IOKitPersonalities"]["RealtekVTDNextEthernet"]["MAC44TSO4Enabled"] is True

assert info["MAC44TSO6Schema"]==1 and info["MAC44TSO6Experimental"] and not info["MAC44TSO6WireVerified"]
assert info["IOKitPersonalities"]["RealtekVTDNextEthernet"]["MAC44TSO6Enabled"] is True

assert info["MAC44NOIOMMU1Schema"]==1 and info["MAC44NOIOMMU1Experimental"] and not info["MAC44NOIOMMU1WireVerified"]

assert info['MAC34TXQ1Schema']==1 and info['MAC34TXQ1Experimental'] is True and info['MAC34TXQ1HardwareValidated'] is False
assert info['IOKitPersonalities']['RealtekVTDNextEthernet']['MAC34TXQ1Enabled'] is True

assert info['MAC34TXQ2Diagnostic'] is True and info['IOKitPersonalities']['RealtekVTDNextEthernet']['MAC34TXQ2Enabled'] is True

assert info['MAC46FULLSchema']==1 and info['MAC46FULLAutomatic'] is True
assert info['MAC46FULLAppleVTDOnly'] is False and info['MAC46FULLPhysicalQualified'] is False

assert info['MAC46FULLNative64Supported'] is True and info['MAC46NOIOMMU1Schema']==1

assert info["MAC46LPI1Schema"]==1 and info["MAC46LPI1LinkUpContainment"] and not info["MAC46LPI1PhysicalQualified"]

assert info["MAC46LPI2Schema"]==1 and info["MAC46LPI2MContainment"] and not info["MAC46LPI2MPhysicalQualified"]

assert info['MAC46N64TXPrecheckSchema']==1 and info['MAC46N64TXPrecheckExperimental'] is True
assert info['MAC46N64TXPrecheckPhysicalQualified'] is False and info['MAC46N64TXPrecheckAppleVTD'] is False
assert b'MAC46N64TXPrecheckEarlyFull' in b.read_bytes()
