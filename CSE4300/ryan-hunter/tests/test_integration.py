#!/usr/bin/env python3
"""Linux integration tests. Only signals child processes created by these tests."""
import errno
import fcntl
import os
from pathlib import Path
import pty
import re
import select
import signal
import struct
import subprocess
import sys
import termios
import time

ROOT = Path(__file__).resolve().parent.parent
os.chdir(ROOT)
passed = 0

def check(label, condition):
    global passed
    if not condition:
        raise AssertionError(label)
    passed += 1
    print('PASS ' + label, flush=True)

def run(*args):
    return subprocess.run(['./minitask', *args], capture_output=True, text=True, timeout=15)

def read_until(fd, needle, timeout=6):
    end = time.monotonic() + timeout
    data = b''
    while time.monotonic() < end:
        ready, _, _ = select.select([fd], [], [], 0.15)
        if ready:
            try:
                chunk = os.read(fd, 65536)
            except OSError as error:
                if error.errno == errno.EIO:
                    break
                raise
            data += chunk
            if needle in data:
                return data
    raise AssertionError('PTY did not show ' + repr(needle) + '\n' + data[-2000:].decode(errors='replace'))

def interactive_tests():
    worker = subprocess.Popen(['./tests/demo_worker', 'sleep'], stdout=subprocess.PIPE, text=True)
    worker.stdout.readline()
    pid, fd = pty.fork()
    if pid == 0:
        os.execv('./minitask', ['./minitask', '--interval', '0.2', '--limit', '12'])
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', 36, 120, 0, 0))
    reaped = False
    try:
        read_until(fd, b'MINI TASK MANAGER')
        check('Live terminal starts', True)
        def command(text, expected):
            os.write(fd, (text + '\n').encode())
            return read_until(fd, expected.encode())
        command('sort name', 'Sorted by Name.')
        check('Interactive sort command', True)
        command('search mt-demo-sleep', 'Filter: mt-demo-sleep')
        check('Interactive name filter', True)
        command('search no-such-minitask-process', 'No matching processes.')
        check('Interactive no-match display', True)
        command('clear', 'Filter: all')
        command('pause', 'Automatic refresh paused.')
        check('Pause command acknowledged', True)
        command('refresh', 'Refreshed.')
        command('kill 1', 'Refused:')
        check('Protected PID rejected by interface', True)
        command(f'kill {worker.pid}', "Type 'yes")
        check('Confirmation does not signal yet', worker.poll() is None)
        command('no', 'Cancelled. No signal sent.')
        check('Cancellation leaves worker running', worker.poll() is None)
        command(f'kill {worker.pid}', "Type 'yes")
        command(f'yes {worker.pid}', 'SIGTERM sent')
        check('Confirmed UI termination exits worker', worker.wait(timeout=5) == 0)
        os.write(fd, b'quit\n')
        end=time.monotonic()+5
        while time.monotonic()<end:
            done,status=os.waitpid(pid,os.WNOHANG)
            if done:
                reaped=True
                check('Interactive quit exits cleanly', os.waitstatus_to_exitcode(status)==0)
                break
            time.sleep(.05)
        check('Interactive process reaped', reaped)
        attrs=termios.tcgetattr(fd)
        check('Terminal echo and canonical input restored', bool(attrs[3]&termios.ECHO) and bool(attrs[3]&termios.ICANON))
    finally:
        if not reaped:
            try: os.kill(pid,signal.SIGTERM)
            except ProcessLookupError: pass
            try: os.waitpid(pid,0)
            except ChildProcessError: pass
        os.close(fd)
        if worker.poll() is None:
            worker.terminate()
            worker.wait(timeout=5)

def soak():
    start=time.monotonic()
    result=subprocess.run(['./minitask','--batch','--iterations','180','--interval','1','--limit','5'],
                          capture_output=True,text=True,timeout=220)
    duration=time.monotonic()-start
    snapshots=result.stdout.count('MINI TASK MANAGER')
    check('Three-minute live refresh run', result.returncode==0 and snapshots==180 and duration>=180)
    print(f'MEASURE soak duration={duration:.2f}s snapshots={snapshots}',flush=True)
    Path('results/soak_last_snapshot.txt').write_text(result.stdout[result.stdout.rfind('MINI TASK MANAGER'):])

if '--soak' in sys.argv:
    soak()
else:
    check('Help command', run('--help').returncode==0)
    for args in [('--interval','0'),('--interval','nan'),('--limit','-1'),('--sort','invalid'),('--iterations','0')]:
        check('Invalid CLI arguments '+str(args),run(*args).returncode==2)
    output=run('--batch','--interval','0.2','--iterations','2','--sort','pid','--limit','5')
    check('Two sampled plain snapshots', output.returncode==0 and output.stdout.count('MINI TASK MANAGER')==2)
    check('Batch has no terminal escapes', '\x1b' not in output.stdout)
    missing=run('--batch','--interval','0.2','--search','no-such-minitask-process')
    check('Batch no-match result',missing.returncode==0 and 'No matching processes.' in missing.stdout)
    interactive_tests()
print(f'RESULT {passed} integration checks passed',flush=True)
