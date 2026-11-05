#!/usr/bin/env python3
"""Build the sealed beta source without installation or network access.

This distribution-only entrypoint reproduces the accepted build's native
compiler and linker commands. It does not replace the historical regression
gate or claim that a local rebuild has been physically validated.
"""
import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import subprocess
import sys

SDK_COMMIT = "3f750085caa17ec3a7880f11c11bf4f48cd6a164"


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(ok, message):
    if not ok:
        raise SystemExit(message)


def check_manifest(root, manifest_path):
    manifest = json.loads(manifest_path.read_text())
    files = manifest["files"]
    for rel, expected in files.items():
        path = (root / rel).resolve()
        require(path.is_relative_to(root), "Unsafe manifest path: " + rel)
        require(path.is_file(), "Missing source: " + rel)
        expected_hash = expected if isinstance(expected, str) else expected["sha256"]
        require(sha256(path) == expected_hash, "Source hash mismatch: " + rel)
    actual = {str(p.relative_to(root)) for p in root.rglob("*") if p.is_file()}
    expected_set = set(files) | {str(manifest_path.relative_to(root))}
    require(actual == expected_set, "Source file set differs from sealed manifest")
    return manifest


def make_commands(source, sdk, output):
    flags = ["-arch", "x86_64", "-mmacosx-version-min=10.15", "-std=c++14", "-mkernel",
             "-DKERNEL", "-DKERNEL_PRIVATE", "-fapple-kext", "-fno-exceptions", "-fno-rtti",
             "-fno-common", "-fno-stack-protector", "-nostdinc", "-nostdinc++",
             "-isystem", str(sdk / "Headers"), "-I", str(source / "src"),
             "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter"]
    sources = sorted((source / "src").glob("*.cpp")) + sorted((source / "macos").glob("*.cpp"))
    require(len(sources) == 22, "Expected all 22 native translation units")
    objects = []
    commands = [("xcode-version", ["xcodebuild", "-version"]),
                ("compiler-version", ["xcrun", "clang++", "--version"])]
    for unit in sources:
        obj = output / "native-objects" / (unit.parent.name + "-" + unit.stem + ".o")
        commands.append(("compile-" + obj.stem,
                         ["xcrun", "clang++", *flags, "-c", str(unit), "-o", str(obj)]))
        objects.append(str(obj))
    kext = output / "Release/RealtekVTDNextEthernet.kext"
    binary = kext / "Contents/MacOS/RealtekVTDNextEthernet"
    commands.extend([
        ("link", ["xcrun", "clang++", "-arch", "x86_64", "-mmacosx-version-min=10.15", "-nostdlib",
                  "-Xlinker", "-kext", "-Xlinker", "-undefined", "-Xlinker", "dynamic_lookup",
                  *objects, str(sdk / "Library/x86_64/libkmod.a"), "-o", str(binary)]),
        ("package", [sys.executable, "-B", str(source / "tools/package_probe.py"), "--kext", str(kext),
                     "--firmware-dir", str(source / "firmware")]),
        ("verify", [sys.executable, "-B", str(source / "tools/verify_kext.py"), str(kext)]),
        ("undefined-kernel-imports", ["nm", "-u", str(binary)]),
    ])
    return commands, binary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    root = Path(__file__).resolve().parents[1]
    parser.add_argument("--source", type=Path, default=root / "driver")
    parser.add_argument("--sdk", type=Path, default=root / "dependencies/MacKernelSDK")
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--sdk-manifest", type=Path, default=root / "dependencies/SDK-MANIFEST.json")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--dry-run", action="store_true", help="Verify inputs and print commands; do not compile")
    args = parser.parse_args()
    source, sdk, output = args.source.resolve(), args.sdk.resolve(), args.output.resolve()
    manifest = args.manifest.resolve()
    require(not output.exists(), "Output already exists; use a new directory")
    require(not output.is_relative_to(source) and not output.is_relative_to(sdk),
            "Output must be outside the sealed source and SDK")
    verified_source = check_manifest(source, manifest)
    sdk_manifest = json.loads(args.sdk_manifest.read_text())
    require(sdk_manifest["commit"] == SDK_COMMIT, "Unexpected SDK commit")
    sdk_files = sdk_manifest["files"]
    actual_sdk = {str(p.relative_to(sdk)) for p in sdk.rglob("*") if p.is_file()}
    require(actual_sdk == set(sdk_files), "SDK file set differs from pinned manifest")
    for rel, wanted in sdk_files.items():
        path = (sdk / rel).resolve()
        require(path.is_relative_to(sdk), "Unsafe SDK path")
        require(sha256(path) == wanted, "SDK hash mismatch: " + rel)
    commands, binary = make_commands(source, sdk, output)
    if args.dry_run:
        print(json.dumps({"status": "DRY_RUN_INPUTS_VERIFIED", "source_entries": len(verified_source["files"]),
                          "sdk_entries": len(sdk_files), "SDK_commit": SDK_COMMIT,
                          "commands": [{"stage": stage, "argv": argv} for stage, argv in commands]}, indent=2))
        return
    (output / "native-objects").mkdir(parents=True)
    (output / "native-logs").mkdir()
    binary.parent.mkdir(parents=True)
    receipts = []
    for stage, argv in commands:
        result = subprocess.run(argv, cwd=source, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        (output / "native-logs" / (stage + ".log")).write_text(result.stdout)
        receipts.append({"stage": stage, "argv": argv, "exit_code": result.returncode})
        (output / "NATIVE_BUILD_COMMANDS.json").write_text(json.dumps(receipts, indent=2) + "\n")
        require(result.returncode == 0, "Build failed: " + stage + "; see native-logs")
    imports = {line.split()[-1] for line in (output / "native-logs/undefined-kernel-imports.log").read_text().splitlines() if line.strip()}
    require(not any("__ZN12realtek_next" in symbol for symbol in imports), "Unresolved internal symbol")
    uuid = subprocess.run(["xcrun", "dwarfdump", "--uuid", str(binary)], check=True,
                          stdout=subprocess.PIPE, text=True).stdout.strip()
    info = plistlib.loads((binary.parent.parent / "Info.plist").read_bytes())
    receipt = {"status": "NATIVE_REBUILD_BUILT_NOT_PHYSICALLY_VALIDATED", "build": verified_source["build"],
               "binary_sha256": sha256(binary), "uuid_output": uuid, "binary_bytes": binary.stat().st_size,
               "native_translation_units": 22, "kernel_imports": sorted(imports), "kernel_import_count": len(imports),
               "bundle_id": info["CFBundleIdentifier"], "SDK_commit": SDK_COMMIT,
               "regression_tests_executed_by_this_script": False, "installation": False, "traffic": False,
               "note": "A rebuild is not assumed byte-identical to the accepted beta. Compare its receipt before use."}
    (output / "REBUILD-IDENTITY.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt, indent=2))


if __name__ == "__main__":
    main()
