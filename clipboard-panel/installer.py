#!/usr/bin/env python3
"""Private installation with exact shortcut snapshots and reversible file changes.
No D-Bus, service, file or clipboard operations occur in --dry-run mode.
"""
import argparse
import fcntl
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

KEY = [268435542, 0, 0, 0]  # Qt Meta+V, with ALL four chords for KGlobalAccel v2.
KLIPPER = ["plasmashell", "show-on-mouse-pos", "Plasma", "Show Clipboard at Mouse Position"]
PANEL = ["clipboard-panel", "show-history", "Portapapeles", "Portapapeles"]
UNIT = "clipboard-panel.service"
POLICY = "org.local.clipboard-panel.clear-history.policy"
POLICY_DIRECTORY = Path("/usr/share/polkit-1/actions")
FILES = [".local/libexec/clipboard-panel", ".config/systemd/user/clipboard-panel.service",
         ".local/share/applications/org.local.clipboard-panel.desktop"]


def command(args, check=True):
    return subprocess.run([str(a) for a in args], check=check, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)


def atomic_json(path, value):
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n")
    temporary.chmod(0o600)
    temporary.replace(path)


def validate_keys(keys):
    if not isinstance(keys, list):
        raise ValueError("Invalid shortcut list")
    for sequence in keys:
        if (not isinstance(sequence, list) or len(sequence) != 4 or
                any(type(n) is not int or not 0 <= n <= 2147483647 for n in sequence)):
            raise ValueError("Each shortcut must have exactly four int32 chords")
    return [k for k in keys if k[0]]


class Shortcuts:
    def __init__(self, runner=command):
        self.runner = runner

    def call(self, method, signature, *args):
        result = self.runner(["busctl", "--user", "--json=short", "call", "org.kde.kglobalaccel",
                              "/kglobalaccel", "org.kde.KGlobalAccel", method, signature, *args])
        return json.loads(result.stdout) if result.stdout.strip() else None

    def read(self, action):
        reply = self.call("shortcutKeys", "as", 4, *action)
        if reply["type"] != "a(ai)":
            raise ValueError("Unexpected KGlobalAccel shortcut reply")
        return validate_keys([entry[0] for entry in reply["data"][0]])

    def set(self, action, keys):
        keys = validate_keys(keys)
        args = [4, *action, len(keys)]
        for key in keys:
            args.extend([4, *key])
        self.call("setForeignShortcutKeys", "asa(ai)", *args)
        if self.read(action) != keys:
            raise RuntimeError("KGlobalAccel did not retain the requested binding")

    def check_owner(self):
        reply = self.call("getGlobalShortcutsByKey", "i", KEY[0])
        if reply["type"] != "a(ssssssaiai)":
            raise ValueError("Unexpected KGlobalAccel owner reply")
        for owner in reply["data"][0]:
            if (owner[2], owner[0]) not in {(KLIPPER[0], KLIPPER[1]), (PANEL[0], PANEL[1])}:
                raise RuntimeError("Meta+V belongs to another action; nothing changed")

    def snapshot(self):
        return {"klipper": self.read(KLIPPER), "panel": self.read(PANEL)}

    def handover(self, saved):
        self.set(KLIPPER, [k for k in saved["klipper"] if k != KEY])
        self.set(PANEL, saved["panel"] if KEY in saved["panel"] else saved["panel"] + [KEY])

    def restore(self, saved):
        # Release Meta+V before giving it back; preserve other keys and multi-chord shortcuts.
        self.set(PANEL, [])
        self.set(KLIPPER, saved["klipper"])
        self.set(PANEL, saved["panel"])


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class KlipperConfig:
    """Edit only General/MaxClipItems, preserving other KConfig settings/comments."""
    def __init__(self, path):
        self.path = path

    def lines(self):
        if self.path.is_symlink() or (self.path.exists() and not self.path.is_file()):
            raise RuntimeError(f"Refusing non-regular Klipper config: {self.path}")
        return self.path.read_text().splitlines(keepends=True) if self.path.exists() else []

    @staticmethod
    def locate(lines):
        group, key, end, inside = None, None, len(lines), False
        for index, line in enumerate(lines):
            stripped = line.strip()
            if stripped.startswith("["):
                if inside:
                    end = index
                    break
                if re.fullmatch(r"\[General\](?:\[.*\])?", stripped):
                    if stripped != "[General]":
                        raise RuntimeError("Klipper General settings are locked or qualified")
                    group, inside = index, True
            elif inside and re.match(r"^\s*MaxClipItems(?:\[.*\])?\s*=", line):
                if key is not None or not re.match(r"^\s*MaxClipItems\s*=", line):
                    raise RuntimeError("Klipper MaxClipItems is duplicated, locked or qualified")
                key = index
        return group, key, end

    def snapshot(self):
        lines = self.lines()
        group, key, _ = self.locate(lines)
        return {"existed": self.path.exists(), "group_existed": group is not None,
                "line": lines[key] if key is not None else None}

    def write(self, original=None):
        lines = self.lines()
        group, key, end = self.locate(lines)
        replacement = "MaxClipItems=100\n" if original is None else original["line"]
        if key is not None:
            if replacement is None:
                del lines[key]
            else:
                lines[key] = replacement
        elif replacement is not None:
            if lines and not lines[-1].endswith("\n"):
                lines[-1] += "\n"
            if group is None:
                lines.extend(["[General]\n", replacement])
            else:
                lines.insert(end, replacement)
        if original is not None and not original["group_existed"]:
            group, _, end = self.locate(lines)
            if group is not None and all(not line.strip() for line in lines[group+1:end]):
                del lines[group:end]
        if original is not None and not original["existed"] and not any(line.strip() for line in lines):
            self.path.unlink(missing_ok=True)
            return
        self.path.parent.mkdir(parents=True, exist_ok=True)
        temporary = self.path.with_name(self.path.name + ".clipboard-panel-tmp")
        temporary.write_text("".join(lines))
        temporary.chmod(self.path.stat().st_mode & 0o777 if self.path.exists() else 0o600)
        temporary.replace(self.path)


class Installer:
    def __init__(self, home, source, shortcuts=None, runner=command, policy_directory=POLICY_DIRECTORY):
        self.home, self.source, self.runner = Path(home), Path(source), runner
        self.state = self.home / ".local/state/clipboard-panel"
        self.manifest = self.state / "installation.json"
        self.shortcuts = shortcuts or Shortcuts(runner)
        config_home = Path(os.environ.get("XDG_CONFIG_HOME", self.home / ".config")) if self.home == Path.home() else self.home / ".config"
        self.klipper = KlipperConfig(config_home / "klipperrc")
        self.policy = Path(policy_directory) / POLICY

    def policy_snapshot(self):
        if self.policy.is_symlink() or (self.policy.exists() and not self.policy.is_file()):
            raise RuntimeError(f"Refusing non-regular policy: {self.policy}")
        expected = digest(self.source / POLICY)
        if self.policy.exists() and digest(self.policy) != expected:
            raise RuntimeError(f"Existing polkit policy differs: {self.policy}")
        return {"owned": not self.policy.exists(), "sha256": expected}

    def check_policy(self, saved):
        info = saved.get("policy")
        if info and self.policy.exists() and (self.policy.is_symlink() or digest(self.policy) != info["sha256"]):
            raise RuntimeError(f"Managed polkit policy changed: {self.policy}")

    def privileged(self, *args):
        try:
            self.runner(["sudo", "-n", *args])
        except subprocess.CalledProcessError as error:
            raise RuntimeError("Polkit policy installation/removal requires working passwordless sudo (-n); nothing can prompt for a password here") from error

    def install_policy(self, saved):
        self.check_policy(saved)
        if not self.policy.exists():
            self.privileged("install", "-m", "0644", "--", self.source / POLICY, self.policy)

    def reload_klipper(self):
        self.runner(["busctl", "--user", "call", "org.kde.klipper", "/klipper",
                     "org.kde.klipper.klipper", "reloadConfig"])


    def service(self, *args, check=True):
        return self.runner(["systemctl", "--user", *args], check=check)

    def service_state(self):
        return {"enabled": self.service("is-enabled", UNIT, check=False).stdout.strip() == "enabled",
                "active": self.service("is-active", UNIT, check=False).stdout.strip() == "active"}

    def stop_if_running(self):
        state = self.service("is-active", UNIT, check=False).stdout.strip()
        if state in {"active", "activating", "reloading", "deactivating"}:
            self.service("stop", UNIT)

    def load(self):
        return json.loads(self.manifest.read_text()) if self.manifest.exists() else None

    def check_files(self, saved):
        self.check_policy(saved)
        for rel, expected in saved.get("managed", {}).items():
            path = self.home / rel
            if path.is_symlink() or not path.is_file() or digest(path) != expected:
                raise RuntimeError(f"Managed file changed: {path}; preserve it before continuing")

    def backup(self):
        saved = {"shortcuts": self.shortcuts.snapshot(), "service": self.service_state(), "files": {}, "managed": {}}
        for index, rel in enumerate(FILES):
            target = self.home / rel
            if target.is_symlink() or (target.exists() and not target.is_file()):
                raise RuntimeError(f"Refusing non-regular destination: {target}")
            info = None
            if target.exists():
                backup = self.state / f"backup-{index}"
                shutil.copy2(target, backup)
                info = {"backup": backup.name, "mode": target.stat().st_mode & 0o777,
                        "sha256": digest(backup)}
            saved["files"][rel] = info
        saved["klipper"] = self.klipper.snapshot()
        saved["policy"] = self.policy_snapshot()
        return saved

    def restore(self, saved):
        # Validate every backup before making a change.
        for info in saved["files"].values():
            if info and digest(self.state / info["backup"]) != info["sha256"]:
                raise RuntimeError("Installation backup is missing or modified")
        self.check_policy(saved)
        self.klipper.lines()  # Validate before restoring any desktop state.
        # Fail before teardown if privilege is unavailable, leaving a retryable install.
        if saved.get("policy", {}).get("owned") and self.policy.exists():
            self.privileged("rm", "--", self.policy)
        self.stop_if_running()
        self.shortcuts.restore(saved["shortcuts"])
        self.service("disable", UNIT, check=False)
        for rel, info in saved["files"].items():
            target = self.home / rel
            if info:
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(self.state / info["backup"], target)
                target.chmod(info["mode"])
            else:
                target.unlink(missing_ok=True)
        self.service("daemon-reload")
        if saved["service"]["enabled"]:
            self.service("enable", UNIT)
        if saved["service"]["active"]:
            self.service("start", UNIT)
        if "klipper" in saved:
            self.klipper.write(saved["klipper"])
            self.reload_klipper()

    def install(self, binary):
        self.shortcuts.check_owner()
        old = self.load()
        if old:
            self.check_files(old)
            saved = old
            # Upgrade older installation snapshots without replacing original backups.
            if "klipper" not in saved:
                saved["klipper"] = self.klipper.snapshot()
            if "policy" not in saved:
                saved["policy"] = self.policy_snapshot()
            atomic_json(self.manifest, saved)
        else:
            saved = self.backup()
            # Durable snapshot precedes the first desktop change, including failure recovery.
            atomic_json(self.manifest, saved)
        try:
            self.install_policy(saved)
            self.klipper.write()
            self.reload_klipper()
            self.stop_if_running()
            sources = [Path(binary).read_bytes(), (self.source / "clipboard-panel.service").read_bytes(),
                       (self.source / "org.local.clipboard-panel.desktop").read_text().replace(
                           "@BINARY@", '"' + str(self.home / FILES[0]).replace('"', '\\"') + '"').encode()]
            for rel, content in zip(FILES, sources):
                target = self.home / rel
                target.parent.mkdir(parents=True, exist_ok=True)
                temporary = target.with_name(target.name + ".clipboard-panel-tmp")
                temporary.write_bytes(content)
                temporary.chmod(0o755 if rel == FILES[0] else 0o644)
                temporary.replace(target)
                saved["managed"][rel] = digest(target)
                atomic_json(self.manifest, saved)
            self.service("daemon-reload")
            self.service("enable", UNIT)
            self.service("start", UNIT)
            ready = False
            for _ in range(30):
                reply = self.runner(["busctl", "--user", "--json=short", "call", "org.local.ClipboardPanel",
                                     "/panel", "org.local.ClipboardPanel", "Status"], check=False)
                if reply.returncode == 0 and json.loads(reply.stdout).get("data") == ["ready"]:
                    ready = True
                    break
                time.sleep(.1)
            if not ready:
                raise RuntimeError("Panel did not become ready")
            self.shortcuts.handover(saved["shortcuts"])
            saved["complete"] = True
            atomic_json(self.manifest, saved)
        except BaseException:
            self.restore(saved)
            # Preserve backups and the snapshot for inspection; the restored desktop has no managed files.
            saved["managed"] = {}
            saved["complete"] = False
            atomic_json(self.manifest, saved)
            raise

    def uninstall(self):
        saved = self.load()
        if not saved:
            raise RuntimeError("No installation snapshot exists; nothing changed")
        self.check_files(saved)
        self.restore(saved)
        self.manifest.unlink()
        # Backups remain; private clipboard history is deliberately retained.


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--uninstall", action="store_true")
    parser.add_argument("--build-dir", type=Path)
    args = parser.parse_args()
    source = Path(__file__).resolve().parent
    home = Path.home()
    if args.dry_run:
        print("DRY RUN: no commands executed; no clipboard, shortcuts, files, services or windows changed.")
        print("Operation:", "restore saved bindings/files/service state" if args.uninstall else "install")
        print("Build: cmake -S", source, "-B <private scratch>/cmake -DCMAKE_BUILD_TYPE=Release")
        print("Build: heavy cmake --build <private scratch>/cmake --parallel 2")
        for rel in FILES:
            print("Managed file:", home / rel)
        print("Klipper: record previous [General] MaxClipItems in ~/.config/klipperrc; set MaxClipItems=100; call org.kde.klipper /klipper org.kde.klipper.klipper reloadConfig.")
        print("Polkit: sudo -n install -m 0644", source / POLICY, POLICY_DIRECTORY / POLICY)
        print("Polkit: auth_self for active users on EVERY Borrar todo; inactive/other sessions denied. Plasma handles the password.")
        print("Uninstall/rollback: restore previous MaxClipItems (or remove the added key/file), reload Klipper; sudo -n remove only the policy installed by this installer.")
        print("Private backups + exact four-chord shortcut snapshot:", home / ".local/state/clipboard-panel")
        print("Read Meta+V owners; abort on any owner except plasmashell/show-on-mouse-pos and clipboard-panel/show-history.")
        print("Record both actions' current keys; remove ONLY Meta+V from plasmashell/show-on-mouse-pos.")
        print("Start/enable clipboard-panel.service; verify Status=ready; add Meta+V to clipboard-panel/show-history.")
        print("No window opens until Meta+V. No clipboard is changed until an item is chosen.")
        print("Failure: stop service; restore exact bindings, previous files and prior service state.")
        print("Uninstall: ./install.sh --uninstall (restores snapshot; retains private history and backups).")
        return
    os.umask(0o077)
    installer = Installer(home, source)
    installer.state.mkdir(parents=True, exist_ok=True)
    installer.state.chmod(0o700)
    with (installer.state / "lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        if args.uninstall:
            installer.uninstall()
        else:
            if args.build_dir:
                binary = args.build_dir / "clipboard-panel"
                if not binary.is_file():
                    raise RuntimeError("Build the supplied --build-dir first")
                installer.install(binary)
            else:
                scratch = home / ".cache/agent-scratch/clipboard-panel"
                scratch.mkdir(parents=True, exist_ok=True)
                with tempfile.TemporaryDirectory(prefix="install-", dir=scratch) as temporary:
                    build = Path(temporary) / "cmake"
                    subprocess.run(["cmake", "-S", source, "-B", build, "-DCMAKE_BUILD_TYPE=Release"], check=True)
                    heavy = shutil.which("heavy")
                    subprocess.run(([heavy] if heavy else []) + ["cmake", "--build", str(build), "--parallel", "2"], check=True)
                    installer.install(build / "clipboard-panel")
    print("Restored previous configuration." if args.uninstall else "Installed. Meta+V opens Portapapeles; choose, then press Ctrl+V yourself.")

if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        # Never include clipboard contents (none are read here).
        print("Installation failed:", error, file=sys.stderr)
        sys.exit(1)
