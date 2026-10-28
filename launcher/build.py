#!/usr/bin/env python3
"""Package the launcher as a standalone app with PyInstaller.

Each operating system builds its own: run this on Windows for an .exe, on macOS for an
.app, on Linux for an executable folder. PyInstaller cannot cross-compile.

    python launcher/build.py            # one-folder build in launcher/dist
    python launcher/build.py --onefile  # single executable, slower to start

Ship the one-folder build. PySide6 is LGPL-3.0, which lets a recipient replace the Qt
libraries, and a folder of dylibs lets them; --onefile does not. See
THIRD-PARTY-NOTICES.md for what a bundle has to carry either way.
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parent
ENTRY = HERE / "main.py"
NAME = "Naruto Launcher"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--onefile", action="store_true",
                        help="put everything in one executable instead of a folder; note "
                             "that PySide6's LGPL-3.0 wants Qt replaceable, so the folder "
                             "build is the one to ship (see THIRD-PARTY-NOTICES.md)")
    parser.add_argument("--clean", action="store_true", help="build from scratch")
    args = parser.parse_args()

    try:
        import PyInstaller  # noqa: F401
    except ImportError:
        print("PyInstaller is not installed. Install it where this Python lives:\n"
              f"  {sys.executable} -m pip install pyinstaller", file=sys.stderr)
        return 2

    dist = HERE / "dist"
    work = HERE / "build" / "pyinstaller"
    command = [
        sys.executable, "-m", "PyInstaller",
        "--noconfirm",
        "--windowed",
        "--name", NAME,
        "--paths", str(HERE),
        "--distpath", str(dist),
        "--workpath", str(work),
        "--specpath", str(work),
        "--collect-submodules", "naruto_launcher",
        "--osx-bundle-identifier", "local.naruto.launcher",
        str(ENTRY),
    ]
    if args.onefile:
        command.insert(3, "--onefile")
    if args.clean:
        command.insert(3, "--clean")

    print("running:", " ".join(command))
    result = subprocess.run(command, cwd=PROJECT)
    if result.returncode != 0:
        print(f"PyInstaller failed with exit status {result.returncode}", file=sys.stderr)
        return result.returncode

    if sys.platform == "darwin":
        produced = dist / f"{NAME}.app"
    elif os.name == "nt":
        produced = dist / NAME / f"{NAME}.exe"
    else:
        produced = dist / NAME / NAME
    print(f"\nBuilt: {produced}")
    print("\nThe launcher needs the project tree beside it. Either:")
    print(f"  * copy the built app into {PROJECT}, or")
    print("  * run it anywhere and use File > Locate Project Folder on first start.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
