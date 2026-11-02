# Naruto: Clash of Ninja — static recompilation

<a href="https://ko-fi.com/jack2438"><img src="https://ko-fi.com/img/githubbutton_sm.svg" alt="Support me on Ko-fi"></a>

Native Windows and macOS builds of five GameCube games, made from your own disc. The PowerPC
code is translated to C once and linked against a runtime that implements the GameCube's
hardware, so this is not an emulator: it is an ordinary executable that runs the real game, at
6× to 9.5× the console's frame rate.

| Game | Disc ID |
| --- | --- |
| Naruto: Clash of Ninja | `GNREDA` |
| Naruto: Clash of Ninja 2 | `GNUEDA` |
| Gekitou Ninja Taisen! 3 | `G3NJDA` |
| Gekitou Ninja Taisen! 4 | `G4NJDA` |
| Naruto: Clash of Ninja - European Version | `GNUPDA` |

The game projects are not in this repository: each one's symbol map and steering tables come
from the disc, so the launcher writes the projects into its own folder and reads your own
disc for the symbol maps. A release carries no game folder — [Releases](docs/RELEASING.md)
covers what it does carry.

![The launcher with its covers](docs/screenshots/launcher.png)

*Box art is fetched on first run, never shipped.*

## Download and run

Releases are on the
[releases page](https://github.com/mapleleafjack/ClashOfNinja-recomp/releases/latest) (latest is
v1.5.1).

| Platform | Download | Also needs |
| --- | --- | --- |
| Windows 10/11 x64 | `NarutoLauncher-Windows-x64-v<version>.zip` (~4 MB) | nothing |
| macOS 14+, Apple Silicon | `NarutoLauncher-macOS-arm64-v<version>.zip` (~13 MB, tools inside) | a C++ compiler: `xcode-select --install` (free) or Xcode |
| Linux x64 | `NarutoLauncher-Linux-x64-v<version>.tar.xz` (~2 MB, SDL2 inside) | `cmake ninja-build g++`, or Linuxbrew, which needs no root on SteamOS |

1. **Unzip it.** On Windows use a path with no spaces in it, for example
   `C:\Games\NarutoLauncher` — MinGW-w64 will not run from one. Windows may also warn that the
   publisher is unknown, as the launcher is not code-signed yet: *More info → Run anyway*. On
   macOS, right-click → Open the app the first time (it is signed ad hoc, not notarised).
2. **Start the launcher.** The games appear as covers.
3. **Click a cover and choose your disc image.** The first click writes that game's folder
   into the launcher's own folder, extracts the disc and builds the game, which takes a few
   minutes; on Windows it first fetches about 190 MB of build tools, with a progress bar.
4. **Play.** A cover only chooses the game: **Play** starts it, and **Mods** changes what it
   plays — a file mod, Gecko codes, or a disc patch such as SCON4, with only the mods that
   game's disc can play listed. The table lists the ones
   `mods.json` knows even before they are downloaded, and **Apply** writes the checked rows,
   fetching any that are not on the machine. Saves and controller mappings stay in that
   game's folder.

The `NarutoLauncher-tools-windows-x64-v<version>.zip` asset is that same toolchain, for anyone
who would rather fetch it by hand than on the first build. You do not need it otherwise.

From a terminal instead of the launcher:

```sh
tools/setup.sh --run "/path/to/your disc image"   # second argument: naruto2, gnt3, gnt4, narutoeu
```

You need your own disc image either way, in any format nodtool reads: ISO, CISO, RVZ, WIA, GCZ,
WBFS, TGC or NFS. It is read once and can be deleted afterwards.

## Read more

| | |
| --- | --- |
| [Building and running](docs/BUILDING.md) | disc setup, a build from source, the command line |
| [The launcher](docs/LAUNCHER.md) | what it does, and how to build it |
| [Recompilation notes](docs/RECOMPILATION.md) | how far each game gets, how the disc reaches it, the steering tables, what each disc turned out to be |
| [Mods](docs/MODS.md) | what a mod is, and how the launcher and the runtime serve it over your own disc |
| [Releases](docs/RELEASING.md) | CI, the Windows and macOS packages, and what may be published |

## Licensing

MIT © 2026 Jack Musajo; see [LICENSE](LICENSE). What it builds on — gcn-recomp, glad, SDL2, stb —
is listed in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) and `licenses/`.

**No game data is included, in any form.** You need your own disc.

**Unofficial.** Not affiliated with, endorsed by or licensed by Nintendo, Shueisha, or the game's
developers and publishers.
