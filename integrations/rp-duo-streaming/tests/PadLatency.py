import argparse, fcntl, os, pathlib, queue, socket, statistics, struct, subprocess, threading, time

parser = argparse.ArgumentParser()
parser.add_argument("serial")
args = parser.parse_args()

root = pathlib.Path(f"/private/tmp/rpduo-stream-{os.getuid()}")
path = root / "pad.sock"
received = queue.Queue()
with (root / "pad.sock.lock").open("a") as lock:
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    if path.exists():
        if not path.is_socket(): raise RuntimeError("not a socket")
        path.unlink()
    with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as sock:
        sock.bind(str(path))
        path.chmod(0o600)
        proc = subprocess.Popen(["/opt/homebrew/bin/adb","-s",args.serial,"shell","-T",
                                 "/data/local/tmp/rpduo-input-time"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        def read():
            while True:
                try: data = sock.recv(64)
                except OSError: return
                now = time.monotonic_ns()
                if len(data) == 16 and data[:4] == b"RPD1":
                    received.put((struct.unpack_from("<H",data,4)[0],now))
        threading.Thread(target=read,daemon=True).start()

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
            neutral = command(b"u")
            edge(0)
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
            path.unlink()
