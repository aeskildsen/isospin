#!/usr/bin/env bash
# Development build of the static UGen sources into build/sc-static.
set -euo pipefail
cd "$(dirname "$0")/../.."
python3 -I scripts/build-sc-static.py --out build/sc-static
