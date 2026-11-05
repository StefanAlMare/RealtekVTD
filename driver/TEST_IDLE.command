#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
python3 tools/idle_test.py
