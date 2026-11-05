# RealtekVTDNext RXFILL1

0.21.0-GU25-P14-IDX257-SCAN1-JTXFAST1-FC1-BME1-VLAN1-JUMBO1-TXCSUM1-TSO4-OPT1-PHYMCU1-ECN1-RXCSUM1-RXCSUM2-CSUM6-TSO6-SINGLE1-TXQOBS1-EMITCOPY1-RXNOWAIT1-TXPRECHECK1-RXFILL1

Single traversal for length normalization and bounded copy into fresh CPU mbufs; not zero-copy or a second payload-copy removal. Exact initialized GU25, ready NoIOMMU and RXNOWAIT1 required. OFF preserves allocateRXPacket + mbuf_copyback exactly. TXPRECHECK1 code retained, comparison preference OFF; TXQOBS1 ON; RXNOWAIT1 ON. No install/traffic/network changes. See ../../RXFILL1_IDENTITY.json and ../../REZULTATE-RXFILL1.txt.
