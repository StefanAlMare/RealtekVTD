# RealtekVTDNext 0.20.0 STAGE4D STALLCAPTURE

Diagnostic build for 0.19.0 TX stall with AppleVTD active. Historical packages remain untouched. Both DMA modes retained; packet path/interrupt mask/PM behavior unchanged. No automatic restart.

Live read of the Acer after manual disable/enable preserved a prior watchdog snapshot: 53951 submitted/53887 completed, 64 pending, 64 pool leases; PCI command 1031 includes BME, ChipCommand 12 includes TX/RX enabled, PHY status 147 includes link, IMR 15, ISR 208, TXPoll 64. One sleep/wake cycle is recorded; causal relation is not established. Current enabled state is recovery, not proof the stall is fixed.

Before stop, 0.20 records one CPU-synchronized TX ring snapshot: head descriptor and bounded pending descriptor OWN/returned counts, validity and sync result. It does not complete any packet, retry polling, re-kick hardware or bypass DMA safety. Hardware may change descriptors during capture, so counts are diagnostic observations, not an atomic transaction. IRQ-service and network-status failures also capture a reason. Existing register snapshot remains. Properties persist through disable/enable until another captured failure or unload/reboot.

Interpretation: owned head suggests hardware still held the packet; returned head suggests completion processing/interrupt delivery needs investigation. Neither alone establishes the ultimate cause. Invalid synchronization is reported explicitly.

Compiled driver: build/Release/RealtekVTDNextEthernet.kext. No install, EFI, NVRAM or network changes by tools. First test is normal operation with AppleVTD active without intentional sleep. On failure, COLLECT_LOCAL.command before disable/enable if possible. Boot and sleep/throughput validation remain incomplete.
