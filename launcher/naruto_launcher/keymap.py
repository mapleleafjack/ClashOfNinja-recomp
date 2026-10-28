"""Turn a Qt key press into the `key <name>` line controls.txt expects.

The runtime parses SDL scancode names (`SDL_GetScancodeFromName`), so the names here are
the ones `SDL_GetScancodeName` produces. Only the keys a gamepad-mapped keyboard needs
are covered; an unmapped key returns None and the launcher says so.
"""
from __future__ import annotations

import sys

from PySide6.QtCore import Qt
from PySide6.QtGui import QKeyEvent

# Qt names its key constants with the SDL spelling for almost everything printable.
_NAMED: dict[int, str] = {
    Qt.Key.Key_Return: "Return",
    Qt.Key.Key_Enter: "Keypad Enter",
    Qt.Key.Key_Escape: "Escape",
    Qt.Key.Key_Tab: "Tab",
    Qt.Key.Key_Backspace: "Backspace",
    Qt.Key.Key_Insert: "Insert",
    Qt.Key.Key_Delete: "Delete",
    Qt.Key.Key_Home: "Home",
    Qt.Key.Key_End: "End",
    Qt.Key.Key_PageUp: "PageUp",
    Qt.Key.Key_PageDown: "PageDown",
    Qt.Key.Key_Left: "Left",
    Qt.Key.Key_Right: "Right",
    Qt.Key.Key_Up: "Up",
    Qt.Key.Key_Down: "Down",
    Qt.Key.Key_CapsLock: "CapsLock",
    Qt.Key.Key_NumLock: "NumLock",
    Qt.Key.Key_ScrollLock: "ScrollLock",
    Qt.Key.Key_Print: "PrintScreen",
    Qt.Key.Key_Pause: "Pause",
    Qt.Key.Key_Menu: "Application",
    Qt.Key.Key_Space: "Space",
    Qt.Key.Key_Minus: "-",
    Qt.Key.Key_Equal: "=",
    Qt.Key.Key_BracketLeft: "[",
    Qt.Key.Key_BracketRight: "]",
    Qt.Key.Key_Backslash: "\\",
    Qt.Key.Key_Semicolon: ";",
    Qt.Key.Key_Apostrophe: "'",
    Qt.Key.Key_QuoteLeft: "`",
    Qt.Key.Key_Comma: ",",
    Qt.Key.Key_Period: ".",
    Qt.Key.Key_Slash: "/",
}

_KEYPAD = {
    Qt.Key.Key_Slash: "Keypad /",
    Qt.Key.Key_Asterisk: "Keypad *",
    Qt.Key.Key_Minus: "Keypad -",
    Qt.Key.Key_Plus: "Keypad +",
    Qt.Key.Key_Period: "Keypad .",
    Qt.Key.Key_Enter: "Keypad Enter",
    Qt.Key.Key_Equal: "Keypad =",
}

_MODIFIER_SIDES = {
    # Qt key: (left name, right name)
    Qt.Key.Key_Shift: ("Left Shift", "Right Shift"),
    Qt.Key.Key_Control: ("Left Ctrl", "Right Ctrl"),
    Qt.Key.Key_Alt: ("Left Alt", "Right Alt"),
    Qt.Key.Key_Meta: ("Left GUI", "Right GUI"),
}


def _is_right_side(event: QKeyEvent, key: int) -> bool:
    """Whether a modifier event is the right-hand key of the pair."""
    native = event.nativeVirtualKey()
    if sys.platform == "darwin":
        return native in (54, 60, 61, 62)          # RGui, RShift, RAlt, RCtrl keycodes
    if sys.platform == "win32":
        return native in (0xA1, 0xA3, 0xA5, 0x5C)  # RShift, RCtrl, RAlt, LWin/RWin pair
    # X11/keysyms, and Wayland's best guess.
    return native in (0xFFE2, 0xFFE4, 0xFFEA, 0xFFEC)


def _quote(name: str) -> str:
    return f'"{name}"' if " " in name else name


def source_for_key(event: QKeyEvent) -> str | None:
    """`key <name>` for a bindable key, or None."""
    key = event.key()
    keypad = bool(event.modifiers() & Qt.KeyboardModifier.KeypadModifier)

    if key in _MODIFIER_SIDES:
        left, right = _MODIFIER_SIDES[key]
        return f"key {_quote(right if _is_right_side(event, key) else left)}"

    if keypad:
        if Qt.Key.Key_0 <= key <= Qt.Key.Key_9:
            return f'key "Keypad {chr(int(key))}"'
        if key in _KEYPAD:
            return f"key {_quote(_KEYPAD[key])}"
        return None

    if Qt.Key.Key_A <= key <= Qt.Key.Key_Z:
        return f"key {chr(key)}"
    if Qt.Key.Key_0 <= key <= Qt.Key.Key_9:
        return f"key {chr(key)}"
    if Qt.Key.Key_F1 <= key <= Qt.Key.Key_F24:
        return f"key F{int(key) - int(Qt.Key.Key_F1) + 1}"
    if key in _NAMED:
        return f"key {_quote(_NAMED[key])}"
    return None
