"""Measure test-page response through streaming to Android frame observation."""
import argparse
import statistics
import subprocess
import time

parser = argparse.ArgumentParser()
parser.add_argument("serial")
args = parser.parse_args()
adb = ["/opt/homebrew/bin/adb", "-s", args.serial, "shell", "-T"]
record = (
    "screenrecord --output-format=frames --display-id 4630947039571902866 "
    "--size 160x90 - | /data/local/tmp/rpduo-screen-frames"
)
frames = subprocess.Popen([*adb, record], stdout=subprocess.PIPE, text=True)
pad = subprocess.Popen([*adb, "/data/local/tmp/rpduo-input-time"],
                       stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)


def event(command):
    pad.stdin.write(command + "\n")
    pad.stdin.flush()
    return int(pad.stdout.readline())


def wait_color(expected):
    while True:
        line = frames.stdout.readline()
        if not line:
            raise RuntimeError("Frame capture stopped")
        color, timestamp = map(int, line.split())
        if color == expected:
            return timestamp


samples = []
try:
    event("u")
    wait_color(0)
    for i in range(16):
        time.sleep(.10 + .013 * (i % 5))
        pressed = event("d")
        first_red = wait_color(1)
        samples.append((first_red - pressed) / 1e6)
        event("u")
        wait_color(0)
    print(f"samples={len(samples)} median={statistics.median(samples):.1f}ms "
          f"min={min(samples):.1f}ms max={max(samples):.1f}ms")
    print("Android frame observation samples:", [round(t, 1) for t in samples])
finally:
    pad.stdin.close()
    pad.wait(timeout=3)
    frames.terminate()
    frames.wait(timeout=3)
