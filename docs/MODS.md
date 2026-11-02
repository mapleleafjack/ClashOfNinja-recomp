# Mods

[README](../README.md) · [The launcher](LAUNCHER.md) · [Building and running](BUILDING.md) ·
[Recompilation notes](RECOMPILATION.md) · [Releases](RELEASING.md)

A mod changes the game without touching the disc the player owns: it replaces a file in the
copy the launcher extracted, or it writes values and instructions the way a Gecko code does
on a console. The runtime answers the game's DVD reads *by disc offset*, and it runs a mod's
Gecko codes once a frame, so both kinds are a list of things to do at boot — nothing is
patched into the game's files, and switching a mod off is deleting a list.

## What a mod is

A folder in `mods/`, beside the game folders:

```
mods/
  retuned-opening/
    mod.txt
    disc/
      files/opening.bnr        # replaces files/opening.bnr on the disc
      sys/main.dol             # any file the tree has, sys/ included
    gecko.txt                  # optional: the codes it switches on
```

`disc/` mirrors a game's `rom/disc/`: the same paths, the same layout. `gecko.txt` holds
codes as a Gecko editor writes them. Nothing else in the folder is read.

`mod.txt` is `key = value` lines, `#` starting a comment:

```
name = Retuned Opening
version = 1.0
author = someone
disc_id = G4NJDA
description = Swaps the banner in the opening movie.
gecko = codes/tuning.txt      # optional; gecko.txt is used when this names none
```

`disc_id` names the builds the mod may be used with, one or more of `GNREDA`, `GNUEDA`,
`G3NJDA`, `G4NJDA` and `GNUPDA`. **A build's addresses are its own**, so a mod is only ever
offered for the discs it names, and a code is only ever run on one of them: five discs, five
address spaces, and nothing crosses between them.

## The catalog: `mods.json`

Beside the launcher is `mods.json`, the list of mods it can offer before they are
downloaded. It ships with the launcher and is plain JSON:

```json
{
  "version": 1,
  "mods": [
    {
      "id": "scon4",
      "name": "SCON4",
      "version": "2.1.0",
      "author": "SCON4 team",
      "description": "Super Clash of Ninja 4, played as a patch over a retail GNT4 disc.",
      "disc_id": ["G4NJDA"],
      "kind": "patch",
      "release": {
        "owner": "NicholasMoser",
        "repo": "SCON4-Releases",
        "asset": "patches.zip",
        "file": "vanilla.xdelta"
      }
    }
  ]
}
```

- `id` names the folder under `mods/`, or the patch's file in `patches/`.
- `disc_id` is required: an entry is only shown for the builds it names.
- `kind` is `patch` for an xdelta of the whole disc, and `mod` (the default) for a folder of
  replaced files.
- `release` names a GitHub release: the newest one is asked for, `asset` is downloaded, and
  `file` is looked for inside it (a patch's delta). For a mod, `folder` names the folder
  inside the archive; the folder holding `mod.txt` is used when it is not named.
- `url` names an archive directly, for a mod published somewhere other than a release.

A mod folder dropped into `mods/` by hand needs no entry: it appears in the table from its
own `mod.txt`, and the catalog only says where a row comes from when its files are missing.

## Files: what the launcher does with `disc/`

The **Mods** button beside **Play** opens the table for the game that is selected, and only
what that game's disc can play is in it: the folders in `mods/` whose `mod.txt` names its ID,
the disc patches in `patches/` that `mods.json` names for it (a delta no entry names is a row
of its own), and the known mods in `mods.json` that have not been downloaded. Each row has a
checkbox;
checking a row stages it, and **Apply** is what changes the game:

- A checked mod that is not on this machine yet is downloaded and unpacked into `mods/`
  first, so a row can be chosen from the catalog before it has any files.
- A checked patch that is not in `patches/` yet is fetched the same way.
- Anything unchecked is removed: the overlay and `patch.manifest` are rewritten from the
  table, so a mod or patch that was on and is not checked is off.
- Closing the panel without Apply writes nothing.

The applied choice is remembered per game, and the mods are written to
`rom/disc/mods.manifest` beside `disc.manifest`:

```
overlay <disc offset> <size> <available> <path>
```

That file is the whole of a mod's files as far as the runtime is concerned. Switching a mod
off deletes it, so nothing about the extracted disc changes; a mod is on or off at the next
start, never mid-run. Mods apply to the extracted tree: a `.iso` handed to the game directly
is not overlaid.

- **Smaller files are fine.** A replacement shorter than the disc's file leaves the rest of
  the extent reading zero, as the padding between files does, so assets that shrank need no
  padding of their own.
- **Larger files are refused**, with a line in Activity. The game's own file table names the
  old extent and the following files sit where they sat, so growing a file is a change to the
  disc layout rather than an overlay — it would need the tree extracted again with a new
  layout, which nothing here does.
- **The last one wins**, and says so. Mods are laid over the disc in the order they were
  switched on, so enabling two that replace the same file leaves the newer one's copy in
  place; Activity names both mods when that happens.
- A file that is not on this disc is skipped, with a line in Activity: a mod folder for
  another game, or one written for a different version of it.
- **`sys/main.dol` is a file like any other, with one caveat.** The runtime reads the DOL's
  sections into memory at boot and the game reads its initialised data from there, so a mod
  that replaces the DOL changes that data — the bytes the game starts with. It does not
  change the instructions: those were recompiled from the disc's own DOL when the game was
  built, so a patched instruction in a replaced DOL does nothing. A code that patches
  instructions belongs in `mods.code` (below), which is the same write applied where it can
  take effect.

## Geckos: codes chosen on their own

A mod carries whatever codes it carries, and switching the mod on switches all of them on.
The **Geckos** panel is for the other case: one code at a time, from a list the player
builds up, so a single tweak does not need a mod folder of its own.

- **Where they are kept.** `geckos/<DISCID>.txt` beside the games, in the format an editor
  exports: `$Name` on its own line, its words under it, a line of `$`s between codes. It is
  keyed by disc ID for the same reason a mod's `disc_id` is: a code's addresses are one
  build's own, so GNT4's codes are never offered for another disc, and the European and
  American releases of a game keep separate lists. The folder is game data the launcher
  writes, so it is gitignored like `mods/`.
- **Where the list comes from.** The community's write-up for that disc is downloaded on its
  own the first time the launcher runs for it — there is no button to press for it — and
  **Refresh list** asks for it again, for a fetch that failed or a write-up that has changed.
  Whatever shape it arrives in, it is written to `geckos/online/<DISCID>.txt` in the same
  `$Name` format as a player's own codes, so one reader serves every source. Two shapes are
  read:

  | Source | Discs | Shape |
  | --- | --- | --- |
  | [Naruto-GNT-Modding](https://github.com/NicholasMoser/Naruto-GNT-Modding) guides | `G4NJDA`, `GNUEDA` | markdown: a `## Name` or `### Name` heading with a fenced block under it |
  | Ralf's [gc-forever](https://www.gc-forever.com/forums/) threads | `GNREDA`, `GNUPDA` | the thread's code box: a `Name [Author]` line with its words under it |

  `G3NJDA` (GNT3) has a write-up in neither, so it says there are none rather than offering
  codes written for another build. Codes are never carried across discs: a disc's addresses
  are its own.
- **What a code has to be.** Each pasted `$Named` block is checked on **Apply**. A block
  this recompiler cannot run — inserted assembly, a switch, a code that keeps its own
  registers — is left out and named in the Activity log, because a check that changes
  nothing in the game is worse than no check at all. The panel still lists such a code when
  it comes from the guide, so a player can see it exists and read why it does nothing here;
  it cannot be switched on.
- **What they become.** Nothing new: the chosen codes join the mods' codes in the same
  `mods.gecko` and `mods.code` (below), after the mods' own, so a code the player picked
  last wins where it and a mod write the same address. Checking a code that patches
  instructions rebuilds the game exactly as a mod's would.

## Codes: what the launcher and the runtime do with them

A Gecko code is a list of 8-byte pairs, `XXXXXXXX YYYYYYYY`. The first byte of the first
word is the codetype — `04` writes a word, `02` a halfword, `20` compares a word, `C2`
inserts PowerPC instructions — and the rest of it is an address or the codetype's operand.

When a mod with codes is on, the launcher resolves them (`launcher/src/gecko.cpp`) into two
files beside `disc.manifest`, and removes both when the last one is switched off:

- **`mods.gecko`** — one operation per line: a write, a compare, a base-address move. The
  runtime's engine (`gcn-recomp/runtime/gecko.cpp`) runs the program on the first Video
  Interface field of each frame, the way a console's code handler runs every code once a
  frame, so a value written each frame stays written. Nothing here is code that was
  recompiled, so this is what a code that changes a table, a flag or a counter becomes.
- **`mods.code`** — the instruction words an unconditional 32-bit write carries, which
  recompilation lays over the DOL before translating it (`gcn-recomp/recomp/modspatch.py`).
  This is the only way `0400CB14 60000000` can work: after recompilation the `bl` at that
  address *is* a host function call, and writing over the guest's copy of it changes nothing
  that runs.

### The rebuild a code patch costs

A code that patches instructions means the game is built again, because those words go into
the generated C. The launcher notices — it keeps what it last compiled in, in
`build/mods-code.stamp` — and says **Ready to rebuild**: the build starts on its own when the
game is launched. Only the generated files holding the patched functions are recompiled and
the game relinked, so it is about 20 seconds on a desktop, not the several minutes of a first
build. Codes that only write memory cost nothing: they are applied each frame and switching
them needs only a restart.

A write a condition guards is not handed to recompilation — whether it should apply is only
known while the game runs — so `04` writes inside an `if`/`endif` block are applied to
memory and not to the code. Activity says so when a mod relies on them.

### Codetypes

Supported, with the same meaning a console's code handler gives them:

| Codetype | Meaning |
| --- | --- |
| `00`, `02`, `04` | 8-bit, 16-bit and 32-bit writes, with the repeat count `00`/`02` carry |
| `10`, `12`, `14`, `16` | the same writes, added to the pointer address |
| `06` | string write (length in the second word, data in the words after it) |
| `20`, `22`, `24`, `26` | if equal / not equal / greater / less, 32-bit, with `else` |
| `28`, `2A`, `2C`, `2E` | the same on a halfword, through the mask in the second word |
| `30`–`3E` | the same comparisons, added to the pointer address |
| `40`, `42`, `44`, `48`, `4A`, `4C` | base and pointer address load, set and store, with the set/add and offset flags |
| `60`, `62` | repeat a block of codes `n` times |
| `E0`, `E2`, `F0` | the terminator, the endif (with `else` and a count) and the exit |

Refused, with a line in Activity naming the codetype and its address, and the rest of the
mod's codes are still read:

| Codetype | Why |
| --- | --- |
| `C0`, `C2`, `C4`, `F2` | inserted PowerPC instructions: there is no PowerPC core at run time to execute them |
| `C6`, `CC`, `CE` | write branch, switch, address range check: these change control flow in the code list |
| `64`, `66`, `68` | return, goto, gosub: a jump inside the code list |
| `80`–`88`, `A0`–`AE`, `8A`, `8C`, `08`, `F6` | gecko registers, counter comparisons, memory copy, serial write, brainslug: runtime state this engine does not keep |
| `46`, `4E` | "base address of the next code": needs the code list's own addresses |

Two smaller limits: the terminator's 16-bit base and pointer addresses are not applied (it
resets them to zero), and a base or pointer operation whose low nibble names a gecko register
is refused rather than guessed at.

## What the community's mods look like here

| Thing | Status |
| --- | --- |
| GNT4 Gecko codes ([Naruto-GNT-Modding](https://github.com/NicholasMoser/Naruto-GNT-Modding/blob/main/gnt4/docs/guides/gecko_codes.md)) | The memory codes run as they are: *Unlock Everything* (`02` writes), *Infinite Time*, *Default Rounds/Time*, the 16:9 constants. Codes with `04` writes — *Skip Three Intro Videos*, *Skip Memory Card Warning*, *Enable Fight Debug Menu* — recompile the patched functions. `C2` codes such as *Enable DPad During Fights* are refused. |
| European Clash of Ninja Gecko codes ([GC-FOREVER](https://www.gc-forever.com/forums/viewtopic.php?t=2175)) | The same rules, and a code is only offered for the disc it was written for. European codes have **not** been run against `GNUPDA` here: no disc of it was at hand, and a code written for GNT4's addresses must never be. |
| Clash of Ninja 2 (`GNUEDA`) | The format works; the codes were not exercised. A con2 mod is a folder with `disc_id = GNUEDA`. |
| GNT3 English translation ([gamebanana](https://gamebanana.com/mods/564395)) | A file mod, so it needs no codes: if its files fit the extents they replace it can be a `disc/` tree. Not tried here; a translation that grows a file cannot be laid over the disc. |
| GNT4 English translation ([romhacking](https://www.romhacking.net/translations/5852/)) | The same shape as any file mod. The listing was not verified. |
| SCON4 ([gnt4.online](https://gnt4.online)) | A **disc patch**, not a mod: it ships as an xdelta of the whole disc. It is a row in `mods.json`, so **Mods** lists it before it is downloaded: checking it and pressing **Apply** fetches the newest release and reads it over the extracted tree, with no second disc and no second copy. The patch is offered only when its windows decode from this disc — SCON4 2.1.0 agrees with all 175 windows of a retail GNT4 dump but two, and those two differ only in space no file owns, so the file table and every file read come out right. It carries its own executable, which is *not* recompiled: the game runs this build's code over SCON4's disc. SCON4 renames the disc it carries to `SG4JDA`, and the runtime's check that the game is the one this build was recompiled from is made against the tree the patch decodes from, so a patch that renames its disc still boots. A tree extracted from a repacked rip (NKit and the like) keeps different offsets and no retail-disc patch can fit it; the launcher says so rather than booting a disc of the wrong files. |
| Dolphin resource packs and texture packs ([ResourcePacks.md](https://github.com/dolphin-emu/dolphin/blob/master/docs/ResourcePacks.md)) | Not supported. Dolphin matches a texture by a hash computed from the game's texture data; the renderer here decodes textures straight to the GPU and keeps no such hash or name, so there is nothing to match a pack's files against. A texture that is a disc file can still be replaced whole. |

## Reporting

Everything a mod does or cannot do is a line in Activity, once per change rather than once
per refresh:

```
[launcher] mods: 2 file(s) served in place of the disc's
[launcher] mod: intro-skip/codes.txt: insert assembly (0xC2042C40) needs a PowerPC core …
[launcher] mods: 1 code(s), 6 operations a frame, and 3 instruction word(s) built into the game
```

The runtime logs its own under `--log=dvd`: how many operations it read, and a write that
landed outside main RAM.

## What is not here yet

- **Inserted assembly**, which is most of what a Dolphin-era code does when a write will not
  do: a native hook or a small interpreter is the way to it, not a memory write.
- **Growing or adding files**, which needs a disc layout change and so a re-extraction.
- **Texture packs keyed by Dolphin's hashes**, which needs the renderer to keep a texture's
  identity.
- **Hooks into the game itself**: events, character stats, move tables, rendering. Those are
  the stable interface the mod format above is deliberately not standing in the way of.

`mods/` and everything in it is game data: it is gitignored, not shipped, and refused by
`tools/check_publish.py`. Nothing here ships a game file or another author's mod assets.
