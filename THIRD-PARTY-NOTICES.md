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

## Qt for Python (PySide6) — LGPL-3.0 — launcher only

`launcher/` is a Qt application and imports PySide6, which is the Qt Company's Qt under
the LGPL-3.0. Nothing is owed for publishing the launcher's source, or for a user running
it against their own PySide6 install from `launcher/requirements.txt`.

A bundle built by `launcher/build.py` is a different thing: it contains Qt, so whoever
receives it must get the LGPL-3.0 (and GPL-3.0, which LGPLv3 incorporates by reference)
and must be able to replace the Qt libraries with their own build — which is why the
default one-folder build is the compliant one, and `--onefile`, which unpacks to a
throwaway directory, is not. Concretely, ship with the bundle:

- this file, `licenses/LGPL-3.0.txt` and `licenses/GPL-3.0.txt`;
- the Qt libraries as separate files, unmodified (`build.py`'s default does this);
- a note saying where to get a matching PySide6 wheel, so the Qt in the bundle can be
  swapped.

Qt itself embeds further third-party code (ICU, HarfBuzz, OpenSSL and so on) whose notices
are in the wheel's `PySide6/Qt/` tree; `qtattributionsscanner` collects them.

## PyInstaller — GPL-2.0-or-later, with the bundling exception — build tool only

`launcher/build.py` drives PyInstaller to produce the launcher bundle. PyInstaller is
GPL-2.0-or-later, and the exception in its `COPYING` — reproduced verbatim at
[licenses/PyInstaller-COPYING.txt](licenses/PyInstaller-COPYING.txt) — is what allows it
to bundle a program that is not GPL. The bootloader it puts into the bundle is PyInstaller
code, so its notice travels with the bundle too; [licenses/GPL-2.0.txt](licenses/GPL-2.0.txt)
is the licence text.

## ANGLE — BSD-2-Clause and others — visionOS target only

`naruto/gcn-recomp/visionos/angle.h` loads ANGLE at runtime; ANGLE is not vendored here,
it is fetched and built by `tools/build-angle-visionos.sh`. Only a visionOS app you build
and distribute carries it, and that app needs ANGLE's notices (BSD-2-Clause for ANGLE
itself, plus whatever WebKit's build pulls in). Nothing in this repository does.

## Not third-party, but worth restating

`naruto/analysis/symbols.txt` is a symbol map — addresses, sizes and names recovered from
the user's own disc, which is the same class of artefact every decompilation project
publishes. `naruto/recomp/*.txt` are steering tables of addresses. Neither contains any of
the game's code or content, and neither is a licensed third-party work.
