#!/usr/bin/env python3
"""Refuse to publish anything that came off the disc.

    tools/check_publish.py

Lists exactly the files `git add .` would publish and fails if any of them is derived
from the game: the extracted tree, a disc image in any format, the build or dtk output,
the memory card. It works with or without a repository -- with none it points an empty
throwaway repository at this work tree, so .gitignore decides either way -- and it lists
tracked files as well as untracked ones, because .gitignore does not un-track a file that
was added before the rule existed.

Two extra rules catch an asset copied somewhere the path rules do not name: a published
file over 2 MiB is refused, and so is one with NUL bytes in it, since nothing here is
either. Publishable artwork is the launcher's own, so images are exempt.

`naruto/analysis/symbols.txt` is meant to be published: it is addresses, sizes and names,
not the game's contents.
"""
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(HERE)

DENIED_DIRS = (
    'naruto/rom/',
    'naruto/build/',
    'naruto/saves/',
    'config/GNREDA/',
    'launcher/.venv/',
    'launcher/build/',
    'launcher/dist/',
    'orig/',
    'build/',
)
DENIED_EXT = frozenset(
    'iso ciso rvz wia gcz wbfs tgc nfs h4m trk bnr dol img'.split())
BINARY_OK_EXT = frozenset('png jpg jpeg gif ico icns svg pdf'.split())
MAX_BYTES = 2 << 20
DOL_SHA1 = '19251f7f15f8b6f7e0cd1c753409151ec49d9af2'


def git(*args, cwd=PROJECT, env=None):
    result = subprocess.run(('git',) + args, cwd=cwd, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode != 0:
        sys.exit('error: git %s failed: %s' % (
            ' '.join(args), result.stderr.decode('utf-8', 'replace').strip()))
    return result.stdout


def publish_set():
    """Paths `git add .` would publish, relative to the project root."""
    if os.path.isdir(os.path.join(PROJECT, '.git')):
        listing = git('ls-files', '-z', '-c', '-o', '--exclude-standard')
        return [p for p in listing.decode('utf-8', 'replace').split('\0') if p]

    # No repository yet: let git apply the same .gitignore rules through a throwaway one.
    tmp = tempfile.mkdtemp(prefix='check-publish-')
    try:
        env = dict(os.environ,
                   GIT_DIR=os.path.join(tmp, '.git'),
                   GIT_WORK_TREE=PROJECT)
        subprocess.run(('git', 'init', '-q'), cwd=tmp, check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        listing = git('ls-files', '-z', '-o', '--exclude-standard', cwd=tmp, env=env)
        return [p for p in listing.decode('utf-8', 'replace').split('\0') if p]
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def problem(path):
    """Why `path` may not be published, or None."""
    if any(path.startswith(d) for d in DENIED_DIRS):
        return 'derived from the disc'
    ext = path.rsplit('.', 1)[-1].lower() if '.' in os.path.basename(path) else ''
    if ext in DENIED_EXT:
        return 'a disc file (.%s)' % ext

    full = os.path.join(PROJECT, path)
    try:
        size = os.path.getsize(full)
    except OSError as e:
        return 'unreadable (%s)' % e.strerror
    if size > MAX_BYTES:
        return '%.1f MiB, larger than anything the project publishes' % (size / 1048576.0)

    with open(full, 'rb') as f:
        head = f.read(8192)
        digest = hashlib.sha1(head)
        while True:
            chunk = f.read(1 << 20)
            if not chunk:
                break
            digest.update(chunk)
    if digest.hexdigest() == DOL_SHA1:
        return 'the game\'s main.dol'
    if b'\0' in head and ext not in BINARY_OK_EXT:
        return 'binary, and not an image'
    return None


def main():
    paths = sorted(publish_set())
    bad = []
    total = 0
    for path in paths:
        total += os.path.getsize(os.path.join(PROJECT, path)) if os.path.isfile(
            os.path.join(PROJECT, path)) else 0
        reason = problem(path)
        if reason:
            bad.append((path, reason))

    print('publish set: %d files, %.1f MiB' % (len(paths), total / 1048576.0))
    if not bad:
        print('ok: nothing derived from the disc is in it')
        return 0

    print('\nerror: %d file(s) derived from the disc would be published:'
          % len(bad), file=sys.stderr)
    for path, reason in bad:
        print('  %-52s %s' % (path, reason), file=sys.stderr)
    print('\nremove them, or `git rm --cached` what is already tracked, then re-run.',
          file=sys.stderr)
    return 1


if __name__ == '__main__':
    sys.exit(main())
