"""The instruction words a mod's Gecko codes write into the game's code.

A Gecko code that patches an instruction -- `0400CB14 60000000` and the like -- cannot
work as a write while the game runs: the recompiler already turned the disc's instructions
into this machine's own, and the guest's copy of them is never executed. So the words are
laid over the DOL here, before it is translated, and the recompiled code is built from the
patched instruction. The launcher writes them into `mods.code`, one `ADDRESS WORD` pair per
line, next to the extracted disc.

Only words inside a text section are applied. A code also writes data -- tables, flags,
counters -- and those writes are applied to guest memory by the runtime each frame, as on a
console; here they are simply not code, so they are counted and dropped.

Run this file directly to check its arithmetic: `python3 modspatch.py`.
"""

import struct
import sys


def load(path):
    """The words in a `mods.code` file: address -> instruction word."""
    words = {}
    try:
        text = open(path, 'r', encoding='utf-8')
    except OSError as error:
        print(f'warning: mods codes at {path} could not be read: {error}', file=sys.stderr)
        return words
    with text:
        for number, line in enumerate(text, 1):
            line = line.split('#', 1)[0].strip()
            if not line:
                continue
            parts = line.split()
            if len(parts) != 2:
                print(f'warning: {path}:{number} is not "ADDRESS WORD": {line}',
                      file=sys.stderr)
                continue
            try:
                address, word = int(parts[0], 16), int(parts[1], 16)
            except ValueError:
                print(f'warning: {path}:{number} is not two hex words: {line}',
                      file=sys.stderr)
                continue
            words[address] = word
    return words


def apply(dol, words):
    """Patches `dol` in place. Returns how many words landed, and why the rest did not.

    `dol` is a dol.Dol whose `data` may be written to: it has to be a bytearray, which
    callers make it when they have words to apply.
    """
    applied = misplaced = data = 0
    for address, word in sorted(words.items()):
        section = None
        for start, size, offset, is_text in dol.sections:
            if start <= address < start + size:
                section = (start, size, offset, is_text)
                break
        if section is None:
            data += 1
            continue
        start, size, offset, is_text = section
        if not is_text:
            data += 1
            continue
        if address & 3 or address + 4 > start + size:
            misplaced += 1
            continue
        at = offset + address - start
        dol.data[at:at + 4] = struct.pack('>I', word & 0xFFFFFFFF)
        applied += 1
    return applied, data, misplaced


def report(path, applied, data, misplaced):
    print(f'mods: {applied} instruction word(s) from {path} applied to the recompiled code',
          file=sys.stderr)
    if data:
        print(f'mods: {data} word(s) from {path} write to the game\'s data, not its code; '
              f'the runtime applies them', file=sys.stderr)
    if misplaced:
        print(f'mods: {misplaced} word(s) from {path} are not whole instructions inside a '
              f'text section; they are left alone', file=sys.stderr)


# ---- checks, run with `python3 modspatch.py` ----

class _Fake:
    def __init__(self):
        self.sections = [(0x80003100, 0x40, 0x100, True), (0x80300000, 0x20, 0x200, False)]
        self.data = bytearray(b'\x11' * 0x300)


def _check(condition, what):
    print(('ok   ' if condition else 'FAIL ') + what)
    if not condition:
        raise SystemExit(1)


def _self_test():
    dol = _Fake()
    applied, data, misplaced = apply(dol, {
        0x80003104: 0x60000000,   # inside text
        0x80300000: 0x12345678,   # data: the runtime's business
        0x80003102: 0x60000000,   # not a whole instruction
        0x81700000: 0x60000000,   # not in the DOL at all
    })
    _check(applied == 1 and data == 2 and misplaced == 1, 'only whole instructions in text land')
    _check(dol.data[0x104:0x108] == b'\x60\x00\x00\x00', 'the instruction is patched')
    _check(dol.data[0x100:0x104] == b'\x11' * 4, 'the instruction beside it is not')

    import os
    import tempfile
    handle, path = tempfile.mkstemp(suffix='.code')
    try:
        with os.fdopen(handle, 'w') as f:
            f.write('# a comment\n8000CB14 60000000\n\n8000CB1860000000\n')
        words = load(path)
    finally:
        os.remove(path)
    _check(words == {0x8000CB14: 0x60000000}, 'a codes file is read, and a bad line skipped')


if __name__ == '__main__':
    _self_test()
