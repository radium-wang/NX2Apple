#!/usr/bin/env python3
"""Launch the NX2Apple game selected by the Mac frontend for direct streaming."""

import argparse
import ctypes
import json
import mmap
import os
import plistlib
import re
import stat
import sys
from pathlib import Path
from xml.parsers.expat import ExpatError


DEFAULT_STATE = Path.home() / ".config/sunshine/rp-duo-direct"
RELAY_SERVICE = "org.nxnative.rpduo.direct.frames"
MAX_REQUEST_BYTES = 64 * 1024
RELAY_MARKERS = (b"DM_STREAM_RELAY\0", b"DM_STREAM_AUDIO_SOCKET\0")
RAW_HAPTICS_MARKER = b"DM_RP_DUO_RAW_HAPTICS\0"

# Settings supplied by GameStart, plus the game options from settings.tsv.
# The native app launcher resolves its own data, saves, shaders and caches.
GAME_ENV = {
    "DM_DYNAMIC_RESOLUTION",
    "DM_FPS_HOLD",
    "DM_FPS_TARGET",
    "DM_HOME_QUIT_TEXT",
    "DM_KEYMAP",
    "DM_LANG",
    "DM_METAL_HUD",
    "DM_NVN_ENC_THREAD",
    "DM_OPMODE",
    "DM_PAD_MAP",
    "DM_PAD_MERGE",
    "DM_PAD_PRESS_TO_JOIN",
    "DM_PICTURE_FIT",
    "DM_PRESENT_SNAP",
    "DM_PRESENT_UPSCALE",
    "DM_PSO_MTL4",
    "DM_PSO_PREBUILD",
    "DM_RENDER_SCALE",
    "DM_ROMFS_OVERLAY",
    "DM_RUMBLE",
    "DM_RUMBLE_GAIN",
    "DM_RUMBLE_MIN",
    "DM_SAVED_CHOICES",
    "DM_SCREEN_ASPECT",
    "DM_SHOW_FPS",
    "DM_TOUCH_HAPTICS",
    "DM_UPSCALER",
}

PACK_DEFAULTS = {
    "DM_FPS_HOLD": {"on", "off"},
    "DM_FPS_TARGET": {"0", "30", "60", "120"},
    "DM_METAL_HUD": {"0", "1"},
    "DM_NVN_ENC_THREAD": {"0", "1"},
    "DM_OPMODE": {"0", "1"},
    "DM_PRESENT_LINK": {"0", "1", "2"},
    "DM_PRESENT_SNAP": {"0", "1"},
    "DM_PRESENT_UPSCALE": {"off", "metalfx"},
}


def read_request(state):
    """Read one complete selection from the private, atomic handoff file."""
    mode = state.stat()
    if not stat.S_ISDIR(mode.st_mode) or mode.st_uid != os.geteuid() or mode.st_mode & 0o077:
        raise ValueError("The direct streaming state directory must be private and owned by this user.")

    request_path = state / "selected-game.json"
    flags = os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK
    try:
        fd = os.open(request_path, flags)
    except FileNotFoundError as error:
        raise ValueError("Select a game in NX2Apple before starting direct streaming.") from error

    with os.fdopen(fd, "rb") as request_file:
        mode = os.fstat(request_file.fileno())
        if not stat.S_ISREG(mode.st_mode) or mode.st_uid != os.geteuid():
            raise ValueError("The selected game request must be a file owned by this user.")
        encoded = request_file.read(MAX_REQUEST_BYTES + 1)
    if len(encoded) > MAX_REQUEST_BYTES:
        raise ValueError("The selected game request is too large.")

    request = json.loads(encoded)
    if not isinstance(request, dict) or type(request.get("version")) is not int or request["version"] != 1:
        raise ValueError("Unsupported selected game request version.")
    app_path = request.get("appPath")
    if not isinstance(app_path, str) or "\0" in app_path or not Path(app_path).is_absolute():
        raise ValueError("The selected game must have an absolute app path.")
    environment = request.get("environment")
    if not isinstance(environment, dict) or any(
        not isinstance(value, str) or "\0" in value for value in environment.values()
    ):
        raise ValueError("The selected game environment must contain string values.")
    return Path(app_path).resolve(strict=True), environment


def find_launcher(app):
    """Validate the native game bundle and confirm its bundled host supports both relays."""
    if app.suffix.lower() != ".app" or not app.is_dir():
        raise ValueError("The selected path is not an NX2Apple game app.")

    contents = app / "Contents"
    with (contents / "Info.plist").open("rb") as plist_file:
        info = plistlib.load(plist_file)
    title_id = info.get("DMTitleID", "") if isinstance(info, dict) else ""
    if not isinstance(title_id, str) or not re.fullmatch(r"[0-9A-Fa-f]{16}", title_id):
        raise ValueError("The selected app is missing its NX2Apple title ID.")

    executable = info.get("CFBundleExecutable")
    if not isinstance(executable, str) or executable in {"", ".", "..", "dmhost"} or "/" in executable:
        raise ValueError("The selected game has an invalid bundle executable.")
    binary_dir = contents / "MacOS"
    launcher = (binary_dir / executable).resolve(strict=True)
    host = (binary_dir / "dmhost").resolve(strict=True)
    for binary in (launcher, host):
        if app not in binary.parents or not binary.is_file() or not os.access(binary, os.X_OK):
            raise ValueError("The selected game has an invalid launcher or engine executable.")

    resources = contents / "Resources/dm"
    with (resources / "launcher.json").open(encoding="utf-8") as profile_file:
        profile = json.load(profile_file)
    if (
        not isinstance(profile, dict)
        or profile.get("schema") != 1
        or str(profile.get("game", "")).upper() != title_id.upper()
        or not isinstance(profile.get("settings"), list)
        or not (resources / "main.patched.bin").is_file()
    ):
        raise ValueError("The selected game has an incomplete NX2Apple launch profile.")

    with host.open("rb") as host_file:
        if host_file.read(4).hex() not in {"cffaedfe", "feedfacf", "cafebabe", "bebafeca", "cafebabf", "bfbafeca"}:
            raise ValueError("The selected game's engine is not a macOS executable.")
        with mmap.mmap(host_file.fileno(), 0, access=mmap.ACCESS_READ) as executable_bytes:
            if not all(executable_bytes.find(marker) >= 0 for marker in RELAY_MARKERS):
                raise ValueError("This game build does not support direct video and audio. Build it with direct-streaming support first.")
            raw_haptics = executable_bytes.find(RAW_HAPTICS_MARKER) >= 0

    defaults = info.get("DMDirectStreamDefaults", {})
    if not isinstance(defaults, dict) or any(
        not isinstance(value, str) or value not in PACK_DEFAULTS.get(key, ()) for key, value in defaults.items()
    ):
        raise ValueError("The selected game has invalid direct streaming defaults.")
    return launcher, host, defaults, raw_haptics


def ensure_not_running(launcher, host):
    """Check executable paths, including games started directly outside Launch Services."""
    proc = ctypes.CDLL("/usr/lib/libproc.dylib", use_errno=True)
    proc.proc_listallpids.argtypes = [ctypes.c_void_p, ctypes.c_int]
    proc.proc_listallpids.restype = ctypes.c_int
    proc.proc_pidpath.argtypes = [ctypes.c_int, ctypes.c_void_p, ctypes.c_uint32]
    proc.proc_pidpath.restype = ctypes.c_int

    capacity = proc.proc_listallpids(None, 0) + 64
    while True:
        pids = (ctypes.c_int * capacity)()
        count = proc.proc_listallpids(pids, ctypes.sizeof(pids))
        if count <= 0:
            raise ValueError("Could not check whether this game is already running.")
        if count < capacity:
            break
        capacity *= 2

    bundle_binaries = {os.fsencode(launcher), os.fsencode(host)}
    executable = ctypes.create_string_buffer(4096)  # PROC_PIDPATHINFO_MAXSIZE on macOS.
    for pid in pids[:count]:
        if pid > 0 and proc.proc_pidpath(pid, executable, len(executable)) > 0 and executable.value in bundle_binaries:
            raise ValueError("This game is already running on the Mac. Quit it before starting direct streaming.")


def launch_environment(state, defaults, overrides, raw_haptics=False):
    """Retain launch options while assigning only the integration's own runtime values."""
    audio_socket = str(state / "game-audio.sock")
    if len(os.fsencode(audio_socket)) >= 104:  # Darwin sockaddr_un.sun_path, including its NUL.
        raise ValueError("The direct streaming audio socket path is too long.")

    environment = {key: value for key, value in os.environ.items() if not key.startswith("DM_")}
    environment.update(defaults)
    environment.update({key: value for key, value in overrides.items() if key in GAME_ENV})
    for key in ("DM_SAVE", "DM_SAVE_SYNC"):
        if key not in overrides:
            continue
        value = overrides[key]
        if not value.startswith("/") and not (key == "DM_SAVE_SYNC" and value in {"", "0", "icloud"}):
            raise ValueError(f"The selected game's {key} setting is invalid.")
        environment[key] = value
    environment.update({
        "DM_STREAM_RELAY": RELAY_SERVICE,
        "DM_STREAM_AUDIO_SOCKET": audio_socket,
        "DM_RP_DUO": "0",
        "DM_LAUNCHER": "0",
        "DM_AUDIO": "0",
    })
    if raw_haptics:
        environment["DM_RP_DUO_RAW_HAPTICS"] = "1"
    return environment


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--state-dir", type=Path, default=DEFAULT_STATE)
    args = parser.parse_args()

    try:
        state = args.state_dir.resolve()
        app, overrides = read_request(state)
        launcher, host, defaults, raw_haptics = find_launcher(app)
        environment = launch_environment(state, defaults, overrides, raw_haptics)
        ensure_not_running(launcher, host)
        os.execve(str(launcher), [str(launcher)], environment)
    except (OSError, ValueError, plistlib.InvalidFileException, ExpatError) as error:
        print(f"Direct streaming: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
