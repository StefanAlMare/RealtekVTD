# Implemented capabilities and explicit limits

The beta is a cumulative codebase, not an “enable all” package. Exact chip identity, firmware/configuration success, resource readiness, feature preferences and actual DMA mode remain prerequisites. Default MTU is **1500**.

| Area | Implemented scope | Qualification / boundary |
|---|---|---|
| Ethernet | Copper link negotiation 10/100/1000, full-duplex where negotiated | Accepted reference runs at 1 Gbps. Not 2.5/5/10 GbE support. |
| DMA | AppleVTD/device IOMapper and NoIOMMU native64 | Real readiness required. Mapping lifetimes, synchronization, barriers and ownership checks retained. |
| TX checksum | IPv4 IP/TCP/UDP and IPv6 TCP/UDP planning, hardware only where eligible, software fallback | MAC34 hardware scope is narrower: IP/TCP with UDP software. Invalid or unsupported requests are not transmitted unfinished. |
| RX checksum | Typed IPv4/IPv6 metadata, descriptor decoding, checked/valid mapping to macOS | Unconfirmed, contradictory or ineligible cases receive no unconfirmed hardware-valid flags and remain eligible for software verification by the network stack. UDP checksum zero and IPv4 failure bits retain their scoped treatment. Descriptor status alone is not independent on-wire proof. |
| TSO4 / TSO6 | Explicit metadata/length/MSS plans, private snapshots, software segmentation and hardware eligibility | MAC34 intentionally software TSO. Other controllers keep revision-specific guards, TCP options/ECN restrictions, independent family policies and safe fallback. No global hardware TSO claim. |
| VLAN | Software tag handling and VLAN MTU reporting | No hardware VLAN insertion/stripping. MAC44 VLAN aggregate bounds were corrected separately; physical history remains qualified. |
| Jumbo | Up to configured MTU9000 only after exact profile/resource preparation | Larger allocation arena and safe fallback required. No automatic MTU/network changes. Default stays1500. |
| Flow control | FC1 negotiated PAUSE under stopped/BME-safe preconditions | Actual none/rx-only/symmetric result is reported; no forced MAC flow-control bits. Jumbo PAUSE withdrawal/restoration follows existing profile policy. |
| Lifecycle | Enable/disable, stop/start and power transitions; verified safe-stop and retained-resource protection | Sleep/wake evidence is platform-specific. No claim of universal D3cold/WoL. |
| Monitoring | Existing statistics, bounded diagnostic snapshots, TXQOBS1, MAC46 precheck counters | Counters are not retransmissions, hardware completions or proof of a root cause unless the field explicitly measures that event. |

## Native64 B change

B adds an **early capacity precheck on the existing hardware TSO4 path**, before expensive lease/snapshot/copy, only for exact initialized RTL8168H/MAC46/XID541/CFG0 with PCI10EC:8168/rev15 and ready NoIOMMU resources. It preserves the final submit check. A full consistent ring can return Retry|Stall before consuming the same mbuf; errors after OWN do not become Retry.

This does not add service/reclaim calls, DMA operations, global optimization flags or a new queue algorithm. The MAC46 resource dimensions stay **ring64 / pool65 / Apple queue256**. Existing GU25 dimensions stay **ring256 / pool257 / queue1024**. The beta does not enlarge them.

## Safety over feature count

STOPSAFE1, BME-off confirmation, DMA/OWN publication order, epoch/cookie validation and retained-resource quarantine are conserved. Failed shutdown confirmation does not permit freeing possibly active buffers. Automatic complete TX recovery is not implemented. NoIOMMU is not forced globally, and mapper readiness is never fabricated.

EEE/LPI treatment remains controller-specific. The H46 link-up LPI treatment is retained; B also contains the intervening LPI2 M-profile extension, which has offline evidence but no physical M acceptance. This is not a global disable-EEE command for all chips or a proven universal stall repair.

## Outside the current endpoint

No completed support claim is made for RSS/multiqueue, zero-copy/direct scatter-gather, hardware VLAN, LRO/RSC, TSN/PTP, new adaptive IRQ moderation, universal WoL/D3cold, or full automatic recovery. The MAC34 RXSTAT/BSD accounting limitation remains documented rather than hidden by invented traffic counts.

Hardware checksum/segmentation submissions in counters do not substitute for independent receiver-side validation. No feature is promoted solely to make a counter positive.
