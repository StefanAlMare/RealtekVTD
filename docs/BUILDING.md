# Building RealtekVTDNext

Requires macOS, Xcode command-line tools and Python3.9+. The exact source and pinned MacKernelSDK are included.

From the repository root, validate inputs and inspect the commands:

```sh
python3 scripts/build_native.py --output "$PWD/build/check" --dry-run
```

Compile into a new directory:

```sh
python3 scripts/build_native.py --output "$PWD/build/rebuild"
```

The script verifies source and SDK manifests, compiles all22 units with the recorded flags, then links and verifies `RealtekVTDNext.kext`. No global optimization flag is added. It performs no installation, downloads or network traffic.

This entrypoint builds the native image; it does not replay the complete historical regression workspace. A rebuilt image gets its own measured UUID/SHA-256 and does not inherit the accepted download's physical validation.

The original source and historical build wrappers are retained for provenance. Use the entrypoint above for this distribution. Run `python3 scripts/verify_distribution.py` to verify the packaged accepted image, source and SDK.
