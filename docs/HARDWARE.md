# Hardware and validation matrix

Status at private beta packaging, **5 November 2026**. A chip family label is not a promise of support for every board carrying a related PCI ID. The driver checks controller registers and profile conditions. PCI 10EC:8168 alone does not identify the silicon revision.

“Implemented” means present in the cumulative source. “Physical” refers to the named profile/build history, not a fresh test of B on every computer.

| Project machine | Hardware identity | AppleVTD / mapper | NoIOMMU native64 | Evidence boundary |
|---|---|---|---|---|
| HP | RTL8168EVL, MAC34, measured XID2C9, PCI rev07 | Backend retained; newer MAC34 feature policies are gated off with mapper | TXQ4 accepted conditionally in three sessions | Median TX IPv4/IPv6 781.86/774.71 Mbps; 1,788,881 ownership transfers matched completions. This history predates B. |
| ASUS Haswell | RTL8168GU/RTL8111GU, GU25/CFG25/MAC42, XID509, rev10 | Integrated path, no GU25 ON physical qualification claimed | STOPSAFE1 accepted privately | TX1/RX1/TX4/RX4 933.92/934.56/941.43/936.64 Mbps; sleep/wake owner-confirmed. Later cumulative preservation is not a full B physical matrix. |
| ASUS1 / AsusCalin | RTL8411B, MAC44, XID5C8, rev12 | Separate checksum, TSO4/6 and FC1 porting evidence | Explicit native64 feature eligibility and VLAN/TSO work | VLANTSO3 IPv6 VLAN correction has offline proof; do not infer its physical acceptance from its successful build alone. |
| Acer | RTL8168H/8111H, MAC46, XID541, rev15 | FULL ON private CONDITIONAL GO; LPI1 applied in two observations in one boot per owner handover | FULL OFF implemented and checked offline | Earlier ON 48-test matrix and later LPI observation are different evidence. They do not prove all intermittent stalls fixed. No automatic Acer OFF acceptance from Dell. |
| Current Dell | RTL8168H/8111H, MAC46, XID541, rev15 | LPI2 TX64OWN watchdog demonstrated; unresolved investigation | **B accepted final reference for beta, physically used and installed** | Owner reports 926.1 Mbps TX and no watchdog in the B session. B's native64 precheck does not fix the ON incident. |
| GU24 reference profile | RTL8168GU method24, CFG24, XID508 | Common implementation preserved | Common implementation preserved | No qualified physical computer for CFG24 in this beta. Measured ASUS GU is CFG25, not CFG24. |
| RTL8168M reference profile | MAC46, XID6C0 | Basic profile conserved | Basic profile conserved | B was branched from LPI2 and contains its M-profile LPI extension. It has offline evidence but **no physical M qualification**; it is not promoted as accepted M support. |

Retail models for HP, ASUS and Acer have not been established by the available evidence, so project labels are used. **Dell Precision 7720** belongs to the historical RealtekVTD 0.6.x incident record; it is not inferred to be the current Dell from a subsystem ID.

## No global mode equivalence

- AppleVTD uses the real device-specific mapper and mapped IOVAs. NoIOMMU uses the existing separately guarded native64 route.
- Availability of a route does not qualify every offload on that route. HP MAC34's newer feature gate is deliberately NoIOMMU-only in this source.
- GU25 hardware feature decisions must not be copied to MAC34, MAC44, H46 or M46 by deleting identity guards.
- NoIOMMU is not a workaround automatically selected in the presence of an active or initializing mapper.
- Reported historical performance numbers are not directly comparable rankings: hosts, test windows and versions differ.
