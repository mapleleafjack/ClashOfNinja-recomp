# Recompilation notes

[README](../README.md) · [Building and running](BUILDING.md) · [The launcher](LAUNCHER.md) ·
[Releases](RELEASING.md)

## How the disc reaches the game

gcn-recomp's runtime answers the console's DVD reads *by disc offset*, so an extracted
directory alone is not enough: `tools/extract_disc.py` also writes `disc.manifest`, one line
per file giving its disc offset, and `runtime/hw/di.cpp` serves `iso_read()` from the tree —
files from their own extents, the padding between them as zeroes.

The result is checked, not assumed. After extracting, the setup reads the tree back through the
manifest and compares it with the image, and the guest's read sequence from the tree and from
the image are identical: 173 of 173 reads, offset and length, over the opening movie's first
40 seconds.

**The disc's FST does not always say what a disc's FST says.** Retail discs store each file's
offset divided by four (files are four-byte aligned). Both images here — the NKit one and the
full-size one — store them as they are, so the setup probes for which reading lands on real
data instead of trusting the convention. The other reading points at padding, which is what
makes the test conclusive.

## Steering tables

Everything in each project's `recomp/` follows the reference projects:

| File | What it steers |
| --- | --- |
| `hle.txt` | functions the runtime implements instead of running: `OSSaveContext`, `OSLoadContext`, `OSReport`, `OSPanic`, `OSResetSystem` |
| `idle.txt` | the backward branch of the SDK scheduler's idle spin inside `SelectThread`, so the runtime can skip guest time |
| `special_calls.txt` | where gcn-recomp emits a `setjmp`, so `OSLoadContext` can resume a saved thread |

**The one thing that made it run instead of dying: `special_calls.txt` holds the address of
`OSSaveContext` itself, not of the `bl` that calls it.** gcn-recomp emits a `setjmp` at every
`bl` to an address listed in `special_calls.txt`. Listing the call site (`800E94F8`, what you
get from searching the disassembly for the `bl`) compiles and links, then faults the moment
`SelectThread` switches to a saved context: `longjmp` to a `jmp_buf` that was never filled,
jumping to host address `0x8`. Listing `800E4184` — the function — fixes it. Symptom, for
reference: `FAULT: host address 0x8`, `lr=800E94FC`, host thread stopped inside
`SelectThread`.

## Analysis

The dtk outputs are not needed to build or run, only to work on the recompilation. They are
derived from the disc, so they are not committed. `dtk` fetches itself with
`naruto/gcn-recomp/tools/fetch_dtk.sh`; run from the project root:

```sh
DTK=naruto/gcn-recomp/tools/dtk
$DTK dol config naruto/rom/disc/sys/main.dol -o config/GNREDA/config.yml    # once, per game
$DTK dol config naruto2/rom/disc/sys/main.dol -o config/GNUEDA/config.yml
$DTK dol config gnt3/rom/disc/sys/main.dol -o config/G3NJDA/config.yml
$DTK dol config gnt4/rom/disc/sys/main.dol -o config/G4NJDA/config.yml

# `dtk dol config` writes only the object and its hash. Add the two outputs to the file,
# or `dtk dol split` has nowhere to put the symbols it makes:
#   symbols: build/G4NJDA/symbols.txt
#   splits: build/G4NJDA/splits.txt

$DTK dol split config/GNREDA/config.yml build/GNREDA                        # 0.6s
$DTK dol split config/GNUEDA/config.yml build/GNUEDA                        # 0.7s
$DTK dol split config/G3NJDA/config.yml build/G3NJDA                        # 0.7s
$DTK dol split config/G4NJDA/config.yml build/G4NJDA                        # 0.7s
```

The symbols for a game are `build/<ID>/symbols.txt`; each project's `analysis/symbols.txt` is
that file with the functions noted below named by hand.

## What each disc turned out to be

| Game | `main.dol` SHA-1 | dtk functions | Image |
| --- | --- | --- | --- |
| Clash of Ninja | `19251f7f15f8b6f7e0cd1c753409151ec49d9af2` | 4015 | NKit |
| Clash of Ninja 2 | `2440b77fad91e1892d1769ff8650852244e64415` | 4959 | trimmed ISO |
| Gekitou Ninja Taisen! 3 | `e8e0a27f6a4a075a11ef322cf6a182c0b87c8d51` | 5062 | full-size ISO |
| Gekitou Ninja Taisen! 4 | `0eb4cc3bd4c7363f8cb7baff79de7a964cbdeadb` | 5312 | full-size ISO |

Each hash is pinned in that game's `CMakeLists.txt`.

- **The first disc is an NKit image, and dtk reads it directly.** `dtk disc info` reports
  `Format: ISO / Lossless: true`; no conversion to a plain ISO was needed. gcn-recomp's own
  `extract_dol.py` only understands uncompressed `.iso`/`.ciso`, but it reads the NKit file
  fine too because the header and DOL are never compressed, only the unused padding. A
  full-size ISO of the same release extracts to byte-identical content, so either works; the
  only files that differ are `boot.bin` (NKit writes its own magic at `0x200`) and `fst.bin`
  (each image's own layout).
- **dtk left part of the SDK unnamed.** In the first game `OSSaveContext` came out as
  `fn_800E4184` and `OSPanic` as `fn_800E48E4`; both were identified from their disassembly
  (GQR/CR/LR/MSR saves into an `OSContext`; `OSDisableInterrupts` + `vprintf` + `OSReport` +
  `PPCHalt`) and renamed in `naruto/analysis/symbols.txt`.
- **The other three games were confirmed rather than guessed.** dtk named the thread and report
  functions itself (`OSLoadContext`, `OSReport`, `OSResetSystem`, `SelectThread`) but left
  `OSSaveContext` and `OSPanic` as `fn_…` in each. The SDK is unchanged across the four discs:
  `OSSaveContext`'s body is byte-for-byte the same, and `OSPanic` sits directly after `OSReport`
  at `+0x80`, the same size, calling `OSDisableInterrupts`, `vprintf`, `OSReport` and `PPCHalt`.
  Each project names them in `analysis/symbols.txt` and lists the function in
  `recomp/special_calls.txt`.
- **The idle spin is at the same offset in every game**: the backward branch inside
  `SelectThread`, marked in each project's `recomp/idle.txt` (`800E954C`, `800FA2E0`,
  `8013DB3C`, `8016C300`).
- **No disc has relocatable modules.** Each carries `audio`, `fpack` and `movie` plus
  `opening.bnr`; nothing is loaded with `OSLink`, so `RELS` and `REL_RUNTIME` are unused.

## Status

| Game | Runs | What works |
| --- | --- | --- |
| Clash of Ninja | yes | boot, SDK init, DVD/FST reads, both logo movies, the opening H4M movie (full-frame video and audio), title screen with 3D scene, music and sound effects, in-game 3D with the HUD |
| Clash of Ninja 2 | yes | the same up to the Viz logo movie and title menu, scripted menu input through Single Player, and a Naruto vs Neji match with the HUD and arena in 3D |
| Gekitou Ninja Taisen! 3 | yes | boot, SDK init, DVD/FST reads, the Eighting logo movie, the opening H4M movie, the title screen |
| Gekitou Ninja Taisen! 4 | yes | boot, SDK init, DVD/FST reads, the Eighting logo movie, the title screen's live 3D attract scene behind `PRESS START BUTTON`, character select, the opening H4M movie |

Headless, with the game at its title screen or in a match, measured with `<game>_bench`:

| Game | Headless | Times the console's 60 fps |
| --- | --- | --- |
| Clash of Ninja (`GNREDA`) | ~570 fps | 9.5× |
| Clash of Ninja 2 (`GNUEDA`) | ~467 fps | 7.8× |
| Gekitou Ninja Taisen! 3 (`G3NJDA`) | ~420 fps | 7× |
| Gekitou Ninja Taisen! 4 (`G4NJDA`) | ~420 fps, ~345 steady | 7× |

With a window, a Naruto vs Kakashi match in Clash of Ninja rendered at ~52 fps in 1280×960.

What is not done yet:

- **Gameplay has not been driven with a controller here** for the first game, so inputs and the
  battle logic beyond what the game does on its own are untested. An earlier "crash around the
  title screen" was not reproduced; it looks like two concurrent instances sharing `saves/`,
  plus a `SIGINT` reading as a crash (the interrupt handler dumps the GX FIFO and exits 1).
- **Each game's steering tables are its predecessor's**, with its own addresses: the five HLE
  replacements, one `special_calls` entry and one idle loop. Nothing has been added for what a
  later game does differently, and the menus of the last three have not been driven beyond
  entering a match or a character select.

## Layout

| Path | What it is |
| --- | --- |
| `tools/extract_disc.py` | one-time extraction of the user's own disc image into `<game>/rom/disc` |
| `tools/setup.sh` | that, then the build; `--run` starts the game, and the game folder is an optional second argument |
| `tools/check_publish.py` | proves nothing derived from the disc is in what would be published |
| `launcher/` | the desktop launcher: one C++ application — extract the disc, build, play |
| `naruto/`, `naruto2/`, `gnt3/`, `gnt4/` | the recompilation projects, the things that build and run |
| each project's `analysis/symbols.txt` | dtk symbols, with the SDK functions dtk missed named by hand |
| each project's `recomp/*.txt` | steering tables: which functions to replace at runtime, and so on |
| each project's `rom/disc/` | the extracted disc. Derived from the user's disc; not committed |
| `covers/` | box art, one file per game. Belongs to the publisher; not committed |
| `config/<disc ID>/`, `build/<disc ID>/` | dtk project config and split output, when you generate them. Not committed |
| `<game>/build/<game>`, `<game>/build/<game>_bench` | the apps and their headless benchmarks |
