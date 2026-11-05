# Origins

The initial goal was Realtek Ethernet with AppleVTD, starting from an attempt involving Mieze's RealtekRTL8111. Development moved to a native macOS driver using reviewed Linux r8169/Realtek PHY behavior and Realtek/Mieze controller references, then expanded to NoIOMMU. Supported profiles and both DMA paths remain cumulative.

This is not an installed Mieze-driver wrapper or a Linux module running inside macOS. It does not automatically track every later Linux revision.

Pinned references:

- Linux r8169/Realtek PHY: `3cdeaef1754ab8155cdd828c958ada3d36e9ad9a`.
- Linux EEE/LPI references: `202fef9bbbf5784487eec27581389c6fb97c350d`, `f71ff7d406523748bfb8236830c7eaa6a308aaa1`.
- Mieze RealtekRTL8111: `a3dad34ed4f1e72805ce42f6d506f6917fdf79a0`.
- MacKernelSDK: `3f750085caa17ec3a7880f11c11bf4f48cd6a164`.

Detailed input hashes and original notices remain with the source and dependencies. [Contributors](../CONTRIBUTORS.md)
