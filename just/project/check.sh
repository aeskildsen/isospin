#!/usr/bin/env bash
# Fast check: builds and checks the release UGens (supercollider.toml),
# without the fuzz test (about 40 s; just test runs it),
# against SuperCollider and SuperSonic at the CI commits.
set -euo pipefail
cd "$(dirname "$0")/../.."
source just/project/pins.sh
python3 -I scripts/check-native.py --manifest supercollider.toml \
	--skip-test test/superpos_fuzz.cpp \
	--sc-include "$SC_INCLUDE" --supersonic "$SUPERSONIC"
