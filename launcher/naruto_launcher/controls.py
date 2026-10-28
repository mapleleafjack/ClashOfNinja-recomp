"""controls.txt: read it, edit it, and understand the --list-controls probe.

The file belongs to the runtime (naruto/gcn-recomp/runtime/input_map.h); this module only
edits the lines it understands and leaves everything else -- force-feedback settings,
stick ranges, comments -- untouched.

The probe prints one line per event to stdout, for example::

    device "Xbox Wireless Controller": 6 axes, 11 buttons, 1 hats, force feedback
    "Xbox Wireless Controller" button 0
    "Xbox Wireless Controller" axis 2-
    key Return
"""
from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path

# (name in controls.txt, label for the launcher). Names are the runtime's own.
PAD_CONTROLS: tuple[tuple[str, str], ...] = (
    ("A", "A"), ("B", "B"), ("X", "X"), ("Y", "Y"), ("Z", "Z"),
    ("start", "Start"),
    ("dpad_up", "D-pad up"), ("dpad_down", "D-pad down"),
    ("dpad_left", "D-pad left"), ("dpad_right", "D-pad right"),
    ("L", "L trigger"), ("R", "R trigger"),
    ("stick_x", "Stick X"), ("stick_y", "Stick Y"),
    ("cstick_x", "C-stick X"), ("cstick_y", "C-stick Y"),
    ("stick_left", "Stick left"), ("stick_right", "Stick right"),
    ("stick_up", "Stick up"), ("stick_down", "Stick down"),
    ("cstick_left", "C-stick left"), ("cstick_right", "C-stick right"),
    ("cstick_up", "C-stick up"), ("cstick_down", "C-stick down"),
)

CONTROL_NAMES = tuple(name for name, _ in PAD_CONTROLS)
LABELS = dict(PAD_CONTROLS)
_BY_FOLD = {name.casefold(): name for name in CONTROL_NAMES}


def canonical_control(name: str) -> str | None:
    """The runtime's spelling of a pad control, or None if it is not one."""
    return _BY_FOLD.get(name.strip().casefold())


@dataclass(frozen=True)
class Binding:
    control: str
    source: str


@dataclass(frozen=True)
class PreservedLine:
    """A line in controls.txt that is not a pad binding (ffb settings, stick ranges)."""
    key: str
    value: str


def _strip_comment(line: str) -> str:
    """Drop a # comment, leaving # inside quoted device names alone."""
    out: list[str] = []
    quoted = False
    for ch in line:
        if ch == '"':
            quoted = not quoted
        elif ch == "#" and not quoted:
            break
        out.append(ch)
    return "".join(out)


def _split_line(line: str) -> tuple[str, str] | None:
    """(left, right) around the first = outside quotes, or None."""
    quoted = False
    for i, ch in enumerate(line):
        if ch == '"':
            quoted = not quoted
        elif ch == "=" and not quoted:
            left, right = line[:i].strip(), line[i + 1:].strip()
            if left and right:
                return left, right
            return None
    return None


def read_lines(path: Path) -> list[str]:
    try:
        return path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        return []


def read_bindings(path: Path) -> dict[str, list[str]]:
    """Every bound pad control and the sources bound to it, in file order."""
    out: dict[str, list[str]] = {}
    for line in read_lines(path):
        parsed = _split_line(_strip_comment(line))
        if not parsed:
            continue
        control = canonical_control(parsed[0])
        if control is None:
            continue
        out.setdefault(control, []).append(parsed[1])
    return out


def read_preserved(path: Path) -> list[PreservedLine]:
    """Settings the launcher does not edit; shown as-is so they are not silently lost."""
    out: list[PreservedLine] = []
    for line in read_lines(path):
        parsed = _split_line(_strip_comment(line))
        if not parsed or canonical_control(parsed[0]) is not None:
            continue
        out.append(PreservedLine(parsed[0], parsed[1]))
    return out


def _write_lines(path: Path, lines: list[str]) -> None:
    text = "\n".join(lines).rstrip() + ("\n" if any(lines) else "")
    path.write_text(text, encoding="utf-8")


def set_binding(path: Path, control: str, source: str) -> None:
    """Bind `control` to `source`, replacing the bindings it already had."""
    canonical = canonical_control(control)
    if canonical is None:
        raise ValueError(f"not a pad control: {control}")
    source = source.strip()
    if not source:
        raise ValueError("empty binding")
    kept: list[str] = []
    for line in read_lines(path):
        parsed = _split_line(_strip_comment(line))
        if parsed and canonical_control(parsed[0]) == canonical:
            continue
        kept.append(line)
    while kept and not kept[-1].strip():
        kept.pop()
    kept.append(f"{canonical} = {source}")
    _write_lines(path, kept)


def remove_binding(path: Path, control: str) -> None:
    canonical = canonical_control(control)
    if canonical is None:
        return
    kept: list[str] = []
    for line in read_lines(path):
        parsed = _split_line(_strip_comment(line))
        if parsed and canonical_control(parsed[0]) == canonical:
            continue
        kept.append(line)
    while kept and not kept[-1].strip():
        kept.pop()
    if kept:
        _write_lines(path, kept)
    elif path.exists():
        path.unlink()


def clear_bindings(path: Path) -> None:
    """Remove the file, which puts the runtime back on its built-in mapping."""
    if path.exists():
        path.unlink()


# -- the probe -------------------------------------------------------------

@dataclass(frozen=True)
class ProbeEvent:
    kind: str      # "device" or "binding"
    text: str      # device name, or the source to paste into controls.txt
    detail: str = ""


_DEVICE_RE = re.compile(r'^device "(.*)": (\d+) axes, (\d+) buttons, (\d+) hats(, force feedback)?$')
_KEY_RE = re.compile(r"^key (.+)$")
_BUTTON_RE = re.compile(r'^"(.*)" button (\d+)$')
_HAT_RE = re.compile(r'^"(.*)" hat (\d+) (up|down|left|right)$')
_AXIS_RE = re.compile(r'^"(.*)" axis (\d+)([+-]?)')


def parse_probe_line(line: str) -> ProbeEvent | None:
    """One line of --list-controls output, or None if it is prose or blank."""
    text = line.rstrip("\r\n").strip()
    if not text:
        return None

    match = _DEVICE_RE.match(text)
    if match:
        name, axes, buttons, hats, ffb = match.groups()
        detail = f"{axes} axes, {buttons} buttons, {hats} hats"
        if ffb:
            detail += ", force feedback"
        return ProbeEvent("device", name, detail)

    match = _BUTTON_RE.match(text)
    if match:
        return ProbeEvent("binding", f'"{match.group(1)}" button {match.group(2)}', "button")

    match = _HAT_RE.match(text)
    if match:
        return ProbeEvent("binding", f'"{match.group(1)}" hat {match.group(2)} {match.group(3)}',
                          "hat")

    match = _AXIS_RE.match(text)
    if match:
        name, axis, half = match.groups()
        # The probe writes "axis 3 invert" for an axis that rests at one end.
        suffix = " invert" if "invert" in text[match.end():].split("(")[0] else ""
        source = f'"{name}" axis {axis}{half}{suffix}'
        if "rests low" in text:
            detail = "axis, rests low"
        elif "rests high" in text:
            detail = "axis, rests high, inverted"
        elif half:
            detail = "axis, one direction"
        else:
            detail = "axis"
        return ProbeEvent("binding", source, detail)

    match = _KEY_RE.match(text)
    if match:
        return ProbeEvent("binding", f"key {match.group(1).strip()}", "keyboard")

    return None
