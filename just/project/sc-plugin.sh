#!/usr/bin/env bash
# Builds the loadable SuperCollider plugin into build/sc-plugin/isospin, with
# the SuperCollider headers at the CI commit. Then loads it in scsynth and
# supernova and renders each UGen (skipped without sclang on PATH).
set -euo pipefail
cd "$(dirname "$0")/../.."
source just/project/pins.sh
python3 -I scripts/build-sc-plugin.py --sc-include "$SC_INCLUDE"
if command -v sclang >/dev/null; then
	python3 -I scripts/check-sc-plugin.py
else
	echo "check-sc-plugin: SKIP (no sclang; the plugin was not loaded)"
fi
