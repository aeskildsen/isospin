#!/usr/bin/env bash
# The scripts that source this file use SC_INCLUDE and SUPERSONIC.
# shellcheck disable=SC2034
# Gets SuperCollider and SuperSonic at the commits that CI uses into
# build/pins, and sets SC_INCLUDE and SUPERSONIC to them. The commits come
# from .github/workflows/check.yml, so the local checks and CI cannot differ.
# A folder that is already at its commit is used again. Source this file.

pin() {  # pin <name>: the value of <name> in check.yml
	sed -n "s/^ *$1: *//p" .github/workflows/check.yml | head -n 1
}

fetch() {  # fetch <repo> <commit> <folder>: one commit, no history
	if [ "$(git -C "$3" rev-parse HEAD 2>/dev/null)" = "$2" ]; then
		return
	fi
	rm -rf "$3"
	git init -q "$3"
	git -C "$3" fetch -q --depth 1 "$1" "$2"
	git -C "$3" checkout -q FETCH_HEAD
}

fetch "$(pin SC_REPO)" "$(pin SC_COMMIT)" build/pins/supercollider
fetch "$(pin SUPERSONIC_REPO)" "$(pin SUPERSONIC_COMMIT)" build/pins/supersonic
SC_INCLUDE=$PWD/build/pins/supercollider/include
SUPERSONIC=$PWD/build/pins/supersonic
