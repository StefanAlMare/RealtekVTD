# Chip compatibility — Beta 1

| Chip | AppleVTD | NoIOMMU |
|---|---|---|
| RTL8411B, XID0x5C8 | Supported | Supported |
| RTL8168H / RTL8111H, XID0x541 | Supported* | Supported; accepted beta reference |
| RTL8168EVL | Not qualified in Beta 1 | Supported |
| RTL8168GU / RTL8111GU, XID0x509 | Not qualified in Beta 1 | Supported |

\* A very rare, apparently random TX stall has been reported with AppleVTD on RTL8168H/RTL8111H. It remains unresolved; the cause is unknown and may involve software or hardware-specific conditions.

The table describes the beta's qualified scope. Retained mapper code does not by itself qualify every chip/feature for AppleVTD. An active or initializing mapper is never bypassed by forcing NoIOMMU.

RTL8168GU XID0x508 and RTL8168M XID0x6C0 have conserved source profiles but are not included in the qualified list. Matching PCI10EC:8168 alone is insufficient; the driver verifies the silicon identity.

Physical evidence is specific to each profile and DMA mode. No universal qualification across revisions is implied.
