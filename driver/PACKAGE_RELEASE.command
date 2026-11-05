#!/bin/bash
set -euo pipefail
root="$(cd "$(dirname "$0")" && pwd)"
name="$(basename "$root")"
parent="$(dirname "$root")"
archive="$parent/$name.zip"

cd "$root"
python3 tools/generate_source_manifest.py
[ ! -e "$archive" ] || { echo "Refusing to overwrite existing archive: $archive"; exit 1; }
cd "$parent"
/usr/bin/zip -qr "$archive" "$name" \
  -x "$name/build/deps/*" \
     "$name/build/obj/*" \
     "$name/build/tests/*" \
     "$name/build/write-audit/probe-*" \
     "$name/build/local-evidence/*" \
     "$name/**/__pycache__/*" \
     "$name/**/.DS_Store" \
     "$name/.DS_Store"
/usr/bin/shasum -a 256 "$archive"
