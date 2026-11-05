The standard masks were checked against Linux include/uapi/linux/mii.h:
https://github.com/torvalds/linux/blob/master/include/uapi/linux/mii.h

The original r8169 hardware-source pin in upstream-manifest.json and all existing chip-specific hardware sequences remain unchanged. CopperAutoneg is an independently implemented standard Clause 22 layer with the preservation and restart policy requested by the user; it is not a wholesale port of phylib.
