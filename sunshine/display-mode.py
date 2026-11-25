#!/usr/bin/env python3
"""Optional prep/undo: match an existing display mode, never invent a timing."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

STATE = Path.home() / ".local/state/sunshine/display.json"
OUTPUT = "DP-3"


def doctor(*args):
    result = subprocess.run(["kscreen-doctor", *args], capture_output=True,
                            text=True, timeout=3, check=True)
    return result.stdout


def outputs():
    return json.loads(doctor("-j"))["outputs"]


def current_mode(output):
    return next(m for m in output["modes"] if m["id"] == output["currentModeId"])


def undo():
    if not STATE.exists():
        return
    saved = json.loads(STATE.read_text())
    live = {o["name"]: o for o in outputs() if o["connected"]}
    original = saved["mode"]
    output = live[saved["output"]]
    mode = next(m for m in output["modes"] if m["size"] == original["size"]
                and abs(m["refreshRate"] - original["refreshRate"]) < 0.01)
    args = [f"output.{output['name']}.mode.{mode['id']}"]
    for name, position in saved["positions"].items():
        if name not in live:
            raise RuntimeError("A monitor disconnected; saved layout retained for recovery")
        args.append(f"output.{name}.position.{position['x']},{position['y']}")
    doctor(*args)
    restored = {o["name"]: o for o in outputs()}
    if (current_mode(restored[output["name"]])["size"] != original["size"]
            or abs(current_mode(restored[output["name"]])["refreshRate"]
                   - original["refreshRate"]) > 0.01
            or any(restored[name]["pos"] != pos for name, pos in saved["positions"].items())):
        raise RuntimeError("Restore was rejected; saved layout retained for recovery")
    STATE.unlink()
    print("Original display mode and positions restored.")


def prepare(plan):
    # The greeter belongs to sddm, not joaquin. Keep its existing mode.
    socket = Path(os.environ.get("XDG_RUNTIME_DIR", "/run/user/1000")) / os.environ.get("WAYLAND_DISPLAY", "wayland-0")
    if not socket.exists():
        print("No user Wayland session; keeping the greeter display mode.")
        return
    width = int(os.environ["SUNSHINE_CLIENT_WIDTH"])
    height = int(os.environ["SUNSHINE_CLIENT_HEIGHT"])
    fps = float(os.environ["SUNSHINE_CLIENT_FPS"])
    live = outputs()
    output = next(o for o in live if o["name"] == OUTPUT and o["enabled"])
    modes = [m for m in output["modes"] if m["size"] == {"width": width, "height": height}]
    if not modes:
        print(f"No existing {width}x{height} mode on {OUTPUT}; keeping the monitor unchanged.")
        return
    mode = min(modes, key=lambda m: abs(m["refreshRate"] - fps))
    print(f"Selected existing mode: {OUTPUT} {mode['name']}")
    if plan or mode["id"] == output["currentModeId"]:
        return
    if STATE.exists():
        raise RuntimeError("Saved layout already exists; run undo before another prepare")
    STATE.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    with STATE.open("x") as stream:
        os.chmod(STATE, 0o600)
        json.dump({"output": OUTPUT, "mode": current_mode(output),
                   "positions": {o["name"]: o["pos"] for o in live if o["enabled"]}}, stream)
    try:
        doctor(f"output.{OUTPUT}.mode.{mode['id']}")
        actual = current_mode(next(o for o in outputs() if o["name"] == OUTPUT))
        # kscreen-doctor can return exit code zero even when KWin rejects a mode.
        if actual["id"] != mode["id"]:
            raise RuntimeError("KWin rejected the requested mode")
    except Exception:
        undo()
        raise


def main():
    os.umask(0o077)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["plan", "prepare", "undo"])
    args = parser.parse_args()
    if args.action == "undo":
        undo()
    else:
        prepare(args.action == "plan")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"Display mode: {error}", file=sys.stderr)
        sys.exit(1)
