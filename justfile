# Requires just 1.23.0 or later.
import 'just/loader.just'

# List available recipes
default:
    @just --list

# Fast check: the release UGens without the fuzz test, against the CI commits of SuperCollider and SuperSonic, and the help pages
check:
    bash just/project/check.sh

# Build the loadable SuperCollider plugin into build/sc-plugin/isospin, then load and render it in scsynth and supernova
sc-plugin:
    bash just/project/sc-plugin.sh

# Link build/sc-plugin/isospin into your SuperCollider user extension folder
sc-install:
    bash just/project/sc-install.sh

# A/B render of two versions of a sound in NRT, with a comparison (see tools/ab/README.md)
ab config:
    bash tools/ab/ab.sh {{config}}
