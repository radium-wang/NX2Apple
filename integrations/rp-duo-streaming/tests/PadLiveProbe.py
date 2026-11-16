"""Verify live Duo controller forwarding with the game closed."""
import argparse
import fcntl
import os
from pathlib import Path
import socket
import statistics
import struct
import subprocess
import time

parser = argparse.ArgumentParser()
parser.add_argument("serial")
parser.add_argument("--adb", default="/opt/homebrew/bin/adb")
args = parser.parse_args()
root = Path(f"/private/tmp/rpduo-stream-{os.getuid()}")
path = root / "pad.sock"
frame = struct.Struct("<IHBBhhhh")
adb = [args.adb, "-s", args.serial, "shell"]
device = "/dev/input/event8"


def send(events):
    commands = [f"sendevent {device} {kind} {code} {value}" for kind, code, value in events]
    commands.append(f"sendevent {device} 0 0 0")
    return subprocess.Popen([*adb, "; ".join(commands)], stdout=subprocess.DEVNULL)


def await_state(sock, match, timeout=1.5):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        sock.settimeout(max(.001, deadline - time.monotonic()))
        try:
            data = sock.recv(frame.size + 1)
        except TimeoutError:
            return None
        if len(data) == frame.size:
            magic, *state = frame.unpack(data)
            if magic == 0x31445052 and match(state):
                return state, time.monotonic_ns()
    return None


cases = [
    ("D-pad up", 3, 17, -1, 0, 0x1),
    ("D-pad down", 3, 17, 1, 0, 0x2),
    ("D-pad left", 3, 16, -1, 0, 0x4),
    ("D-pad right", 3, 16, 1, 0, 0x8),
    ("bottom / Switch B", 1, 304, 1, 0, 0x1000),
    ("right / Switch A", 1, 305, 1, 0, 0x2000),
    ("left / Switch Y", 1, 307, 1, 0, 0x4000),
    ("top / Switch X", 1, 308, 1, 0, 0x8000),
    ("L shoulder", 1, 310, 1, 0, 0x100),
    ("R shoulder", 1, 311, 1, 0, 0x200),
    ("left stick right", 3, 0, 23000, 3, 23000),
    ("left stick up", 3, 1, -23000, 4, 23000),
    ("right stick right", 3, 2, 23000, 5, 23000),
    ("right stick up", 3, 5, -23000, 6, 23000),
    ("left trigger", 3, 10, 20000, 1, 155),
    ("right trigger", 3, 9, 20000, 2, 155),
]
failures = 0
times = []
with (root / "pad.sock.lock").open("a") as lock:
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    if path.exists():
        if not path.is_socket():
            raise RuntimeError("pad.sock is not a socket")
        path.unlink()
    with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as sock:
        sock.bind(str(path))
        path.chmod(0o600)
        try:
            for name, kind, code, value, index, expected in cases:
                tolerance = 0 if index == 0 else 2500 if index >= 3 else 7
                start = time.monotonic_ns()
                pressed = send([(kind, code, value)])
                try:
                    got = await_state(sock, lambda s: abs(s[index] - expected) <= tolerance)
                    pressed.wait(timeout=3)
                finally:
                    released = send([(kind, code, 0)])
                    neutral = await_state(sock, lambda s: not any(s))
                    released.wait(timeout=3)
                if got and neutral:
                    elapsed = (got[1] - start) / 1_000_000
                    times.append(elapsed)
                    print(f"PASS {name}: {got[0][index]}, released; {elapsed:.1f} ms including ADB")
                else:
                    failures += 1
                    print(f"FAIL {name}: pressed={got}, neutral={neutral}")
        finally:
            path.unlink()
if times:
    print(f"{len(times)}/{len(cases)} passed; ADB + stream median {statistics.median(times):.1f} ms, min {min(times):.1f}, max {max(times):.1f}")
raise SystemExit(bool(failures))
