#!/usr/bin/env bash
# just ab <config>: builds the UGens of this tree as a loadable scsynth plugin
# (scripts/build-sc-plugin.py, without supernova), renders both sides of the
# config in NRT and compares them. The renders go to
# build/ab/<config name>/a.wav and b.wav. Needs sclang and scsynth on PATH.
set -euo pipefail
config=$(realpath "$1")
cd "$(dirname "$0")/../.."
command -v sclang >/dev/null || { echo "ab: needs sclang on PATH" >&2; exit 1; }
source just/project/pins.sh

name=$(basename "$config" .scd)
work=$PWD/build/ab/plugin
out=$PWD/build/ab/$name
mkdir -p "$out"
python3 -I scripts/build-sc-plugin.py --sc-include "$SC_INCLUDE" --no-supernova \
	--out "$work" --build "$work-cmake" >"$work.log" 2>&1 \
	|| { cat "$work.log" >&2; exit 1; }

# sclang compiles only its own class library and the class files of this
# build, not the installed extensions: an installed copy of an isospin class
# stops the compile. AB_CLASSLIB can give the class library folder.
classlib=${AB_CLASSLIB:-$(dirname "$(readlink -f "$(command -v sclang)")")/../share/SuperCollider/SCClassLibrary}
[ -d "$classlib" ] || { echo "ab: no class library at $classlib; set AB_CLASSLIB" >&2; exit 1; }
lang=$PWD/build/ab/sclang.yaml
printf 'excludeDefaultPaths: true\nincludePaths:\n  - %s\n  - %s\n' "$(realpath "$classlib")" "$work/isospin/Classes" >"$lang"

AB_CONFIG=$config AB_OUT=$out AB_PLUGINS=$work/isospin/plugins QT_QPA_PLATFORM=offscreen \
	sclang -l "$lang" tools/ab/ab.scd >"$out/sclang.log" 2>&1 \
	|| { tail -n 30 "$out/sclang.log" >&2; exit 1; }
grep '^ab.scd:' "$out/sclang.log" || true
[ -f "$out/a.wav" ] && [ -f "$out/b.wav" ] || { tail -n 30 "$out/sclang.log" >&2; exit 1; }
python3 -I tools/ab/compare.py "$out/a.wav" "$out/b.wav"
echo "ab: renders in $out"
