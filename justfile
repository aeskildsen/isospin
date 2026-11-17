# Requires just 1.23.0 or later.
import 'just/loader.just'

# List available recipes
default:
    @just --list

# Fast check: the release UGens without the fuzz test, against the CI commits of SuperCollider and SuperSonic, and the help pages
check:
    bash just/project/check.sh
