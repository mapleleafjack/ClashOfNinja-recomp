"""Where the project is, what it has built, and which tools it needs.

Everything here is plain Python so it can be tested without Qt.
"""
from __future__ import annotations

import hashlib
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

GAME_TITLE = "Naruto: Clash of Ninja"
GAME_SUBTITLE = "GameCube static recompilation"

# The build this project supports, also pinned in naruto/CMakeLists.txt.
FALLBACK_DOL_SHA1 = "19251f7f15f8b6f7e0cd1c753409151ec49d9af2"

_PROJECT_MARKERS = ("naruto/CMakeLists.txt", "tools/extract_disc.py")
_path_cache: str | None = None


def looks_like_project(path: Path) -> bool:
    return all((path / marker).is_file() for marker in _PROJECT_MARKERS)


def _candidate_roots() -> list[Path]:
    """Directories to check, in order, for the project tree."""
    starts: list[Path] = []
    if getattr(sys, "frozen", False):
        starts.append(Path(sys.executable).resolve().parent)
    # <project>/launcher/naruto_launcher/project.py -> <project>
    starts.append(Path(__file__).resolve().parents[2])
    starts.append(Path.cwd())
    seen: set[Path] = set()
    out: list[Path] = []
    for start in starts:
        for candidate in (start, *start.parents):
            if candidate not in seen:
                seen.add(candidate)
                out.append(candidate)
    return out


def locate_project(saved: str | None = None) -> Path | None:
    """The project root: the remembered one, else the app's own tree, else the cwd's."""
    if saved:
        saved_path = Path(saved).expanduser()
        if looks_like_project(saved_path):
            return saved_path
    for candidate in _candidate_roots():
        if looks_like_project(candidate):
            return candidate
    return None


def _login_shell_path() -> str | None:
    """macOS GUI apps get a minimal PATH; ask the user's shell what it would be."""
    if os.name == "nt":
        return None
    shell = os.environ.get("SHELL") or ("/bin/zsh" if sys.platform == "darwin" else "/bin/bash")
    try:
        result = subprocess.run(
            [shell, "-lc", 'printf %s "$PATH"'],
            capture_output=True, text=True, timeout=3,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    return result.stdout.strip() or None


def search_path() -> str:
    """PATH to hand to child processes: the app's, plus the usual package manager dirs."""
    global _path_cache
    if _path_cache is not None:
        return _path_cache
    parts = [os.environ.get("PATH", "")]
    login = _login_shell_path()
    if login:
        parts.append(login)
    if sys.platform == "darwin":
        parts += ["/opt/homebrew/bin", "/usr/local/bin", "/opt/local/bin"]
    elif os.name != "nt":
        parts += ["/usr/local/bin", "/usr/bin", "/bin"]
    _path_cache = os.pathsep.join(p for p in parts if p)
    return _path_cache


def find_tool(name: str) -> str | None:
    return shutil.which(name, path=search_path())


@dataclass(frozen=True)
class ToolInfo:
    name: str
    path: str | None
    needed: bool
    install_hint: str

    @property
    def available(self) -> bool:
        return self.path is not None


def _install_hint(name: str) -> str:
    if sys.platform == "darwin":
        return f"brew install {name}"
    if os.name == "nt":
        hints = {"cmake": "winget install Kitware.CMake", "ninja": "winget install Ninja-build.Ninja",
                 "nodtool": "cargo install --locked nodtool"}
        return hints.get(name, f"install {name}")
    hints = {"cmake": "sudo apt install cmake", "ninja": "sudo apt install ninja-build",
             "nodtool": "cargo install --locked nodtool"}
    return hints.get(name, f"install {name}")


def check_tools() -> list[ToolInfo]:
    """cmake and ninja are needed to build; nodtool only for non-ISO/CISO images."""
    return [
        ToolInfo("cmake", find_tool("cmake"), True, _install_hint("cmake")),
        ToolInfo("ninja", find_tool("ninja"), True, _install_hint("ninja")),
        ToolInfo("nodtool", find_tool("nodtool"), False, _install_hint("nodtool")),
    ]


def sha1_file(path: Path) -> str:
    digest = hashlib.sha1()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


@dataclass(frozen=True)
class DiscStatus:
    extracted: bool
    dol_verified: bool
    dol_sha1: str | None
    expected_sha1: str


@dataclass(frozen=True)
class ProjectStatus:
    disc: DiscStatus
    built: bool


class Project:
    """Paths and state of one checked-out copy of the recompilation."""

    def __init__(self, root: Path):
        self.root = Path(root)

    # -- layout ------------------------------------------------------------
    @property
    def naruto(self) -> Path:
        return self.root / "naruto"

    @property
    def disc(self) -> Path:
        return self.naruto / "rom" / "disc"

    @property
    def dol(self) -> Path:
        return self.disc / "sys" / "main.dol"

    @property
    def manifest(self) -> Path:
        return self.disc / "disc.manifest"

    @property
    def build_dir(self) -> Path:
        return self.naruto / "build"

    @property
    def binary(self) -> Path:
        return self.build_dir / ("naruto.exe" if os.name == "nt" else "naruto")

    @property
    def bench_binary(self) -> Path:
        return self.build_dir / ("naruto_bench.exe" if os.name == "nt" else "naruto_bench")

    @property
    def controls(self) -> Path:
        return self.naruto / "controls.txt"

    @property
    def saves(self) -> Path:
        return self.naruto / "saves"

    @property
    def extract_script(self) -> Path:
        return self.root / "tools" / "extract_disc.py"

    @property
    def setup_script(self) -> Path:
        return self.root / "tools" / "setup.sh"

    @property
    def cmake_lists(self) -> Path:
        return self.naruto / "CMakeLists.txt"

    # -- state -------------------------------------------------------------
    def expected_sha1(self) -> str:
        try:
            text = self.cmake_lists.read_text(encoding="utf-8", errors="replace")
        except OSError:
            return FALLBACK_DOL_SHA1
        match = re.search(r"DOL_SHA1\s+([0-9a-fA-F]{40})", text)
        return match.group(1).lower() if match else FALLBACK_DOL_SHA1

    def disc_status(self) -> DiscStatus:
        expected = self.expected_sha1()
        if not self.dol.is_file():
            return DiscStatus(False, False, None, expected)
        try:
            actual = sha1_file(self.dol)
        except OSError:
            return DiscStatus(True, False, None, expected)
        return DiscStatus(True, actual == expected, actual, expected)

    def status(self) -> ProjectStatus:
        return ProjectStatus(self.disc_status(), self.binary.is_file())

    def controls_exists(self) -> bool:
        return self.controls.is_file()
