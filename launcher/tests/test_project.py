"""Project discovery, the pinned DOL hash, and disc status."""
from __future__ import annotations

import hashlib
import tempfile
import unittest
from pathlib import Path

from naruto_launcher.project import Project, looks_like_project


class ProjectTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        (self.root / "naruto").mkdir()
        (self.root / "tools").mkdir()
        (self.root / "tools" / "extract_disc.py").write_text("", encoding="utf-8")

    def tearDown(self):
        self.tmp.cleanup()

    def _write_cmake(self, sha: str) -> None:
        (self.root / "naruto" / "CMakeLists.txt").write_text(
            "gcn_add_game(\n  NAME naruto\n  DISC_ID GNREDA\n  DOL_SHA1 " + sha + "\n)\n",
            encoding="utf-8",
        )

    def test_detects_project_layout(self):
        self.assertFalse(looks_like_project(self.root))
        self._write_cmake("0" * 40)
        self.assertTrue(looks_like_project(self.root))

    def test_expected_sha1_comes_from_cmakelists(self):
        self._write_cmake("a" * 40)
        self.assertEqual(Project(self.root).expected_sha1(), "a" * 40)

    def test_missing_cmakelists_falls_back(self):
        self.assertEqual(len(Project(self.root).expected_sha1()), 40)

    def test_disc_status_verifies_main_dol(self):
        dol = b"main dol bytes"
        sha = hashlib.sha1(dol).hexdigest()
        self._write_cmake(sha)
        dol_path = self.root / "naruto" / "rom" / "disc" / "sys" / "main.dol"
        dol_path.parent.mkdir(parents=True)
        dol_path.write_bytes(dol)
        status = Project(self.root).disc_status()
        self.assertTrue(status.extracted)
        self.assertTrue(status.dol_verified)
        self.assertEqual(status.dol_sha1, sha)

    def test_mismatched_dol_is_reported(self):
        self._write_cmake("b" * 40)
        dol_path = self.root / "naruto" / "rom" / "disc" / "sys" / "main.dol"
        dol_path.parent.mkdir(parents=True)
        dol_path.write_bytes(b"something else")
        status = Project(self.root).disc_status()
        self.assertTrue(status.extracted)
        self.assertFalse(status.dol_verified)

    def test_binary_name_per_platform(self):
        binary = Project(self.root).binary
        self.assertIn(binary.name, ("naruto", "naruto.exe"))


if __name__ == "__main__":
    unittest.main()
