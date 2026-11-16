import argparse, queue, statistics, subprocess, threading, time

parser = argparse.ArgumentParser()
parser.add_argument("serial")
parser.add_argument("observer")
args = parser.parse_args()


received = queue.Queue()
monitor = subprocess.Popen([args.observer, "30", "--timing"], stdout=subprocess.PIPE, text=True)
def read():
    for line in monitor.stdout:
        fields = line.split()
        if len(fields) == 3 and fields[0] == "edge":
            received.put((int(fields[1]),int(fields[2])))
threading.Thread(target=read, daemon=True).start()
proc = subprocess.Popen(["/opt/homebrew/bin/adb", "-s", args.serial, "shell", "-T",
                         "/data/local/tmp/rpduo-input-time"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
def command(cmd):
    start = time.monotonic_ns()
    proc.stdin.write(cmd + b"\n")
    proc.stdin.flush()
    stamp = int(proc.stdout.readline())
    return start, stamp, time.monotonic_ns()

def edge(mask):
    while True:
        value, t = received.get(timeout=2)
        if value == mask: return t

try:
    offsets = [(before-dev, after-dev) for before,dev,after in (command(b"p") for _ in range(60))]
    low, high = max(x[0] for x in offsets), min(x[1] for x in offsets)
    if low > high: raise RuntimeError("clock alignment intervals disagree")
    command(b"u")
    delays=[]
    for i in range(32):
        while not received.empty(): received.get_nowait()
        _, dev, _=command(b"d")
        delays.append(edge(2)-dev)
        command(b"u")
        edge(0)
        time.sleep(.10 + (i%5)*.011)
    midpoint=(low+high)/2
    ms=sorted((sample-midpoint)/1e6 for sample in delays)
    print(f"samples={len(ms)} min={ms[0]:.2f}ms median={statistics.median(ms):.2f}ms p95={ms[30]:.2f}ms max={ms[-1]:.2f}ms offset_uncertainty=±{(high-low)/2e6:.2f}ms")

finally:
    proc.stdin.close()
    proc.wait(timeout=3)
    monitor.terminate()
    monitor.wait(timeout=3)
