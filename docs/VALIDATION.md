# Validation and limits — Beta 1

The distributed executable is the accepted RTL8168H/XID541 NoIOMMU reference, unchanged and not rebuilt for packaging.

The recorded TCP TX session reached926.1Mbps, with397,650 submissions matched by completions, zero TX resources still occupied and no watchdog. QueueDrops410 and TCP retransmissions were not eliminated or attributed to a specific cause. Queue statistics are not automatically on-wire losses.

Original build evidence covers22 native units,517 linked imports, ABI/vtable/layout checks, source integrity and cumulative offline regressions. Those are existing executed results, not new physical tests performed during packaging.

Earlier profile validation is not a fresh physical test of this executable on every chip. Host models and submission counters do not independently validate hardware segmentation or checksum correctness at the receiver.

**Known limitation:** RTL8168H/8111H AppleVTD retains an unresolved TX-stall issue. Invalid-cookie, unconfirmed-stop and quarantine paths primarily have offline coverage. Full automatic recovery is not implemented.

No additional test campaign is required to use this accepted reference within its documented scope.
