"""Subprocess plumbing: a streaming child, a command queue, and the game itself."""
from __future__ import annotations

import codecs
import os
import shlex
import signal
from dataclasses import dataclass
from pathlib import Path

from PySide6.QtCore import QObject, QProcess, QProcessEnvironment, QTimer, Signal

from .project import search_path


def child_environment() -> QProcessEnvironment:
    """The app's environment, with the PATH cmake/ninja/nodtool are actually on."""
    env = QProcessEnvironment.systemEnvironment()
    env.insert("PATH", search_path())
    env.insert("PYTHONUNBUFFERED", "1")
    return env


def format_command(argv: list[str] | tuple[str, ...]) -> str:
    return " ".join(shlex.quote(str(part)) for part in argv)


def _kill_if_running(proc: QProcess) -> None:
    try:
        if proc.state() != QProcess.ProcessState.NotRunning:
            proc.kill()
    except RuntimeError:
        pass


class StreamingProcess(QObject):
    """One child process whose merged output is emitted as text as it arrives."""

    output = Signal(str)
    finished = Signal(int)

    def __init__(self, parent: QObject | None = None):
        super().__init__(parent)
        self._proc: QProcess | None = None
        self._decoder = codecs.getincrementaldecoder("utf-8")("replace")
        self._reported = False

    @property
    def running(self) -> bool:
        return self._proc is not None and self._proc.state() != QProcess.ProcessState.NotRunning

    @property
    def pid(self) -> int:
        return self._proc.processId() if self._proc is not None else 0

    def start(self, argv: list[str] | tuple[str, ...], cwd: Path | None = None) -> None:
        self.stop()
        self._decoder.reset()
        self._reported = False
        proc = QProcess(self)
        proc.setProcessEnvironment(child_environment())
        proc.setProcessChannelMode(QProcess.ProcessChannelMode.MergedChannels)
        if cwd is not None:
            proc.setWorkingDirectory(str(cwd))
        proc.setProgram(str(argv[0]))
        proc.setArguments([str(a) for a in argv[1:]])
        proc.readyReadStandardOutput.connect(self._drain)
        proc.finished.connect(self._done)
        proc.errorOccurred.connect(self._error)
        self._proc = proc
        proc.start()

    def stop(self, grace_ms: int = 1500) -> None:
        proc = self._proc
        if proc is None or proc.state() == QProcess.ProcessState.NotRunning:
            return
        proc.terminate()
        QTimer.singleShot(grace_ms, lambda p=proc: _kill_if_running(p))

    def kill(self) -> None:
        """Stop now, waiting for the process to go, so a closing window leaves no child."""
        proc = self._proc
        if proc is None or proc.state() == QProcess.ProcessState.NotRunning:
            return
        proc.kill()
        proc.waitForFinished(1000)

    def wait(self, ms: int) -> bool:
        proc = self._proc
        if proc is None:
            return True
        return proc.waitForFinished(ms)

    def _drain(self) -> None:
        proc = self._proc
        if proc is None:
            return
        text = self._decoder.decode(bytes(proc.readAllStandardOutput()))
        if text:
            self.output.emit(text)

    def _done(self, code: int, _status: QProcess.ExitStatus) -> None:
        self._drain()
        tail = self._decoder.decode(b"", final=True)
        if tail:
            self.output.emit(tail)
        if self._reported:
            return
        self._reported = True
        self.finished.emit(code)

    def _error(self, error: QProcess.ProcessError) -> None:
        if error == QProcess.ProcessError.FailedToStart and not self._reported:
            self._reported = True
            program = self._proc.program() if self._proc else "?"
            self.output.emit(f"[launcher] could not start {program}\n")
            self.finished.emit(127)


@dataclass(frozen=True)
class Step:
    argv: tuple[str, ...]
    cwd: Path
    label: str


class CommandQueue(QObject):
    """Runs steps in order; stops at the first failure. Emits the whole transcript."""

    output = Signal(str)
    progress = Signal(str)
    finished = Signal(int)

    def __init__(self, parent: QObject | None = None):
        super().__init__(parent)
        self._runner = StreamingProcess(self)
        self._runner.output.connect(self.output)
        self._runner.finished.connect(self._step_done)
        self._steps: list[Step] = []
        self._busy = False

    @property
    def busy(self) -> bool:
        return self._busy

    def run(self, steps: list[Step]) -> None:
        if self._busy:
            return
        self._steps = list(steps)
        self._busy = True
        self._start_next()

    def stop(self) -> None:
        self._steps.clear()
        self._runner.kill()
        if self._busy:
            self._busy = False
            self.progress.emit("")
            self.finished.emit(130)

    def _start_next(self) -> None:
        if not self._steps:
            self._finish(0)
            return
        step = self._steps.pop(0)
        self.progress.emit(step.label)
        self.output.emit(f"\n$ {format_command(step.argv)}\n")
        self._runner.start(step.argv, step.cwd)

    def _step_done(self, code: int) -> None:
        if not self._busy:
            return
        if code != 0:
            self.output.emit(f"[launcher] stopped: exit status {code}\n")
            self._finish(code)
            return
        self._start_next()

    def _finish(self, code: int) -> None:
        self._busy = False
        self.progress.emit("")
        self.finished.emit(code)


class GameProcess(QObject):
    """The recompiled game, running in a window of its own."""

    output = Signal(str)
    started = Signal()
    finished = Signal(int, bool)   # exit code, stopped from the launcher

    def __init__(self, parent: QObject | None = None):
        super().__init__(parent)
        self._runner = StreamingProcess(self)
        self._runner.output.connect(self.output)
        self._runner.finished.connect(self._done)
        self._user_stopped = False

    @property
    def running(self) -> bool:
        return self._runner.running

    def start(self, argv: list[str], cwd: Path) -> None:
        self._user_stopped = False
        self._runner.start(argv, cwd)
        self.started.emit()

    def stop(self) -> None:
        """SIGINT first -- the runtime handles it by dumping its GX FIFO -- then TERM, then KILL."""
        self._user_stopped = True
        if not self._runner.running:
            return
        if os.name != "nt":
            try:
                os.kill(self._runner.pid, signal.SIGINT)
            except OSError:
                pass
            QTimer.singleShot(1200, self._terminate)
        else:
            self._runner.stop()

    def _terminate(self) -> None:
        if self._runner.running:
            self._runner.stop(grace_ms=1200)

    def shutdown(self) -> None:
        """Called as the window closes: ask nicely, then make sure nothing is left."""
        if not self._runner.running:
            return
        if os.name != "nt":
            try:
                os.kill(self._runner.pid, signal.SIGINT)
            except OSError:
                pass
            self._runner.wait(1000)
        self._runner.kill()

    def _done(self, code: int) -> None:
        self.finished.emit(code, self._user_stopped)
