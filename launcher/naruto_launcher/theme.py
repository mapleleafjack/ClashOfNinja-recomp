"""The launcher's look: one dark palette, one stylesheet, both platforms."""
from __future__ import annotations

import sys

from PySide6.QtGui import QColor, QFont, QPalette
from PySide6.QtWidgets import QApplication

ACCENT = "#e2693a"
OK = "#4ec98a"
WARN = "#e8b64c"
BAD = "#f0705c"
MUTED = "#98a1b2"

_MONO = ("SF Mono", "Cascadia Mono", "Consolas", "DejaVu Sans Mono", "Liberation Mono", "monospace")

STYLESHEET = """
QMainWindow, QDialog { background: #12141a; }
QLabel { color: #e7e9ef; }
QLabel#Title { font-size: 27px; font-weight: 700; }
QLabel#Subtitle { color: #98a1b2; font-size: 13px; }
QLabel#CardTitle { font-size: 15px; font-weight: 600; }
QLabel#Muted { color: #98a1b2; }
QLabel#Error { color: #f0705c; }

QFrame#Card { background: #1b1e25; border: 1px solid #272c36; border-radius: 14px; }
QFrame#Separator { background: #272c36; border: none; max-height: 1px; min-height: 1px; }

QPushButton { background: #242932; border: 1px solid #333a46; border-radius: 8px;
              padding: 7px 14px; color: #e7e9ef; }
QPushButton:hover { background: #2c333e; }
QPushButton:pressed { background: #20252d; }
QPushButton:disabled { color: #69707d; background: #1a1d23; border-color: #242932; }
QPushButton#Primary { background: #e2693a; border: 1px solid #e2693a; color: #ffffff;
                      font-weight: 600; padding: 9px 20px; }
QPushButton#Primary:hover { background: #ee7a4b; }
QPushButton#Primary:disabled { background: #513427; border-color: #513427; color: #a08a7c; }
QPushButton#Danger { color: #ff9a86; }
QPushButton#Compact { padding: 3px 8px; font-size: 11px; }
QPushButton#Link { background: transparent; border: none; color: #8fb4ff; padding: 2px 4px; }
QPushButton#Link:hover { color: #b6cdff; }

QPlainTextEdit#Log { background: #0d0f13; border: 1px solid #242932; border-radius: 10px;
                     padding: 8px; color: #c8cede; selection-background-color: #3a4353; }

QProgressBar { border: none; background: #242932; border-radius: 3px; max-height: 6px; }
QProgressBar::chunk { background: #e2693a; border-radius: 3px; }

QTableWidget { background: transparent; border: none; }
QTableWidget::item { padding: 6px 8px; border-bottom: 1px solid #23272f; color: #dfe3ec; }
QTableWidget::item:selected { background: #262c37; color: #ffffff; }
QHeaderView::section { background: transparent; color: #98a1b2; border: none;
                       padding: 6px 8px; font-weight: 600; }
QTableCornerButton::section { background: transparent; border: none; }

QComboBox { background: #242932; border: 1px solid #333a46; border-radius: 8px;
            padding: 6px 10px; min-width: 64px; }
QComboBox::drop-down { border: none; width: 20px; }
QComboBox QAbstractItemView { background: #1b1e25; border: 1px solid #333a46;
                              selection-background-color: #2f3742; }

QCheckBox { spacing: 8px; color: #e7e9ef; }

QListWidget { background: #15181e; border: 1px solid #242932; border-radius: 10px; padding: 4px; }
QListWidget::item { padding: 5px 8px; border-radius: 6px; }
QListWidget::item:selected { background: #2b3340; }
QListWidget::item:hover { background: #20252e; }

QScrollArea { border: none; background: transparent; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: #333a46; border-radius: 5px; min-height: 24px; }
QScrollBar::handle:vertical:hover { background: #414a59; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: #333a46; border-radius: 5px; min-width: 24px; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }

QToolTip { background: #242932; color: #e7e9ef; border: 1px solid #333a46; padding: 4px; }
"""


def apply_theme(app: QApplication) -> None:
    app.setStyle("Fusion")
    palette = QPalette()
    palette.setColor(QPalette.ColorRole.Window, QColor("#12141a"))
    palette.setColor(QPalette.ColorRole.WindowText, QColor("#e7e9ef"))
    palette.setColor(QPalette.ColorRole.Base, QColor("#0d0f13"))
    palette.setColor(QPalette.ColorRole.AlternateBase, QColor("#1b1e25"))
    palette.setColor(QPalette.ColorRole.Text, QColor("#e7e9ef"))
    palette.setColor(QPalette.ColorRole.Button, QColor("#242932"))
    palette.setColor(QPalette.ColorRole.ButtonText, QColor("#e7e9ef"))
    palette.setColor(QPalette.ColorRole.Highlight, QColor(ACCENT))
    palette.setColor(QPalette.ColorRole.HighlightedText, QColor("#ffffff"))
    palette.setColor(QPalette.ColorRole.ToolTipBase, QColor("#242932"))
    palette.setColor(QPalette.ColorRole.ToolTipText, QColor("#e7e9ef"))
    palette.setColor(QPalette.ColorRole.PlaceholderText, QColor(MUTED))
    palette.setColor(QPalette.ColorRole.Link, QColor("#8fb4ff"))
    app.setPalette(palette)

    font = QFont()
    font.setPointSize(13 if sys.platform == "darwin" else 10)
    app.setFont(font)
    app.setStyleSheet(STYLESHEET)


def mono_font(size: int = 11) -> QFont:
    font = QFont()
    font.setFamilies(list(_MONO))
    font.setStyleHint(QFont.StyleHint.Monospace)
    font.setPointSize(size)
    return font
