# isospin for SuperCollider

UGens from [isospin](https://github.com/aeskildsen/isospin), built as a SuperCollider plugin for scsynth and supernova. `BUILDINFO` gives the version and the commit.

| Class | What it is |
|---|---|
| `IsoWoodbar` | A struck wooden bar: claves, wood stick, xylophone or marimba bar. |
| `IsoSuperPos` | Granular synthesis from a mono buffer, with overlapping grains in stereo. |
| `IsoSuperPosLive` | The grain engine of `IsoSuperPos` on a circular record buffer, with freeze. |
| `IsoPulsar` | Pulsar synthesis: three trains of sine grains (a fundamental and two formants) in stereo, shaped by an energy input. |

The help page of each class has its arguments and examples.

Each class starts with `Iso`, so that it does not have the same name as a class of another extension. Up to isospin 0.6.0 the classes were `PhysWoodbar`, `Pulsar`, `SuperPos` and `SuperPosLive`. Only the names changed: put `IsoWoodbar`, `IsoPulsar`, `IsoSuperPos` and `IsoSuperPosLive` in your code.

## Install

Needs SuperCollider 3.14 or later.

1. In SuperCollider, run `Platform.userExtensionDir.openOS;` to open the folder of your extensions. If the folder does not exist, make it.
2. Copy the `isospin` folder into it.
3. Recompile the class library (Language > Recompile Class Library), then boot the server.

**macOS:** the plugin is not notarized. Before the first boot, remove the quarantine flag from the downloaded files. In Terminal:

```sh
xattr -dr com.apple.quarantine "<the extension folder>/isospin"
```

**Remove an earlier copy.** If you have another copy of `IsoWoodbar`, `IsoPulsar`, `IsoSuperPos` or `IsoSuperPosLive` in your extensions (for example an older isospin), remove it first. Two class files with the same class stop the class library.

## Licence

AGPL-3.0-or-later (see `LICENSE`). `NOTICE.md` gives the licences of the parts of the source code. The plugins are built from the release `isospin-sc-static-<version>.tar.gz` of the same GitHub release, which is the complete source code. The repository has the scripts that make it.
