# Launcher UI — analysis notes

Companion to [launcher-ui-map.pen](./launcher-ui-map.pen). The `.pen` file is the
map itself: where each surface is reached from, what the main page looks like with
details closed and open, the eight page states, and a render of every panel. This
file holds everything that is prose rather than flow or state — the source of
truth, the evidence, the geometry, the surface inventory with its status, and what
is still open.

## Scope

Every surface the launcher draws, how you reach it, the states the main page moves
through, and what is still half-built. The whole UI is hand-painted into one SDL
surface — no widget toolkit — so this map is the closest thing to a screen
inventory the project has.

## Source of truth

`launcher/src/app_paint.cpp` — `paint()`, `paint_games()`, `paint_overlay()`.
Layout constants in `app_shared.h`.

## Evidence

Nine frames rendered from the current build with `--render`, no window. This is the
working tree, WIP commit included.

## Geometry

Designed at 1060×820. Window zooms 1×–2× to keep text steady; minimum window
940×700.

## Status key

How the status column below reads. The map drops the coloured dots the older
version of this document carried, so status lives here only.

| Status | Meaning |
| --- | --- |
| shipped | Shipped and verified by a render test |
| in progress | In progress on this branch |
| missing | Missing or broken |
| untested | Present, no test |

## Surface inventory

| Surface | Reached from | Size | What it draws | Status |
| --- | --- | --- | --- | --- |
| Main window | app start · Escape from any panel | full window | Logo, game title + “GameCube static recompilation”, Settings, optional Update badge · five 3:4 covers, the selected one outlined in accent · status pill, hero line, body copy · Play, with Mods and Geckos beside it once a disc is extracted · Show details | shipped |
| Activity log | Show details link · Hide in the card | fills the rest | Card with Clear / Copy / Hide, a mono log on near-black, progress bar and Cancel while a job runs — the only place build or extract output is shown | shipped |
| Settings | Settings, top right of the header | 720 × 340 | Three sections only — CONTROLLERS (Players…), RESOLUTION (1×–4× combo with a pop-up menu), UPDATES (version line, Check now, Install update, start-up checkbox), Done | shipped |
| Players | Settings → Players… | 660 × 304 | One row per console pad, each a device dropdown (Not set / Keyboard / each controller) that opens upward near the bottom edge · Refresh · Done | shipped |
| Mods | Mods on the game page · needs an extracted disc | 720 wide | On / Mod / Version / Author table for the folders in mods/ and the catalog’s disc patches, · codes and · download suffixes, hover description under the table, Open the mods folder, Choose a patch file…, Apply | in progress |
| Geckos | Geckos on the game page · needs an extracted disc | 760 wide | On / Code / From table scrolling 3–12 rows, “can’t run here” rows kept visible, paste box (Return is a newline, ⌘V pastes), Open the codes folder, Fetch the guide’s, Apply | in progress |
| Disc picker | Play · drag-and-drop · Linux with no zenity/kdialog | 760 × 560 | Path field, Up, folder list, “Choose a disc image, or type its path.”, Choose Image, Cancel — Windows and macOS use the native dialog instead | shipped |
| Update prompt | start-up check · Update x.y.z badge | 620 wide · fits the text | “Version 1.1.0 is available”, the copy, and the path it would replace cut in the middle to one line, Cancel and Update now; the badge stays in the header after a Cancel | shipped |
| Confirm / tools | build without tools · disc swap · other prompts | 620 wide · fits the text | One reusable overlay: body text plus action (and Cancel when it is a question). Also the prompt offering to fetch the Windows toolchain | shipped |
| Pop-up menus | scale combo · a player row | anchored | Detached menu frames drawn above everything else: 4×32 scale list, and the device list that flips upward when the row is near the panel floor | untested |

There is no navigation rail and no second window: the main page is always
underneath, and every other surface is a centred panel drawn over it behind a 62%
black scrim. Escape walks back one level — Players returns to Settings, the others
return to the page.

## Main page geometry

Frames in the design are the current build at 1060 × 820.

### Details closed (the default page)

- Header: 48 px logo at 24,18 · title 82,14 · Settings pinned right at width − 128
- Five covers, 3:4, sized to the window down to 160 px tall, 32 px apart, the chosen
  one ringed in accent
- Status block centred: pill → hero line → 2-line body → Play, with Mods and Geckos
  beside it once a disc is extracted → Show details
- No log on this layout; the page ends at Show details and leaves the lower band empty

### Details open (the working page)

- Covers shrink to a left cluster, 20 px apart, capped at 216 px tall
- Right column is 32% of the width, clamped 300–380 px, and holds pill, title, body,
  then Play beside the other two
- The Activity card takes everything below, so the log is always visible
- 339 px cannot hold three worded buttons, so Play keeps “Set up the disc…” and Mods
  and Geckos become 50 px marks

## Panel notes

Every panel is the same box: rounded 16 px panel, 26 px inset, title top-left, Done
bottom-right, everything else stacked between. Players returns to Settings; the
panel height grows with the rows it holds (Mods 34 px a row, Geckos 22 px a row,
clamped to what the window can show), and a message panel is as tall as its text.

| Panel | Size | Note |
| --- | --- | --- |
| Settings | 720 × 340 | Three sections, no scrolling. Install update only appears when a release is found. |
| Players | 660 × 304 | Four pad rows; the device list flips up for the lower two. |
| Mods | 720 × grows | Apply sits disabled until a checkbox changes. Disc patches say so in the row. |
| Geckos | 760 × grows | Rows the recompiler cannot run stay listed, greyed, with the reason on hover. |
| Disc picker | 760 × 560 | Only Linux without zenity/kdialog sees this; the others get the native dialog. |
| Update prompt | 620 × fits the text | Shares the generic overlay with every confirm prompt, so the panel is as tall as its text and the path it names is cut in the middle to one line. |

## What is still open

What is broken, what is untested, and what is yours to decide.

### Broken in this build

1. **The documentation screenshots are older than the UI** — `docs/screenshots/launcher.png`
   and `players.png` show two buttons and larger covers; the build has three. The
   README and `docs/LAUNCHER.md` therefore illustrate a page that no longer exists.

### Unverified

1. **The eight states render, but CI still draws one** — `state:<name>[:details]`
   forces each of the eight, so any frame can be reproduced by hand; what is missing
   is the ctest entries, so a bad string in “needs disc” or “mods changed” still
   ships unnoticed.
2. **The pop-up menus render, but nothing runs them** — `scale-menu` and
   `pad-menu[:last]` click the control by id, so the scale list and the device list —
   the upward flip near the floor included — can be drawn on demand. The ctest
   entries are not written.
3. **The interface is mouse-first** — Tab focus exists only in the disc picker.
   Elsewhere Escape, Return and the shortcut for paste work, but buttons, covers and
   checkboxes are reachable by pointer only.
4. **Two explanations live under the cursor** — A mod’s description and a code’s
   “can’t run here” reason only appear while hovering the row — nothing on the page
   shows them for the keyboard or for a screenshot.

### Decisions for you

**A. Does this branch ship the Mods and Geckos panels together?** They are one WIP
commit; the panels are drawn and render-tested, but the rows they show are stand-ins
during a render, so what Apply writes is still only covered by the unit tests.

**B. Refresh the doc screenshots now or after the panels land?** Regenerating them
is one `--render` per view; leaving them stale costs a reader the two-button page.

## Reproduce

```
launcher/build/NarutoLauncher.app/Contents/MacOS/NarutoLauncher --render out.bmp \
  [main | settings | players | mods | geckos | picker | update \
   | state:<name>[:details] | scale-menu | pad-menu[:last]]

ctest --test-dir launcher/build
```

Read from `launcher/src/app_paint.cpp` at HEAD + the WIP mods/geckos work.
