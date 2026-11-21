#!/usr/bin/env bash
# Puts a link to build/sc-plugin/isospin into the user extension folder of
# SuperCollider, so that each new build of `just sc-plugin` is used after a
# recompile of the class library. Stops if the folder has a different
# isospin, or another copy of a class of isospin.
set -euo pipefail
cd "$(dirname "$0")/../.."
plugin=$PWD/build/sc-plugin/isospin
if [ ! -d "$plugin/plugins" ]; then
	echo "sc-install: no build in $plugin; run just sc-plugin first" >&2
	exit 1
fi
case "$(uname -s)" in
Darwin) extensions="$HOME/Library/Application Support/SuperCollider/Extensions" ;;
*) extensions="${XDG_DATA_HOME:-$HOME/.local/share}/SuperCollider/Extensions" ;;
esac
link=$extensions/isospin

# Two class files with the same class stop the class library.
others=$(find -L "$extensions" -path "$link" -prune -o \
	\( -name PhysWoodbar.sc -o -name SuperPos.sc -o -name SuperPosLive.sc -o -name Pulsar.sc \) -print 2>/dev/null || true)
if [ -n "$others" ]; then
	echo "sc-install: other copies of isospin classes are installed; remove them first:" >&2
	echo "$others" >&2
	exit 1
fi

if [ -L "$link" ] && [ "$(readlink "$link")" = "$plugin" ]; then
	echo "sc-install: $link already points to $plugin"
	exit 0
fi
if [ -e "$link" ] || [ -L "$link" ]; then
	echo "sc-install: $link exists and is not a link to $plugin; remove it first" >&2
	exit 1
fi
mkdir -p "$extensions"
ln -s "$plugin" "$link"
echo "sc-install: $link -> $plugin"
echo "Recompile the class library in SuperCollider, then boot the server."
