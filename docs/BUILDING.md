# Building and running

[README](../README.md) · [The launcher](LAUNCHER.md) · [Recompilation notes](RECOMPILATION.md) ·
[Releases](RELEASING.md)

## Setup (once, per game)

Each game needs its own disc image, and none may be committed or shipped. Any format
[nodtool](https://github.com/encounter/nod) reads works:

`ISO` · `CISO` · `RVZ` · `WIA` · `GCZ` · `WBFS` · `TGC` · `NFS`

```sh
cargo install --locked nodtool        # once, if your image is not a plain ISO/CISO

tools/setup.sh "/path/to/clash of ninja.iso"            # naruto (the default)
tools/setup.sh "/path/to/clash of ninja 2.iso" naruto2
tools/setup.sh "/path/to/gekitou ninja taisen 3.iso" gnt3
tools/setup.sh "/path/to/gekitou ninja taisen 4.iso" gnt4
```

That writes `<game>/rom/disc/` — `sys/main.dol`, `sys/fst.bin`, `files/…` and a
`disc.manifest` of disc offsets — then reads the tree back through the manifest and compares
every file with the image, refusing to continue if `main.dol` is not the pinned build for that
game.

**The image is not needed after that:**

```sh
rm "/path/to/clash of ninja 2.iso"
```

`<game>/rom/disc/disc.manifest` is what makes that possible: it records the disc offset each
extracted file came from, and the runtime serves the console's DVD reads from it. See
[how the disc reaches the game](RECOMPILATION.md#how-the-disc-reaches-the-game).

## Build and run

`tools/setup.sh` does the whole job, and `--run` starts the game when the build succeeds:

```sh
tools/setup.sh --run "/path/to/your disc image"
```

Step by step, which is all the launcher itself does:

```sh
cd naruto
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build
./build/naruto            # or: ./build/naruto_bench --seconds=10
```

`naruto2/`, `gnt3/` and `gnt4/` build the same way, each producing `./build/<game>` and
`./build/<game>_bench`. All four share the runtime and recompiler in `naruto/gcn-recomp/`
rather than copying them.

`cmake` picks up `rom/disc` on its own. `-DGAME_ISO=/path/to/image.iso` also works, as does
putting an `.iso`/`.ciso` in `rom/` instead of the extracted tree.

### Without the launcher, from a release

A release can be driven from a terminal. Download one from the
[releases page](https://github.com/mapleleafjack/ClashOfNinja-recomp/releases/latest). On macOS
everything lives in the app:

```sh
"/Applications/Naruto Launcher.app/Contents/Resources/project/tools/setup.sh" \
    --run "/path/to/your disc image"
```

That copies the sources out of the bundle to `~/Naruto` (a signed bundle must not be written
into), extracts the disc there, and builds with the CMake, Ninja, SDL2 and nodtool the app
carries — no Homebrew, no `pkg-config`. The only thing still needed is the compiler from
Apple's command line tools. Afterwards the game starts with `~/Naruto/naruto/build/naruto`,
and `tools/setup.sh` in the copy can be re-run at any time. Set `NARUTO_HOME` to put the copy
elsewhere, or `NARUTO_TOOLS` to use another tool folder.

On Windows the release folder is already a checkout without the disc, so the same thing is
done by hand:

```powershell
$env:PATH = "$PWD\tools\mingw64\bin;$PWD\tools\cmake\bin;$PWD\tools\ninja;$PWD\tools\python;$env:PATH"
.\tools\python\python.exe .\tools\extract_disc.py "D:\your disc image.iso"
.\tools\cmake\bin\cmake.exe -S naruto -B naruto\build -G Ninja `
    -DCMAKE_BUILD_TYPE=RelWithDebInfo -DGCN_SDL2_PREFIX="$PWD\tools\sdl2" -DGCN_OPENXR=OFF
.\tools\ninja\ninja.exe -C naruto\build
.\naruto\build\naruto.exe
```

Under MinGW-w64 the game is linked with `-static -static-libgcc -static-libstdc++`, so the
built `naruto.exe` carries the C++ runtime: it starts on its own, with no toolchain DLLs
beside it or on `PATH`.

## Command line

The game takes these; the launcher passes the play-related ones through.

| Option | What it does |
| --- | --- |
| `--scale=N` | window scale, default 2 (640×480 × N) |
| `--no-music` | mute the game's background music |
| `--no-input-log` | do not write input logs |
| `--players=N` | controllers to poll, 1 to 4 |
| `--controls=FILE` | control mapping, default `controls.txt` |
| `--list-devices` | list the controllers the runtime sees, and exit |
| `--headless` | run without a window, for measuring |
| `--seconds=N` | how long `<game>_bench` runs |
| `--dump-dir=DIR --dump-every=N` | write every Nth presented frame as PNG |
| `--record=out.mp4 [--record-size=WxH]` | record the window with the game's audio |
| `--log=CATEGORIES`, `--log-all` | debug logging: `cpu`, `os`, `hw`, `dvd`, `gx`, `vi`, `si`, `exi`, `dsp`, `ai`, `thr` |

Environment:

| Variable | What it does |
| --- | --- |
| `GCN_INPUT="frame:BUTTON:duration,..."` | scripted input, e.g. `"900:START:20,1200:A:10"` |
| `GCN_IPL_FONT=FILE` | serve your own IPL font blob at `0x1FCF00` |
| `GCN_TELEMETRY=host:port` | send the game's motion to a motion platform |

## Notes

- Each game's `saves/` holds its memory card image and input logs.
- Dumps come from the present path, so they need a window: `--headless` never presents a frame
  (it discards each batch) and is for measuring, not looking. `--headless` runs until it is
  killed; there is no time limit in the app, `--seconds=N` belongs to `<game>_bench`. `SIGINT`
  dumps the GX FIFO and exits, which is why a headless run ends with a FIFO dump and exit
  status 1 — that is the interrupt handler, not a crash.
- `--record=out.mp4` needs ffmpeg with an encoder it can open. Homebrew's ffmpeg failed all
  three trial encoders here, so `--dump-dir` is the safer choice.
- Some text on screen is the SDK's, not the game's: the memory card prompts read their font
  from the IPL ROM at `0x1FCF00`, which a runtime with no ROM does not have. The built-in
  replacement is a public-domain 8×8 set, so those prompts have words. `GCN_IPL_FONT=FILE`
  serves a blob of your own from that address instead — the Yay0 font the ROM holds, the same
  shape as Dolphin's bundled fonts.
- Nothing from the disc is redistributable. Every game's `rom/`, `build/` and `saves/`,
  `controls.txt` and `config/<disc ID>/` are gitignored, as is `covers/`, which holds the
  publisher's box art. See [Releases](RELEASING.md#publishing).
