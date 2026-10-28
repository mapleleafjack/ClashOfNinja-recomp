"""Render the launcher offscreen and save a PNG, as a quick visual check.

    QT_QPA_PLATFORM=offscreen python launcher/tests/ui_smoke.py out.png [project]
"""
from __future__ import annotations

import os
import sys
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
LAUNCHER = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(LAUNCHER))

from PySide6.QtWidgets import QApplication  # noqa: E402

from naruto_launcher.app import MainWindow  # noqa: E402
from naruto_launcher.project import Project, locate_project  # noqa: E402
from naruto_launcher.theme import apply_theme  # noqa: E402


def main() -> int:
    out = Path(sys.argv[1] if len(sys.argv) > 1 else LAUNCHER / "build" / "screenshot.png")
    if len(sys.argv) > 2:
        root = Path(sys.argv[2]).resolve()
    else:
        root = locate_project()
    app = QApplication(sys.argv[:1])
    apply_theme(app)
    window = MainWindow(Project(root) if root else None)
    window.resize(1000, 920)
    window.show()
    app.processEvents()
    out.parent.mkdir(parents=True, exist_ok=True)
    window.grab().save(str(out))
    print(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
