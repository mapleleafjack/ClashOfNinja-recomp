#!/usr/bin/env python3
"""Naruto: Clash of Ninja launcher.

Starts the GUI, or with `--extract <project> <image>` runs tools/extract_disc.py in a
child process. The second form is why a packaged launcher needs no system Python: the
launcher re-runs itself, and this module reports the extraction's output and exit status.
"""
from __future__ import annotations

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)


def run_extract(project_root: str, image: str) -> None:
    tools = os.path.join(project_root, "tools")
    if not os.path.isfile(os.path.join(tools, "extract_disc.py")):
        sys.exit(f"error: no tools/extract_disc.py in {project_root!r}")
    if tools not in sys.path:
        sys.path.insert(0, tools)
    import extract_disc  # noqa: PLC0415 - from the project, at run time

    sys.argv = ["extract_disc.py", image]
    extract_disc.main()


def main(argv: list[str]) -> int:
    if len(argv) >= 2 and argv[1] == "--extract":
        if len(argv) < 4:
            print("usage: --extract <project folder> <disc image>", file=sys.stderr)
            return 2
        run_extract(argv[2], argv[3])
        return 0

    from naruto_launcher.app import main as gui_main  # noqa: PLC0415 - keep --extract Qt-free

    return gui_main()


if __name__ == "__main__":
    sys.exit(main(sys.argv))
