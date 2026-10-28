"""controls.txt parsing and editing, and the --list-controls probe format."""
from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from naruto_launcher import controls


class ProbeParseTests(unittest.TestCase):
    def test_device(self):
        event = controls.parse_probe_line(
            'device "Xbox Wireless Controller": 6 axes, 11 buttons, 1 hats, force feedback')
        self.assertIsNotNone(event)
        assert event
        self.assertEqual(event.kind, "device")
        self.assertEqual(event.text, "Xbox Wireless Controller")
        self.assertIn("force feedback", event.detail)

    def test_button(self):
        event = controls.parse_probe_line('"8BitDo Pro 2" button 3')
        assert event
        self.assertEqual(event.kind, "binding")
        self.assertEqual(event.text, '"8BitDo Pro 2" button 3')

    def test_hat(self):
        event = controls.parse_probe_line('"Pad" hat 0 left')
        assert event
        self.assertEqual(event.text, '"Pad" hat 0 left')

    def test_axis_whole(self):
        event = controls.parse_probe_line(
            '"Wheel" axis 1            (this direction alone; "axis 1" for both, as a stick or wheel)')
        assert event
        self.assertEqual(event.text, '"Wheel" axis 1')

    def test_axis_half(self):
        event = controls.parse_probe_line('"Wheel" axis 2-            (this direction alone)')
        assert event
        self.assertEqual(event.text, '"Wheel" axis 2-')

    def test_axis_invert(self):
        event = controls.parse_probe_line(
            '"Pedals" axis 1 invert      (rests high: a pedal or lever)')
        assert event
        self.assertEqual(event.text, '"Pedals" axis 1 invert')

    def test_key(self):
        event = controls.parse_probe_line("key Left Shift")
        assert event
        self.assertEqual(event.text, "key Left Shift")

    def test_prose_is_ignored(self):
        for line in ("", "Press a key or a button, or move an axis well away from where it rests, and the",
                     "source to bind it with is printed: put it after \"<pad control> =\" in controls.txt.",
                     "Escape, with this window in front, or closing it quits."):
            self.assertIsNone(controls.parse_probe_line(line), line)


class EditTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = Path(self.tmp.name) / "controls.txt"

    def tearDown(self):
        self.tmp.cleanup()

    def test_read_bindings_is_case_insensitive(self):
        self.path.write_text("# my pad\nSTICK_X = \"Wheel\" axis 0 gain 2.5\n"
                             'a = key Return\nffb_gain = 0.5\n', encoding="utf-8")
        bindings = controls.read_bindings(self.path)
        self.assertEqual(bindings["stick_x"], ['"Wheel" axis 0 gain 2.5'])
        self.assertEqual(bindings["A"], ["key Return"])
        self.assertNotIn("ffb_gain", bindings)

    def test_set_binding_replaces_only_that_control(self):
        self.path.write_text("A = key X\nstick_x_min = 19\nffb = off\n", encoding="utf-8")
        controls.set_binding(self.path, "a", '"Pad" button 0')
        text = self.path.read_text(encoding="utf-8")
        self.assertIn("A = \"Pad\" button 0", text)
        self.assertNotIn("key X", text)
        self.assertIn("stick_x_min = 19", text)
        self.assertIn("ffb = off", text)
        self.assertEqual(controls.read_bindings(self.path)["A"], ['"Pad" button 0'])

    def test_set_binding_keeps_other_bindings_for_the_control(self):
        self.path.write_text("A = key X\nA = \"Pad\" button 0\n", encoding="utf-8")
        controls.set_binding(self.path, "A", "key Z")
        self.assertEqual(controls.read_bindings(self.path)["A"], ["key Z"])

    def test_set_binding_creates_file(self):
        controls.set_binding(self.path, "start", "key Return")
        self.assertEqual(self.path.read_text(encoding="utf-8"), "start = key Return\n")

    def test_comment_inside_device_name_is_not_a_comment(self):
        self.path.write_text('A = "Pad #2" button 0   # the second pad\n', encoding="utf-8")
        self.assertEqual(controls.read_bindings(self.path)["A"], ['"Pad #2" button 0'])

    def test_remove_last_binding_deletes_file(self):
        controls.set_binding(self.path, "B", "key Z")
        controls.remove_binding(self.path, "b")
        self.assertFalse(self.path.exists())

    def test_remove_keeps_settings(self):
        self.path.write_text("B = key Z\nffb = on\n", encoding="utf-8")
        controls.remove_binding(self.path, "B")
        self.assertTrue(self.path.exists())
        self.assertEqual(self.path.read_text(encoding="utf-8"), "ffb = on\n")

    def test_unknown_control_is_rejected(self):
        with self.assertRaises(ValueError):
            controls.set_binding(self.path, "stick_z", "key A")

    def test_preserved_settings(self):
        self.path.write_text("A = key X\nffb_gain = 0.5\nstick_x_min = 19\n", encoding="utf-8")
        preserved = controls.read_preserved(self.path)
        self.assertEqual([(p.key, p.value) for p in preserved],
                         [("ffb_gain", "0.5"), ("stick_x_min", "19")])


if __name__ == "__main__":
    unittest.main()
