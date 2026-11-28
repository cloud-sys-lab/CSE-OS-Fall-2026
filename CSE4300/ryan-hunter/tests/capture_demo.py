#!/usr/bin/env python3
"""Record actual CLI and terminal output using only disposable child processes.

Writes evidence to results/. No screen pixels or performance values are invented.
Run after: make minitask tests/demo_worker
"""
import errno
import fcntl
import json
import os
from pathlib import Path
import pty
import re
import select
import signal
import struct
import subprocess
import termios
import time

ROOT = Path(__file__).resolve().parent.parent
os.chdir(ROOT)
OUT = ROOT / 'results'
OUT.mkdir(exist_ok=True)
workers = []
manager = None
fd = None
raw = bytearray()
checks = []

def read_until(needle=None, duration=2):
    data = bytearray()
    end = time.monotonic() + duration
    while time.monotonic() < end:
        if not select.select([fd], [], [], .1)[0]:
            continue
        try:
            chunk = os.read(fd, 65536)
        except OSError as error:
            if error.errno == errno.EIO:
                break
            raise
        if not chunk:
            break
        data.extend(chunk)
        raw.extend(chunk)
        if needle and needle.encode() in data and data.rstrip().endswith(b'>'):
            break
    if needle and needle.encode() not in data:
        raise RuntimeError('Missing demo response: ' + needle)
    return data

def save_frame(name, data):
    # The application's complete frames begin with cursor-home + erase-screen.
    text = bytes(data).decode(errors='replace').split('\x1b[H\x1b[2J')[-1]
    text = re.sub(r'\x1b\[[0-9;?]*[A-Za-z]', '', text).replace('\r', '')
    (OUT / name).write_text(text.rstrip() + '\n')

def command(text, expected, capture=None):
    os.write(fd, (text + '\n').encode())
    data = read_until(expected, 5)
    if capture:
        save_frame(capture, data)
    checks.append({'command': text, 'observed': expected})

try:
    pids = {}
    for mode in ('cpu', 'memory', 'sleep', 'stubborn'):
        child = subprocess.Popen(['./tests/demo_worker', mode], stdout=subprocess.PIPE,
                                 text=True)
        child.stdout.readline()
        workers.append(child)
        pids[mode] = child.pid
    batch = subprocess.run(['./minitask', '--batch', '--interval', '1', '--sort', 'cpu',
                            '--search', 'mt-demo', '--limit', '10'],
                           capture_output=True, text=True, check=True, timeout=10)
    (OUT / 'demo_batch.txt').write_text(batch.stdout)
    manager, fd = pty.fork()
    if manager == 0:
        os.execv('./minitask', ['./minitask', '--interval', '1', '--search', 'mt-demo'])
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', 36, 120, 0, 0))
    save_frame('demo_cpu_terminal.txt', read_until(duration=2.3))
    command('sort mem', 'Sorted by Memory.', 'demo_memory_terminal.txt')
    command(f"search {pids['sleep']}", f"Filter: {pids['sleep']}", 'demo_pid_search.txt')
    command('clear', 'Filter: all')
    command('search mt-demo', 'Filter: mt-demo')
    command(f"kill {pids['sleep']}", "Type 'yes", 'demo_confirmation_terminal.txt')
    command('no', 'Cancelled. No signal sent.', 'demo_cancel_terminal.txt')
    assert workers[2].poll() is None
    command(f"kill {pids['sleep']}", "Type 'yes")
    command(f"yes {pids['sleep']}", 'SIGTERM sent', 'demo_term_terminal.txt')
    assert workers[2].wait(timeout=5) == 0
    command(f"force {pids['stubborn']}", "Type 'yes")
    command(f"yes {pids['stubborn']}", 'SIGKILL sent', 'demo_force_terminal.txt')
    assert workers[3].wait(timeout=5) == -signal.SIGKILL
    command('refresh', 'Refreshed.', 'demo_after_terminal.txt')
    os.write(fd, b'quit\n')
    read_until(duration=.4)
    _, status = os.waitpid(manager, 0)
    manager = None
    assert os.waitstatus_to_exitcode(status) == 0
    (OUT / 'demo_session.ansi').write_bytes(raw)
    (OUT / 'demo_actions.json').write_text(json.dumps({
        'environment': 'Agent Linux sandbox; actual process IDs vary each run.',
        'workers': pids, 'actions': checks,
        'verified': ['cancel preserved sleep worker', 'confirmed SIGTERM exited sleep worker',
                     'confirmed SIGKILL exited stubborn worker', 'manager exited with status 0'],
        'capture': 'Actual pseudoterminal frames; text captures, not desktop screenshots.'
    }, indent=2) + '\n')
    print('Recorded real batch output, 8 terminal frames, and confirmed child-process outcomes.')
finally:
    if manager:
        try:
            os.kill(manager, signal.SIGTERM)
            os.waitpid(manager, 0)
        except (ProcessLookupError, ChildProcessError):
            pass
    if fd is not None:
        os.close(fd)
    for child in workers:
        if child.poll() is None:
            child.kill()
            child.wait()
