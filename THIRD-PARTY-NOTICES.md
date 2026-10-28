# Third-party notices

This repository ships no game data: no `main.dol`, no disc image, no assets. That is a
copyright matter and `tools/check_publish.py` enforces it. This file is the other half —
the licences of the code that *is* here, and what to carry when you distribute a build.

Keep this file with the repository. If you ship a binary built from it, ship this file and
the `licenses/` directory with the binary.

## gcn-recomp — MIT (Ethan Nicholas)

`naruto/gcn-recomp/` is [gcn-recomp](https://github.com/ethannicholas/gcn-recomp)'s
PowerPC-to-C recompiler and GameCube runtime. The whole thing is MIT licensed, and the
verbatim text lives at [naruto/gcn-recomp/LICENSE](naruto/gcn-recomp/LICENSE). It is
reproduced here because a built `naruto` binary contains a substantial portion of it and
MIT asks for the notice in the distribution, not just the source tree:

```
MIT License

Copyright (c) 2026 Ethan Nicholas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## glad — (WTFPL OR CC0-1.0) AND Apache-2.0

`naruto/gcn-recomp/runtime/gx/glad/` and `.../glad_es/` are GL and GLES loaders generated
by glad 2.0.8 from the Khronos specifications. Each generated file carries its own SPDX
line; the Khronos `khrplatform.h` is MIT, with the text in the file. The Apache-2.0 half
wants a copy of its licence for anyone who receives the sources, which is
[licenses/Apache-2.0.txt](licenses/Apache-2.0.txt).

## SDL2 — zlib licence — the launcher's window

`launcher/` links SDL2 for its window, event loop, clipboard and drag and drop, the same
library the game's runtime uses. It is statically linked, so the executable contains it and
its notice must travel with any build: [licenses/SDL2.txt](licenses/SDL2.txt).

## stb_truetype and stb_image — public domain (or MIT) — launcher only

`launcher/src/third_party/` vendors two single-header libraries by Sean Barrett: the glyph
rasteriser and the PNG reader for the launcher's own logo. Each file states its own terms
("public domain ... or MIT") at the top, and that statement is the licence that has to
travel with a build. Nothing else in the project uses them.

## ANGLE — BSD-2-Clause and others — visionOS target only

`naruto/gcn-recomp/visionos/angle.h` loads ANGLE at runtime; ANGLE is not vendored here,
it is fetched and built by `tools/build-angle-visionos.sh`. Only a visionOS app you build
and distribute carries it, and that app needs ANGLE's notices (BSD-2-Clause for ANGLE
itself, plus whatever WebKit's build pulls in). Nothing in this repository does.

## nodtool — MIT OR Apache-2.0 — bundled with Windows releases

`launcher/packaging/build-release.ps1` puts the Windows `nodtool` binary from
[encounter/nod](https://github.com/encounter/nod) in `tools/nodtool/`, so the launcher can
convert RVZ, WIA, GCZ, WBFS, TGC and NFS images before extracting them. It is MIT OR
Apache-2.0; the release carries `tools/nodtool/LICENSE.txt`, and
[licenses/Apache-2.0.txt](licenses/Apache-2.0.txt) is the same licence text.

## Native launcher toolchain — bundled with Windows releases

A full release folder also carries portable CMake (BSD-3-Clause), Ninja (Apache-2.0),
MinGW-w64 (GCC Runtime Library Exception) and the Python embeddable runtime (PSF licence)
in `tools\`. It is pruned to what a build of the game reaches — MinGW's Fortran and
Objective-C frontends, gdb, doxygen, ctest, cpack and the documentation tree are left out
— so a recipient who needs those should install the full distribution from WinLibs
instead. See each project for its licence text; `licenses/Apache-2.0.txt` covers the
Apache-2.0 parts. The launcher executable itself needs no framework beyond SDL2: it draws
its own interface and rasterises text from a font the system already has.

## Launcher icon — fan artwork by sadrithmora

The launcher's icon and the leaf symbol in its header come from
`naruto_themed_windows_icons_by_sadrithmora` by
[sadrithmora](https://www.deviantart.com/sadrithmora) (leafsymbolD.ico), supplied for
this project by its author. It is fan artwork: the Naruto name and marks belong to their
respective owners. If you redistribute the launcher, keep this attribution with it.

## Not third-party, but worth restating

`naruto/analysis/symbols.txt` is a symbol map — addresses, sizes and names recovered from
the user's own disc, which is the same class of artefact every decompilation project
publishes. `naruto/recomp/*.txt` are steering tables of addresses. Neither contains any of
the game's code or content, and neither is a licensed third-party work.
