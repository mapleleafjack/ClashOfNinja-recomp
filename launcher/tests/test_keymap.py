"""Qt key presses become the SDL scancode names controls.txt expects."""
from __future__ import annotations

import unittest

from PySide6.QtCore import QEvent, Qt
from PySide6.QtGui import QKeyEvent

from naruto_launcher.keymap import source_for_key


def press(key, modifiers=Qt.KeyboardModifier.NoModifier, text: str = "") -> str | None:
    return source_for_key(QKeyEvent(QEvent.Type.KeyPress, key, modifiers, text))


class KeymapTests(unittest.TestCase):
    def test_letter(self):
        self.assertEqual(press(Qt.Key.Key_A, text="a"), "key A")

    def test_digit(self):
        self.assertEqual(press(Qt.Key.Key_7, text="7"), "key 7")

    def test_function_key(self):
        self.assertEqual(press(Qt.Key.Key_F5), "key F5")

    def test_space(self):
        self.assertEqual(press(Qt.Key.Key_Space, text=" "), "key Space")

    def test_punctuation(self):
        self.assertEqual(press(Qt.Key.Key_Semicolon, text=";"), "key ;")

    def test_arrow(self):
        self.assertEqual(press(Qt.Key.Key_Left), "key Left")

    def test_keypad(self):
        self.assertEqual(press(Qt.Key.Key_1, Qt.KeyboardModifier.KeypadModifier, "1"),
                         'key "Keypad 1"')

    def test_modifier(self):
        self.assertEqual(press(Qt.Key.Key_Shift, Qt.KeyboardModifier.ShiftModifier),
                         'key "Left Shift"')

    def test_unknown_key(self):
        self.assertIsNone(press(Qt.Key.Key_unknown))


if __name__ == "__main__":
    unittest.main()
