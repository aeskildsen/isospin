#!/usr/bin/env bash
# The CI check job: the release UGens, then the test UGens, against
# SuperCollider and SuperSonic at the CI commits.
set -euo pipefail
cd "$(dirname "$0")/../.."
source just/project/pins.sh
python3 -I scripts/check-native.py --manifest supercollider.toml \
	--sc-include "$SC_INCLUDE" --supersonic "$SUPERSONIC"
python3 -I scripts/check-native.py \
	--sc-include "$SC_INCLUDE" --supersonic "$SUPERSONIC"
