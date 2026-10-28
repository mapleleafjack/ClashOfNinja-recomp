"""The launcher window."""
from __future__ import annotations

import os
import sys
from pathlib import Path

from PySide6.QtCore import QSettings, Qt, QTimer, QUrl
from PySide6.QtGui import QAction, QDesktopServices, QGuiApplication
from PySide6.QtWidgets import (
    QAbstractItemView, QApplication, QCheckBox, QComboBox, QDialog, QFileDialog,
    QHBoxLayout, QHeaderView, QLabel, QMainWindow, QMessageBox, QProgressBar,
    QPushButton, QScrollArea, QSizePolicy, QTableWidget, QTableWidgetItem, QVBoxLayout,
    QWidget,
)

from . import __version__, controls
from .capture import CaptureDialog
from .processes import CommandQueue, GameProcess, Step, format_command
from .project import (
    GAME_SUBTITLE, GAME_TITLE, Project, check_tools, find_tool, locate_project,
    looks_like_project,
)
from .theme import BAD, MUTED, OK, apply_theme
from .widgets import Card, LogView, StatusPill

VERSION = __version__


def _short_path(path: Path) -> str:
    """The project path, with the home directory abbreviated and the middle elided."""
    text = str(path)
    home = str(Path.home())
    if text.startswith(home):
        text = "~" + text[len(home):]
    if len(text) <= 52:
        return text
    parts = text.split(os.sep)
    return os.sep.join([parts[0], "…", *parts[-2:]])


DISC_FILTER = ("Disc images (*.iso *.ciso *.rvz *.wia *.gcz *.wbfs *.tgc *.nfs);;"
               "All files (*)")

DEFAULT_MAPPING = (
    "Without controls.txt the game uses its built-in mapping: X=A, Z=B, C=X, S=Y, "
    "D=Z, Return=Start, IJKL=D-pad, arrow keys=stick, Q/W=L/R, and the first connected "
    "gamepad is mapped automatically."
)


class MainWindow(QMainWindow):
    def __init__(self, project: Project | None):
        super().__init__()
        self.settings = QSettings()
        self.project = project
        self.queue = CommandQueue(self)
        self.game = GameProcess(self)

        self.setWindowTitle(f"{GAME_TITLE} — Launcher")
        self.setMinimumSize(860, 640)
        self.resize(1000, 920)

        self.queue.output.connect(self._log)
        self.queue.progress.connect(self._on_progress)
        self.queue.finished.connect(self._on_queue_finished)
        self.game.output.connect(self._log)
        self.game.started.connect(self._on_game_started)
        self.game.finished.connect(self._on_game_finished)

        self._build_ui()
        self._build_menu()
        self._restore_settings()

        if self.project is None:
            QTimer.singleShot(0, self._first_run)
        self.refresh()

    # -- construction ------------------------------------------------------
    def _build_ui(self) -> None:
        central = QWidget()
        outer = QVBoxLayout(central)
        outer.setContentsMargins(22, 18, 22, 16)
        outer.setSpacing(12)

        header = QHBoxLayout()
        titles = QVBoxLayout()
        titles.setSpacing(2)
        title = QLabel(GAME_TITLE)
        title.setObjectName("Title")
        subtitle = QLabel(GAME_SUBTITLE)
        subtitle.setObjectName("Subtitle")
        titles.addWidget(title)
        titles.addWidget(subtitle)
        header.addLayout(titles)
        header.addStretch(1)

        project_box = QVBoxLayout()
        project_box.setSpacing(2)
        project_caption = QLabel("PROJECT")
        project_caption.setStyleSheet("color: #98a1b2; font-size: 10px; font-weight: 700;")
        project_caption.setAlignment(Qt.AlignmentFlag.AlignRight)
        self.project_label = QLabel("not located")
        self.project_label.setObjectName("Muted")
        self.project_label.setAlignment(Qt.AlignmentFlag.AlignRight)
        self.locate_button = QPushButton("Locate Project Folder…")
        self.locate_button.setObjectName("Link")
        self.locate_button.setCursor(Qt.CursorShape.PointingHandCursor)
        self.locate_button.clicked.connect(self._locate_project)
        project_box.addWidget(project_caption)
        project_box.addWidget(self.project_label)
        project_box.addWidget(self.locate_button, 0, Qt.AlignmentFlag.AlignRight)
        header.addLayout(project_box)
        outer.addLayout(header)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        content = QWidget()
        body = QVBoxLayout(content)
        body.setContentsMargins(0, 0, 8, 0)
        body.setSpacing(12)
        body.addWidget(self._disc_card())
        body.addWidget(self._controls_card())
        body.addWidget(self._play_card())
        body.addStretch(1)
        scroll.setWidget(content)
        outer.addWidget(scroll, 1)
        outer.addWidget(self._log_card())

        self.setCentralWidget(central)

    def _disc_card(self) -> Card:
        self.disc_card = Card("Game disc")
        self.disc_pill = StatusPill()
        self.disc_card.header_right.addWidget(self.disc_pill)

        self.disc_text = QLabel()
        self.disc_text.setWordWrap(True)
        self.disc_text.setObjectName("Muted")
        self.disc_card.body.addWidget(self.disc_text)

        row = QHBoxLayout()
        row.setSpacing(8)
        self.choose_button = QPushButton("Choose Disc Image…")
        self.choose_button.setObjectName("Primary")
        self.choose_button.clicked.connect(self._choose_image)
        self.build_button = QPushButton("Build Game")
        self.build_button.clicked.connect(self._build)
        self.disc_folder_button = QPushButton("Disc Folder")
        self.disc_folder_button.clicked.connect(
            lambda: self._open_path(self.project.disc if self.project else None))
        row.addWidget(self.choose_button)
        row.addWidget(self.build_button)
        row.addWidget(self.disc_folder_button)
        row.addStretch(1)
        self.build_after = QCheckBox("Build after extracting")
        self.build_after.setChecked(True)
        row.addWidget(self.build_after)
        self.disc_card.body.addLayout(row)

        self.tools_label = QLabel()
        self.tools_label.setStyleSheet("font-size: 11px; color: #98a1b2;")
        self.tools_label.setTextFormat(Qt.TextFormat.RichText)
        self.disc_card.body.addWidget(self.tools_label)
        return self.disc_card

    def _controls_card(self) -> Card:
        self.controls_card = Card("Controllers")
        self.bindings_pill = StatusPill()
        self.controls_card.header_right.addWidget(self.bindings_pill)

        self.controls_text = QLabel()
        self.controls_text.setWordWrap(True)
        self.controls_text.setObjectName("Muted")
        self.controls_text.setText(DEFAULT_MAPPING)
        self.controls_card.body.addWidget(self.controls_text)

        self.bindings_table = QTableWidget(0, 3)
        self.bindings_table.setHorizontalHeaderLabels(["Control", "Bound to", ""])
        self.bindings_table.verticalHeader().setVisible(False)
        self.bindings_table.setShowGrid(False)
        self.bindings_table.setSelectionMode(QAbstractItemView.SelectionMode.NoSelection)
        self.bindings_table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self.bindings_table.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        header = self.bindings_table.horizontalHeader()
        header.setSectionResizeMode(0, QHeaderView.ResizeMode.ResizeToContents)
        header.setSectionResizeMode(1, QHeaderView.ResizeMode.Stretch)
        header.setSectionResizeMode(2, QHeaderView.ResizeMode.Fixed)
        self.bindings_table.setColumnWidth(2, 92)
        self.bindings_table.setMinimumHeight(40)
        self.bindings_table.setSizePolicy(QSizePolicy.Policy.Expanding,
                                          QSizePolicy.Policy.Preferred)
        self.controls_card.body.addWidget(self.bindings_table)

        self.settings_note = QLabel()
        self.settings_note.setObjectName("Muted")
        self.settings_note.setStyleSheet("font-size: 11px; color: #98a1b2;")
        self.controls_card.body.addWidget(self.settings_note)

        row = QHBoxLayout()
        row.setSpacing(8)
        self.bind_button = QPushButton("Add Binding…")
        self.bind_button.clicked.connect(lambda: self._capture(None))
        self.detect_button = QPushButton("Detect Controllers…")
        self.detect_button.clicked.connect(self._detect)
        self.open_controls_button = QPushButton("Open controls.txt")
        self.open_controls_button.clicked.connect(
            lambda: self._open_path(self.project.controls if self.project else None))
        self.reset_controls_button = QPushButton("Reset to Defaults")
        self.reset_controls_button.clicked.connect(self._reset_controls)
        row.addWidget(self.bind_button)
        row.addWidget(self.detect_button)
        row.addStretch(1)
        row.addWidget(self.open_controls_button)
        row.addWidget(self.reset_controls_button)
        self.controls_card.body.addLayout(row)
        return self.controls_card

    def _play_card(self) -> Card:
        self.play_card = Card("Play")
        self.game_pill = StatusPill()
        self.play_card.header_right.addWidget(self.game_pill)

        options = QHBoxLayout()
        options.setSpacing(14)
        options.addWidget(QLabel("Window scale"))
        self.scale = QComboBox()
        for factor in (1, 2, 3, 4):
            self.scale.addItem(f"{factor}×  ({640 * factor}×{480 * factor})", factor)
        self.scale.setCurrentIndex(1)
        options.addWidget(self.scale)
        self.mute_music = QCheckBox("Mute music")
        options.addWidget(self.mute_music)
        self.input_log = QCheckBox("Log controller input")
        self.input_log.setChecked(True)
        self.input_log.setToolTip("Writes each run's controller input to saves/inputs, so a "
                                  "run can be replayed with --replay.")
        options.addWidget(self.input_log)
        options.addStretch(1)
        self.play_card.body.addLayout(options)

        row = QHBoxLayout()
        row.setSpacing(8)
        self.play_button = QPushButton("Play")
        self.play_button.setObjectName("Primary")
        self.play_button.setMinimumWidth(140)
        self.play_button.clicked.connect(self._play)
        self.stop_button = QPushButton("Stop")
        self.stop_button.setObjectName("Danger")
        self.stop_button.setEnabled(False)
        self.stop_button.clicked.connect(self.game.stop)
        self.saves_button = QPushButton("Saves Folder")
        self.saves_button.clicked.connect(
            lambda: self._open_path(self.project.saves if self.project else None))
        row.addWidget(self.play_button)
        row.addWidget(self.stop_button)
        row.addStretch(1)
        row.addWidget(self.saves_button)
        self.play_card.body.addLayout(row)

        self.play_status = QLabel("")
        self.play_status.setObjectName("Muted")
        self.play_status.setWordWrap(True)
        self.play_card.body.addWidget(self.play_status)
        return self.play_card

    def _log_card(self) -> Card:
        self.log_card = Card("Activity")
        self.log_toggle = QPushButton("Hide")
        self.log_toggle.setObjectName("Link")
        self.log_toggle.setCursor(Qt.CursorShape.PointingHandCursor)
        self.log_toggle.clicked.connect(self._toggle_log)
        copy_button = QPushButton("Copy")
        copy_button.setObjectName("Link")
        copy_button.clicked.connect(
            lambda: QGuiApplication.clipboard().setText(self.log.toPlainText()))
        clear_button = QPushButton("Clear")
        clear_button.setObjectName("Link")
        clear_button.clicked.connect(lambda: self.log.clear())
        self.log_card.header_right.addWidget(self.log_toggle)
        self.log_card.header_right.addWidget(copy_button)
        self.log_card.header_right.addWidget(clear_button)
        self.log = LogView()
        self.log.setMinimumHeight(110)
        self.log.setMaximumHeight(140)
        self.log.setVisible(False)
        self.log_toggle.setText("Show")
        self.log_card.body.addWidget(self.log)

        self.progress = QProgressBar()
        self.progress.setRange(0, 0)
        self.progress.setTextVisible(False)
        self.progress.setVisible(False)
        self.log_card.body.addWidget(self.progress)
        self.task_label = QLabel("")
        self.task_label.setStyleSheet(f"color: {MUTED}; font-size: 11px;")
        self.log_card.body.addWidget(self.task_label)
        return self.log_card

    def _toggle_log(self) -> None:
        visible = not self.log.isVisible()
        self.log.setVisible(visible)
        self.log_toggle.setText("Hide" if visible else "Show")

    def _build_menu(self) -> None:
        file_menu = self.menuBar().addMenu("File")
        locate = QAction("Locate Project Folder…", self)
        locate.triggered.connect(self._locate_project)
        file_menu.addAction(locate)
        open_project = QAction("Open Project Folder", self)
        open_project.triggered.connect(
            lambda: self._open_path(self.project.root if self.project else None))
        file_menu.addAction(open_project)
        file_menu.addSeparator()
        quit_action = QAction("Quit", self)
        quit_action.setShortcut("Ctrl+Q")
        quit_action.triggered.connect(self.close)
        file_menu.addAction(quit_action)

        help_menu = self.menuBar().addMenu("Help")
        about = QAction(f"About {GAME_TITLE} Launcher", self)
        about.triggered.connect(self._about)
        help_menu.addAction(about)

    # -- settings ----------------------------------------------------------
    def _restore_settings(self) -> None:
        saved = self.settings.value("projectRoot")
        if self.project is None and isinstance(saved, str):
            candidate = Path(saved)
            if looks_like_project(candidate):
                self.project = Project(candidate)
        if saved and self.project and str(self.project.root) != saved:
            self.settings.setValue("projectRoot", str(self.project.root))

        geometry = self.settings.value("geometry")
        if geometry is not None:
            self.restoreGeometry(geometry)
        scale = self.settings.value("scale", 2)
        try:
            index = self.scale.findData(int(scale))
            if index >= 0:
                self.scale.setCurrentIndex(index)
        except (TypeError, ValueError):
            pass
        self.mute_music.setChecked(self.settings.value("muteMusic", False, type=bool))
        self.input_log.setChecked(self.settings.value("inputLog", True, type=bool))
        self.build_after.setChecked(self.settings.value("buildAfter", True, type=bool))

    def _save_settings(self) -> None:
        self.settings.setValue("geometry", self.saveGeometry())
        self.settings.setValue("scale", self.scale.currentData())
        self.settings.setValue("muteMusic", self.mute_music.isChecked())
        self.settings.setValue("inputLog", self.input_log.isChecked())
        self.settings.setValue("buildAfter", self.build_after.isChecked())
        if self.project:
            self.settings.setValue("projectRoot", str(self.project.root))

    def closeEvent(self, event) -> None:  # noqa: N802 - Qt naming
        self._save_settings()
        self.queue.stop()
        self.game.shutdown()
        super().closeEvent(event)

    # -- project -----------------------------------------------------------
    def _first_run(self) -> None:
        if self.project is not None:
            return
        answer = QMessageBox.question(
            self, "Locate the project",
            "Point the launcher at the recompilation project.\n\n"
            "That is the folder containing naruto/ and tools/extract_disc.py — the folder "
            "this launcher was built from.",
            QMessageBox.StandardButton.Ok | QMessageBox.StandardButton.Cancel,
        )
        if answer == QMessageBox.StandardButton.Ok:
            self._locate_project()

    def _locate_project(self) -> None:
        start = str(self.project.root) if self.project else str(Path.home())
        folder = QFileDialog.getExistingDirectory(self, "Choose the project folder", start)
        if not folder:
            return
        path = Path(folder)
        if not looks_like_project(path):
            QMessageBox.warning(
                self, "Not the project folder",
                "That folder does not contain naruto/CMakeLists.txt and "
                "tools/extract_disc.py.",
            )
            return
        self.project = Project(path)
        self.settings.setValue("projectRoot", str(path))
        self.log.clear()
        self._log(f"[launcher] project: {path}\n")
        self.refresh()

    def refresh(self) -> None:
        p = self.project
        self.locate_button.setVisible(p is None)
        self.locate_button.setEnabled(True)
        self.project_label.setText(_short_path(p.root) if p else "not located")
        self.project_label.setToolTip(str(p.root) if p else "")

        built = bool(p and p.binary.is_file())
        disc = p.disc_status() if p else None

        if p is None:
            self.disc_pill.set_state("Project not located", "bad")
            self.disc_text.setText("Locate the project folder to continue.")
        elif disc and disc.dol_verified:
            self.disc_pill.set_state("Disc ready", "ok")
            self.disc_text.setText(
                "The disc is extracted and main.dol matches the supported build "
                f"({disc.dol_sha1[:12]}…). The original image is no longer needed."
            )
        elif disc and disc.extracted:
            self.disc_pill.set_state("Different main.dol", "warn")
            self.disc_text.setText(
                "A disc is extracted, but main.dol is "
                f"{disc.dol_sha1[:12] if disc.dol_sha1 else 'unreadable'}…, not the supported "
                f"{disc.expected_sha1[:12]}…. Re-extract from the matching image."
            )
        else:
            self.disc_pill.set_state("No disc yet", "bad")
            self.disc_text.setText(
                "Choose your own disc image (ISO, CISO, RVZ, WIA, GCZ, WBFS, TGC or NFS). "
                "The launcher extracts it into naruto/rom/disc; nothing from the disc is "
                "shipped with the launcher or committed."
            )

        for button in (self.choose_button, self.build_button, self.disc_folder_button,
                       self.bind_button, self.detect_button, self.open_controls_button,
                       self.reset_controls_button, self.saves_button):
            button.setEnabled(p is not None)

        self.build_button.setEnabled(bool(p and disc and disc.extracted))
        self.disc_folder_button.setEnabled(bool(disc and disc.extracted))
        self.open_controls_button.setEnabled(bool(p and p.controls_exists()))
        self.reset_controls_button.setEnabled(bool(p and p.controls_exists()))
        self.play_button.setEnabled(built and bool(disc and disc.dol_verified)
                                    and not self.queue.busy and not self.game.running)
        self.stop_button.setEnabled(self.game.running)
        self.choose_button.setText("Choose Disc Image…" if not (disc and disc.extracted)
                                   else "Re-extract Disc Image…")

        if p is not None:
            self.tools_label.setText(self._tools_html())
        else:
            self.tools_label.clear()

        if self.game.running:
            self.game_pill.set_state("Running", "ok")
        elif built:
            self.game_pill.set_state("Ready to play", "muted")
        else:
            self.game_pill.set_state("Not built", "warn")

        if not built:
            self.play_status.setText(
                "Build the game to enable Play." if p is not None else ""
            )
        elif not disc or not disc.dol_verified:
            self.play_status.setText("The game is built but the disc is not ready.")
        elif not self.game.running:
            self.play_status.setText(
                "The game opens in its own window; closing it returns here. Escape quits."
            )

        self._refresh_controls()

    def _tools_html(self) -> str:
        parts = []
        for tool in check_tools():
            color = OK if tool.available else (BAD if tool.needed else MUTED)
            label = tool.name if tool.needed else f"{tool.name} (optional)"
            mark = "●"
            hint = "" if tool.available else f" — {tool.install_hint}"
            parts.append(f"<span style='color:{color};'>{mark}</span> {label}{hint}")
        return " &nbsp;&nbsp; ".join(parts)

    def _refresh_controls(self) -> None:
        p = self.project
        if p is None:
            self.bindings_table.setRowCount(0)
            self.bindings_table.setVisible(False)
            self.settings_note.setVisible(False)
            self.bindings_pill.set_state("", "muted")
            return

        bindings = controls.read_bindings(p.controls)
        flattened = [(control, source) for control, sources in bindings.items()
                     for source in sources]
        preserved = controls.read_preserved(p.controls)

        self.bindings_table.setRowCount(len(flattened))
        for row, (control, source) in enumerate(flattened):
            name_item = QTableWidgetItem(controls.LABELS.get(control, control))
            name_item.setToolTip(control)
            source_item = QTableWidgetItem(source)
            source_item.setFont(self.log.font())
            self.bindings_table.setItem(row, 0, name_item)
            self.bindings_table.setItem(row, 1, source_item)
            remove = QPushButton("Remove")
            remove.setObjectName("Compact")
            remove.setFixedWidth(78)
            remove.setCursor(Qt.CursorShape.PointingHandCursor)
            remove.clicked.connect(lambda _=False, c=control: self._remove_binding(c))
            self.bindings_table.setCellWidget(row, 2, remove)
        self.bindings_table.setVisible(bool(flattened))
        if flattened:
            section = self.bindings_table.verticalHeader().defaultSectionSize()
            height = self.bindings_table.horizontalHeader().height() + section * len(flattened) + 4
            self.bindings_table.setFixedHeight(min(height, 280))

        count = len(flattened)
        if count:
            self.bindings_pill.set_state(f"{count} binding{'s' if count != 1 else ''}", "ok")
        else:
            self.bindings_pill.set_state("Built-in mapping", "muted")

        notes = []
        if preserved:
            notes.append(f"{len(preserved)} extra setting"
                         f"{'s' if len(preserved) != 1 else ''} kept in controls.txt")
        if not p.controls_exists():
            notes.append("controls.txt absent, so the built-in mapping is used")
        self.settings_note.setText(" · ".join(notes))
        self.settings_note.setVisible(bool(notes))

    # -- activity ----------------------------------------------------------
    def _log(self, text: str) -> None:
        if hasattr(self, "log"):
            self.log.append_text(text)

    def _on_progress(self, label: str) -> None:
        busy = self.queue.busy
        if busy and not self.log.isVisible():
            self._toggle_log()
        self.progress.setVisible(busy)
        self.task_label.setText(label)
        self._set_busy(busy)

    def _set_busy(self, busy: bool) -> None:
        for button in (self.choose_button, self.build_button, self.bind_button,
                       self.detect_button, self.open_controls_button,
                       self.reset_controls_button):
            button.setEnabled(not busy and self.project is not None)
        self.locate_button.setEnabled(not busy)
        self.build_after.setEnabled(not busy)
        self.play_button.setEnabled(not busy and self.project is not None
                                    and self.project.binary.is_file()
                                    and self.project.disc_status().dol_verified)
        if not busy:
            self.refresh()

    def _on_queue_finished(self, code: int) -> None:
        self.progress.setVisible(False)
        self.task_label.setText("")
        if code == 0:
            self._log("[launcher] done\n")
        elif code != 130:
            self._log(f"[launcher] failed (status {code}); see the output above\n")
        self.refresh()

    # -- disc --------------------------------------------------------------
    def _choose_image(self) -> None:
        if self.project is None:
            return
        image, _ = QFileDialog.getOpenFileName(
            self, "Choose your disc image", str(Path.home()), DISC_FILTER)
        if image:
            self._start_setup(Path(image))

    def _start_setup(self, image: Path) -> None:
        p = self.project
        if p is None or self.queue.busy:
            return
        if not image.is_file():
            QMessageBox.warning(self, "Disc image", f"No such file:\n{image}")
            return
        if p.dol.exists():
            answer = QMessageBox.question(
                self, "Replace the extracted disc?",
                "A disc is already extracted. Extract again from this image and replace it?",
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            )
            if answer != QMessageBox.StandardButton.Yes:
                return

        steps = [Step(tuple(self._extract_argv(image)), p.root,
                      f"Extracting {image.name}…")]
        if self.build_after.isChecked():
            build = self._build_steps()
            if build is None:
                return
            steps += build
        self._log(f"[launcher] image: {image}\n")
        self.queue.run(steps)

    def _extract_argv(self, image: Path) -> list[str]:
        assert self.project is not None
        if getattr(sys, "frozen", False):
            return [sys.executable, "--extract", str(self.project.root), str(image)]
        entry = Path(__file__).resolve().parent.parent / "main.py"
        return [sys.executable, str(entry), "--extract", str(self.project.root), str(image)]

    def _build(self) -> None:
        if self.project is None or self.queue.busy:
            return
        steps = self._build_steps()
        if steps is not None:
            self.queue.run(steps)

    def _build_steps(self) -> list[Step] | None:
        p = self.project
        assert p is not None
        missing = [tool for tool in check_tools() if tool.needed and not tool.available]
        if missing:
            lines = "\n".join(f"  {t.name}: {t.install_hint}" for t in missing)
            self._log(f"[launcher] missing build tools:\n{lines}\n")
            QMessageBox.warning(self, "Missing build tools",
                                "The build needs:\n\n" + lines)
            return None
        cmake = find_tool("cmake")
        ninja = find_tool("ninja")
        assert cmake and ninja
        return [
            Step((cmake, "-S", str(p.naruto), "-B", str(p.build_dir), "-G", "Ninja",
                  "-DCMAKE_BUILD_TYPE=RelWithDebInfo"), p.root, "Configuring CMake…"),
            Step((ninja, "-C", str(p.build_dir)), p.root, "Compiling the game…"),
        ]

    # -- controllers -------------------------------------------------------
    def _capture(self, target: str | None) -> None:
        p = self.project
        if p is None:
            return
        dialog = CaptureDialog(p, target, self)
        if dialog.exec() == QDialog.DialogCode.Accepted and dialog.result_binding:
            control, source = dialog.result_binding
            self._log(f"[launcher] bound {controls.LABELS.get(control, control)} "
                      f"to {source}\n")
            self.refresh()

    def _detect(self) -> None:
        self._capture(None)

    def _remove_binding(self, control: str) -> None:
        p = self.project
        if p is None:
            return
        controls.remove_binding(p.controls, control)
        self._log(f"[launcher] removed bindings for {controls.LABELS.get(control, control)}\n")
        self.refresh()

    def _reset_controls(self) -> None:
        p = self.project
        if p is None or not p.controls_exists():
            return
        answer = QMessageBox.question(
            self, "Reset controls?",
            "Delete controls.txt and go back to the game's built-in keyboard and "
            "gamepad mapping?",
            QMessageBox.StandardButton.Reset | QMessageBox.StandardButton.Cancel,
        )
        if answer == QMessageBox.StandardButton.Reset:
            controls.clear_bindings(p.controls)
            self._log("[launcher] controls.txt removed\n")
            self.refresh()

    # -- play --------------------------------------------------------------
    def _play(self) -> None:
        p = self.project
        if (p is None or not p.binary.is_file() or self.game.running
                or not p.disc_status().dol_verified):
            return
        argv = [str(p.binary), f"--scale={self.scale.currentData()}"]
        if self.mute_music.isChecked():
            argv.append("--no-music")
        if not self.input_log.isChecked():
            argv.append("--no-input-log")
        argv.append(f"--controls={p.controls}")
        self._save_settings()
        self._log(f"\n$ {format_command(argv)}\n")
        self.play_status.setText("Starting…")
        self.game.start(argv, p.naruto)

    def _on_game_started(self) -> None:
        if not self.log.isVisible():
            self._toggle_log()
        self.refresh()
        self.play_status.setText("Running in its own window. Use Stop to end it.")

    def _on_game_finished(self, code: int, stopped: bool) -> None:
        self.refresh()
        if stopped:
            self.play_status.setText("Stopped from the launcher.")
            self._log("[launcher] game stopped\n")
        else:
            self.play_status.setText(f"The game exited with status {code}.")
            self._log(f"[launcher] game exited with status {code}\n")

    # -- misc --------------------------------------------------------------
    def _open_path(self, path: Path | None) -> None:
        if path is None:
            return
        if not path.exists():
            QMessageBox.information(self, "Not there yet", f"Nothing at:\n{path}")
            return
        QDesktopServices.openUrl(QUrl.fromLocalFile(str(path)))

    def _about(self) -> None:
        QMessageBox.about(
            self, f"{GAME_TITLE} Launcher",
            f"<b>{GAME_TITLE} Launcher</b> {VERSION}<br><br>"
            "Runs a native static recompilation of the GameCube game.<br>"
            "The game code and disc data come from your own disc image; nothing from the "
            "game is included in this launcher.",
        )


def main() -> int:
    app = QApplication(sys.argv)
    app.setOrganizationName("NarutoLauncher")
    app.setApplicationName("Naruto Launcher")
    app.setApplicationDisplayName(f"{GAME_TITLE} Launcher")
    apply_theme(app)

    saved = QSettings().value("projectRoot")
    root = locate_project(saved if isinstance(saved, str) else None)
    window = MainWindow(Project(root) if root else None)
    window.show()
    return app.exec()
