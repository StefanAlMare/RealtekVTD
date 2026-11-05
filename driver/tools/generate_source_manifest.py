#!/usr/bin/env python3
import hashlib
import json
import pathlib

root = pathlib.Path(__file__).resolve().parents[1]
excluded_roots = {"build", ".git"}
excluded_files = {"source-manifest.json", ".DS_Store", "PHYDIAG-LINEAGE.bundle", "AUTONEG-LINEAGE.bundle", "GIT_LINEAGE.txt", "build-sdk.log"}
manifest = {}
for path in sorted(root.rglob("*")):
    if not path.is_file():
        continue
    relative = path.relative_to(root)
    if "__pycache__" in relative.parts or path.suffix == ".pyc" or relative.parts[0] in excluded_roots or relative.as_posix() in excluded_files:
        continue
    manifest[relative.as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
(root / "source-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
print(f"Wrote {len(manifest)} source hashes")
