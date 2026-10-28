"""Small building blocks for the launcher window."""
from __future__ import annotations

from PySide6.QtWidgets import (
    QFrame, QHBoxLayout, QLabel, QPlainTextEdit, QSizePolicy, QVBoxLayout, QWidget,
)

from .theme import BAD, MUTED, OK, WARN, mono_font


class Card(QFrame):
    """A titled panel. Add rows to `body`; put a pill or link in `header_right`."""

    def __init__(self, title: str, parent: QWidget | None = None):
        super().__init__(parent)
        self.setObjectName("Card")
        outer = QVBoxLayout(self)
        outer.setContentsMargins(18, 16, 18, 18)
        outer.setSpacing(12)

        header = QHBoxLayout()
        header.setSpacing(8)
        self.title = QLabel(title)
        self.title.setObjectName("CardTitle")
        self.header_right = QHBoxLayout()
        self.header_right.setSpacing(8)
        header.addWidget(self.title)
        header.addStretch(1)
        header.addLayout(self.header_right)
        outer.addLayout(header)

        self.body = QVBoxLayout()
        self.body.setSpacing(10)
        outer.addLayout(self.body)


class StatusPill(QLabel):
    """A dot and a short state, coloured by state."""

    COLORS = {"ok": OK, "warn": WARN, "bad": BAD, "muted": MUTED}

    def __init__(self, parent: QWidget | None = None):
        super().__init__(parent)
        self.set_state("", "muted")

    def set_state(self, text: str, state: str = "muted") -> None:
        color = self.COLORS.get(state, MUTED)
        self.setText(f"<span style='color:{color};'>●</span>&nbsp;&nbsp;{text}")
        self.setStyleSheet("font-size: 12px; color: #cfd5e2;")


class Divider(QFrame):
    def __init__(self, parent: QWidget | None = None):
        super().__init__(parent)
        self.setObjectName("Separator")
        self.setFrameShape(QFrame.Shape.HLine)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)


class LogView(QPlainTextEdit):
    """The transcript: commands as they run, and whatever the tools print."""

    def __init__(self, parent: QWidget | None = None):
        super().__init__(parent)
        self.setObjectName("Log")
        self.setReadOnly(True)
        self.setLineWrapMode(QPlainTextEdit.LineWrapMode.NoWrap)
        self.setFont(mono_font(11))
        self.setMinimumHeight(150)
        self.document().setMaximumBlockCount(6000)
        self.setPlaceholderText("Setup and build output appears here.")

    def append_text(self, text: str) -> None:
        if not text:
            return
        bar = self.verticalScrollBar()
        at_bottom = bar.value() >= bar.maximum() - 4
        text = text.replace("\r\n", "\n").replace("\r", "\n")
        self.moveCursor(self.textCursor().MoveOperation.End)
        self.insertPlainText(text)
        if at_bottom:
            bar.setValue(bar.maximum())

