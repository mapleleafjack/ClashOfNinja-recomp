"""The controller-binding dialog: watch the game's probe, then write the binding."""
from __future__ import annotations

from PySide6.QtCore import QEvent, Qt, QTimer
from PySide6.QtWidgets import (
    QApplication, QComboBox, QDialog, QHBoxLayout, QLabel, QListWidget, QListWidgetItem,
    QPushButton, QVBoxLayout, QWidget,
)

from . import controls
from .keymap import source_for_key
from .processes import StreamingProcess
from .project import Project
from .widgets import Divider


class CaptureDialog(QDialog):
    """Listen to `naruto --list-controls` and bind one pad control to one input.

    The probe is how the runtime itself names devices, buttons and axes, so whatever is
    captured here is written verbatim into controls.txt. Keyboard bindings are taken from
    this window instead: the probe's window does not have focus, ours does.
    """

    def __init__(self, project: Project, target: str | None = None,
                 parent: QWidget | None = None):
        super().__init__(parent)
        self.project = project
        self.result_binding: tuple[str, str] | None = None
        self._probe = StreamingProcess(self)
        self._buffer = ""
        self._devices: list[str] = []
        self._probe_started = False
        self._seen: list[str] = []

        self.setWindowTitle("Bind a control" if target else "Detect controllers")
        self.setMinimumWidth(600)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(20, 18, 20, 18)
        layout.setSpacing(12)

        headline = QLabel("Press a button on your controller, or a key in this window.")
        headline.setObjectName("CardTitle")
        layout.addWidget(headline)
        hint = QLabel(
            "Controller input is read by the game's own probe, so the binding matches what "
            "the runtime expects. Keyboard names follow SDL's scancodes, which are the "
            "US-layout key positions. A small window from the game may open behind this "
            "one; it can stay behind."
        )
        hint.setObjectName("Muted")
        hint.setWordWrap(True)
        layout.addWidget(hint)
        layout.addWidget(Divider())

        top = QHBoxLayout()
        top.setSpacing(10)
        top.addWidget(QLabel("Bind to:"))
        self.target = QComboBox()
        for name, label in controls.PAD_CONTROLS:
            self.target.addItem(name if label == name else f"{label}  ({name})", name)
        if target:
            index = self.target.findData(controls.canonical_control(target) or target)
            self.target.setCurrentIndex(max(0, index))
        else:
            self.target.setCurrentIndex(-1)
            self.target.setPlaceholderText("choose a control")
        self.target.currentIndexChanged.connect(self._update_bind_button)
        top.addWidget(self.target)
        top.addStretch(1)
        self.listen = QLabel("● Listening…")
        self.listen.setStyleSheet("color: #e8b64c; font-size: 12px;")
        top.addWidget(self.listen)
        layout.addLayout(top)

        columns = QHBoxLayout()
        columns.setSpacing(12)

        left = QVBoxLayout()
        left.setSpacing(6)
        left.addWidget(self._section_label("Devices"))
        self.devices = QListWidget()
        self.devices.setMinimumHeight(130)
        left.addWidget(self.devices)
        columns.addLayout(left, 1)

        right = QVBoxLayout()
        right.setSpacing(6)
        right.addWidget(self._section_label("Input seen"))
        self.events = QListWidget()
        self.events.setMinimumHeight(130)
        self.events.itemSelectionChanged.connect(self._update_bind_button)
        right.addWidget(self.events)
        columns.addLayout(right, 1)
        layout.addLayout(columns, 1)

        self.message = QLabel("")
        self.message.setObjectName("Muted")
        self.message.setWordWrap(True)
        layout.addWidget(self.message)

        buttons = QHBoxLayout()
        self.clear_button = QPushButton("Clear")
        self.clear_button.clicked.connect(self._clear_events)
        buttons.addWidget(self.clear_button)
        buttons.addStretch(1)
        self.bind_button = QPushButton("Bind")
        self.bind_button.setObjectName("Primary")
        self.bind_button.setEnabled(False)
        self.bind_button.clicked.connect(self._bind)
        self.close_button = QPushButton("Close")
        self.close_button.clicked.connect(self.reject)
        buttons.addWidget(self.bind_button)
        buttons.addWidget(self.close_button)
        layout.addLayout(buttons)

        self._probe.output.connect(self._on_output)
        self._probe.finished.connect(self._on_probe_finished)

    @staticmethod
    def _section_label(text: str) -> QLabel:
        label = QLabel(text.upper())
        label.setStyleSheet("color: #98a1b2; font-size: 10px; font-weight: 700;")
        return label

    # -- lifecycle ---------------------------------------------------------
    def showEvent(self, event) -> None:  # noqa: N802 - Qt naming
        super().showEvent(event)
        QApplication.instance().installEventFilter(self)
        self._start_probe()
        self._refocus()
        # The probe opens a window of its own; take the focus back for keyboard capture.
        QTimer.singleShot(500, self._refocus)

    def _refocus(self) -> None:
        if self.isVisible():
            self.raise_()
            self.activateWindow()
            self.setFocus(Qt.FocusReason.ActiveWindowFocusReason)

    def closeEvent(self, event) -> None:  # noqa: N802 - Qt naming
        self._stop_probe()
        QApplication.instance().removeEventFilter(self)
        super().closeEvent(event)

    def done(self, result: int) -> None:
        self._stop_probe()
        QApplication.instance().removeEventFilter(self)
        super().done(result)

    def _start_probe(self) -> None:
        if self._probe_started:
            return
        if not self.project.binary.is_file():
            self.listen.setText("● Game not built")
            self.listen.setStyleSheet("color: #f0705c; font-size: 12px;")
            self.message.setText("Build the game first: the probe is the game's own "
                                 "--list-controls mode.")
            return
        self._probe_started = True
        self._probe.start([str(self.project.binary), "--list-controls"], self.project.naruto)

    def _stop_probe(self) -> None:
        self._probe.kill()

    def _on_probe_finished(self, code: int) -> None:
        if self.isVisible():
            self.listen.setText("● Probe stopped")
            self.listen.setStyleSheet("color: #98a1b2; font-size: 12px;")

    # -- probe output ------------------------------------------------------
    def _on_output(self, chunk: str) -> None:
        self._buffer += chunk
        while "\n" in self._buffer:
            line, self._buffer = self._buffer.split("\n", 1)
            event = controls.parse_probe_line(line)
            if event is None:
                continue
            if event.kind == "device":
                self._add_device(event)
            else:
                self._add_event(event)

    def _add_device(self, event: controls.ProbeEvent) -> None:
        if event.text in self._devices:
            return
        self._devices.append(event.text)
        item = QListWidgetItem(event.text)
        item.setToolTip(event.detail)
        self.devices.addItem(item)

    def _add_event(self, event: controls.ProbeEvent) -> None:
        if self._seen and self._seen[-1] == event.text:
            return
        self._seen.append(event.text)
        item = QListWidgetItem(event.text)
        item.setData(Qt.ItemDataRole.UserRole, event.text)
        item.setToolTip(event.detail)
        self.events.insertItem(0, item)
        while self.events.count() > 30:
            self.events.takeItem(self.events.count() - 1)
        self.events.setCurrentRow(0)
        self.message.setText(f"Caught: {event.text}")
        self._update_bind_button()

    def _clear_events(self) -> None:
        self.events.clear()
        self._seen.clear()
        self.message.setText("")

    # -- keyboard ----------------------------------------------------------
    def eventFilter(self, obj, event) -> bool:  # noqa: N802 - Qt naming
        target = getattr(obj, "window", lambda: None)()
        if (event.type() == QEvent.Type.KeyPress and self.isVisible() and target is self
                and not event.isAutoRepeat()):
            if event.key() == Qt.Key.Key_Escape:
                self.reject()
                return True
            if event.modifiers() & (Qt.KeyboardModifier.ControlModifier
                                    | Qt.KeyboardModifier.AltModifier
                                    | Qt.KeyboardModifier.MetaModifier):
                if event.key() not in (Qt.Key.Key_Control, Qt.Key.Key_Alt, Qt.Key.Key_Meta):
                    return False
            source = source_for_key(event)
            if source is None:
                self.message.setText("That key has no SDL name the game recognises; try another.")
                return True
            self._add_event(controls.ProbeEvent("binding", source, "keyboard"))
            return True
        return super().eventFilter(obj, event)

    # -- result ------------------------------------------------------------
    def _selected_source(self) -> str | None:
        item = self.events.currentItem()
        if item is None:
            return None
        return item.data(Qt.ItemDataRole.UserRole)

    def _update_bind_button(self) -> None:
        has_target = self.target.currentIndex() >= 0
        self.bind_button.setEnabled(bool(has_target and self._selected_source()))

    def _bind(self) -> None:
        source = self._selected_source()
        target = self.target.currentData()
        if not source or not target:
            return
        try:
            controls.set_binding(self.project.controls, target, source)
        except (OSError, ValueError) as exc:
            self.message.setText(f"Could not write controls.txt: {exc}")
            return
        self.result_binding = (str(target), source)
        self.accept()
