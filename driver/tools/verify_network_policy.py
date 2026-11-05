#!/usr/bin/env python3
"""Audit integration invariants; runtime behavior is verified by tests/network.cpp."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
native=(root/'macos/RealtekVTDNextEthernet.cpp').read_text()
backend=(root/'macos/MacOSBackend.cpp').read_text()
core=(root/'src/NetworkProgram.cpp').read_text()
assert 'public IOEthernetController' in native and 'RVTTXQOutputQueue::create(this)' in native
queue=(root/'macos/TXQOutputQueue.hpp').read_text()
assert 'q->init(target,target->getOutputHandler(),256,1)' in queue
assert 'IOBasicOutputQueue::output(queue,state)' in queue and 'IOBasicOutputQueue::flush()' in queue
assert 'void free() override{IOBasicOutputQueue::free();}' in queue
assert 'enqueue(' not in queue and 'dequeue(' not in queue and 'service(' not in queue
output=native.split('UInt32 RealtekVTDNextEthernet::outputPacket')[1].split('bool RealtekVTDNextEthernet::receive')[0]
assert output.index('loop->inGate()')<output.index('mapPacket')
assert output.index('IOLockUnlock(lock)')<output.index('mapPacket')
assert 'generation==epoch' in output
assert 'mbuf_copydata' in backend and 'txPool.snapshot' in backend
assert 'withAddressRanges' not in backend
assert 'mbuf_data_to_physical' not in backend and 'getPhysicalSegment' not in backend
for forbidden in ['inTaskWithPhysicalMask','kIODMACommandOutputHost32','IODMACommand::kBypassed']:
 assert forbidden not in backend
assert 'if(dmaMode==realtek_next::DMAMode::NoIOMMU)ioDirection|=kIOMemoryPhysicallyContiguous;' in backend
assert 'const bool systemMapperOrPending=IOMapper::gSystem!=nullptr;' in backend
assert 'const bool iommuParent=pci->getProperty("iommu-parent")!=nullptr;' in backend
assert 'selectDMAMode(false,systemMapperOrPending,iommuParent,appleVTD)' in backend
assert 'kIODMACommandOutputHost64' in backend and 'IODMACommand::kMapped' in backend
assert 'holder->memoryPrepared?1:' in backend
assert 'wireLength-4' in core and 'n.rxConsumer=(i+1)%kNetworkRXCount' in core
assert core.index('d.setBusMaster(false)')<core.index('cleanup(d,n.txPackets[i])',core.index('bool stopNetwork'))
assert 'n.unsafeRetained=true' in core and 'packet=DMABuffer{}' in core
irq=native.split('void RealtekVTDNextEthernet::tick')[1].split('IOReturn RealtekVTDNextEthernet::selectMedium')[0]
assert 'serviceNetwork' not in irq and 'syncForCPU' not in irq
assert 'STAGE3A' not in native and 'RealtekVTDNext-STAGE' not in core
print('PASS: Ethernet integration, ungated private TX snapshot, epoch check, native64, CRC removal, IRQ-only completions and BME-before-release')
