#!/usr/bin/env python3
"""Drives the emulator started by start.sh: knob input and screenshots over the script console.

    p404.py shot NAME        saves build/mini404/shots/NAME.png
    p404.py twist N          turns the knob N steps; positive moves up a menu
    p404.py push             presses the knob
    p404.py serial [SECONDS] prints the firmware log from the USB serial port
    p404.py seq 'twist -2' 'wait 5' 'push' 'shot x'

The firmware runs several times slower than real time and reads the knob in its own time. Steps
sent close together can merge or land late, so twist sends them one at a time, 1.5 s apart, and it
is still worth checking the focus with a screenshot before pressing.
"""
import os
import socket
import subprocess
import sys
import time
from pathlib import Path

RUN = Path(
    os.environ.get('MINI404_RUN',
                   Path(__file__).resolve().parents[2] / 'build' / 'mini404'))
SCRIPT_PORT = 7070
SERIAL_PORT = 7071
CONTAINER = 'mini404-coreone'


def finished_count():
    logs = subprocess.run(['docker', 'logs', CONTAINER],
                          capture_output=True,
                          text=True)
    return (logs.stdout + logs.stderr).count('ScriptHost: Script FINISHED')


def script(*commands, delay=0.1):
    """Runs the commands one at a time. The console takes one line at a time and garbles lines
    sent while one is still running, so each waits for its "Script FINISHED"."""
    with socket.create_connection(('localhost', SCRIPT_PORT), timeout=5) as s:
        s.settimeout(1.0)
        try:
            s.recv(4096)  # the console prints one line on connect
        except socket.timeout:
            pass
        for command in commands:
            before = finished_count()
            s.sendall(command.encode() + b'\n')
            deadline = time.time() + 10
            while finished_count() <= before:
                if time.time() > deadline:
                    sys.exit(f'no "Script FINISHED" for {command}')
                time.sleep(0.1)
            time.sleep(delay)


def shot(name):
    path = RUN / 'shots' / f'{name}.png'
    path.unlink(missing_ok=True)
    script(f'generic-spi-display::Screenshot(/work/shots/{name}.png)',
           delay=0.2)
    for _ in range(50):
        if path.exists() and path.stat().st_size > 0:
            time.sleep(0.2)
            print(path)
            return
        time.sleep(0.2)
    sys.exit(f'screenshot {name} not written')


def serial(seconds):
    """The emulated USB serial port only carries output: input never reaches the firmware."""
    with socket.create_connection(('localhost', SERIAL_PORT), timeout=5) as s:
        s.settimeout(0.3)
        out = b''
        end = time.time() + seconds
        while time.time() < end:
            try:
                out += s.recv(65536)
            except socket.timeout:
                pass
    print(out.decode(errors='replace'), end='')


def main(argv):
    action, *args = argv
    if action == 'shot':
        shot(args[0])
    elif action == 'twist':
        steps = int(args[0])
        direction = 1 if steps > 0 else -1
        for _ in range(abs(steps)):
            script(f'encoder-input::Twist({direction})', delay=1.5)
    elif action == 'push':
        script('encoder-input::Push()', delay=0.5)
    elif action == 'serial':
        serial(float(args[0]) if args else 5.0)
    elif action == 'cmd':
        script(args[0])
    elif action == 'seq':
        for step in args:
            verb, *rest = step.split()
            if verb == 'wait':
                time.sleep(float(rest[0]))
            else:
                main([verb, *rest])
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main(sys.argv[1:])
