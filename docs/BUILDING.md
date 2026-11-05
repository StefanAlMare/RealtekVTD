# Distribution build entrypoint

The `driver/` tree is an unmodified copy of the sealed native source. Its historical `BUILD_DRIVER.command` is preserved for provenance; that wrapper expects the original development workspace and its complete historical regression closure. For this repository, use `scripts/build_native.py` instead.

The distribution script verifies the selected source manifest and every file of the vendored, pinned MacKernelSDK before generating all 22 compiler commands and the linker/package/verification commands. `dependencies/SDK-MANIFEST.json` pins the SDK files; the SDK commit is `3f750085caa17ec3a7880f11c11bf4f48cd6a164`. The SDK retains its own license and file notices.

For the accepted B source distribution, run from the repository root:

```sh
python3 scripts/build_native.py --manifest driver/source-manifest-MAC46-N64-TXPRECHECK-B.json --output "$PWD/build/dry-run" --dry-run
python3 scripts/build_native.py --manifest driver/source-manifest-MAC46-N64-TXPRECHECK-B.json --output "$PWD/build/rebuild"
```

Use a fresh output directory. Neither command installs a kext, changes a network interface, downloads dependencies, or launches traffic. The actual build requires macOS and Xcode with `xcrun`, `clang++`, `nm`, and `dwarfdump`.

This is a **build-only** entrypoint. It does not rerun the original development workspace's entire regression suite. The original test receipts document previously executed tests; they must not be described as tests newly executed by this script.

A local rebuild is not assumed to have the same UUID or SHA-256 as the accepted beta. `REBUILD-IDENTITY.json` records its actual identity and marks it as not physically validated. The beta download remains the original, previously accepted executable with its published UUID and checksum.

`BUILD-COMMAND-PARITY.json` records an offline comparison against the actual accepted B build: 28 command stages, including all 22 compilation units, match exactly after relocating only the output directory. No compiler optimization flag was added. No sealed source file or original executable was changed.

Python 3.9 or newer is required. Dry-run validates inputs and prints commands without creating output or compiling. This beta publication only performs that input/command check; the accepted image is not rebuilt.
