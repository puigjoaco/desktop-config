#!/usr/bin/env python3
import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import shutil
import xml.etree.ElementTree as ET
import tempfile
import sys
sys.dont_write_bytecode = True
import unittest

SOURCE = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("installer", SOURCE / "installer.py")
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class FakeBus:
    def __init__(self):
        self.keys = {tuple(m.KLIPPER): [m.KEY, [16777224, 0, 0, 0], [65, 66, 0, 0]],
                     tuple(m.PANEL): [[80, 81, 82, 83]]}
        self.calls = []
        self.unknown_owner = False

    def __call__(self, args, check=True):
        self.calls.append(args)
        method, signature, *arguments = args[7:]
        if method == "getGlobalShortcutsByKey":
            data = [["else" if self.unknown_owner else m.KLIPPER[1], "friendly", m.KLIPPER[0], "friendly", "default", "default", [m.KEY[0]], []]]
            reply = {"type": "a(ssssssaiai)", "data": [data]}
        else:
            self.assert_count = int(arguments[0])
            assert self.assert_count == 4
            action = tuple(arguments[1:5])
            if method == "setForeignShortcutKeys":
                count = int(arguments[5]); rest = arguments[6:]; keys = []
                for index in range(count):
                    chunk = rest[index*5:index*5+5]
                    assert int(chunk[0]) == 4
                    keys.append([int(n) for n in chunk[1:]])
                assert len(rest) == 5*count
                self.keys[action] = keys
            reply = {"type": "a(ai)", "data": [[[[n for n in key]] for key in self.keys.get(action, [])]]}
        return subprocess.CompletedProcess(args, 0, json.dumps(reply), "")


class InstallerTest(unittest.TestCase):
    def test_exact_shortcut_snapshot_handover_restore(self):
        bus = FakeBus(); shortcuts = m.Shortcuts(bus)
        before = copy.deepcopy(bus.keys)
        shortcuts.check_owner(); snapshot = shortcuts.snapshot()
        shortcuts.handover(snapshot)
        self.assertNotIn(m.KEY, bus.keys[tuple(m.KLIPPER)])
        self.assertIn(m.KEY, bus.keys[tuple(m.PANEL)])
        self.assertIn([65, 66, 0, 0], bus.keys[tuple(m.KLIPPER)])
        shortcuts.restore(snapshot)
        self.assertEqual(before, bus.keys)

    def test_foreign_owner_refused_and_malformed_chords_never_sent(self):
        bus = FakeBus(); shortcuts = m.Shortcuts(bus); bus.unknown_owner = True
        with self.assertRaises(RuntimeError):
            shortcuts.check_owner()
        calls = len(bus.calls)
        for invalid in ([[1]], [[1, 2, 3]], [[-1, 0, 0, 0]], [[2147483648, 0, 0, 0]], [[True, 0, 0, 0]]):
            with self.assertRaises(ValueError):
                shortcuts.set(m.PANEL, invalid)
        self.assertEqual(calls, len(bus.calls))

    def setup_install(self, home):
        bus = FakeBus(); shortcuts = m.Shortcuts(bus)
        services = {"enabled": False, "active": False}
        calls = []
        def runner(args, check=True):
            calls.append(args)
            result = ""
            if args[0] == "systemctl":
                operation = args[2]
                if operation == "stop" and not (Path(home) / m.FILES[1]).exists():
                    raise subprocess.CalledProcessError(5, args, stderr="Unit not loaded")
                if operation == "is-enabled": result = "enabled" if services["enabled"] else "disabled"
                if operation == "is-active": result = "active" if services["active"] else "inactive"
                if operation in ("enable", "disable"): services["enabled"] = operation == "enable"
                if operation in ("start", "stop"): services["active"] = operation == "start"
            elif args[0] == "sudo":
                assert args[1] == "-n"
                if args[2] == "install":
                    destination = Path(args[-1]);destination.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(args[-2], destination)
                elif args[2] == "rm":
                    Path(args[-1]).unlink()
                else:
                    raise AssertionError(args)
            else:
                result = json.dumps({"type": "s", "data": ["ready"]})
            return subprocess.CompletedProcess(args, 0, result, "")
        installer = m.Installer(home, SOURCE, shortcuts, runner, Path(home) / "system-policies")
        installer.state.mkdir(parents=True)
        binary = Path(home) / "synthetic-binary"; binary.write_bytes(b"synthetic executable")
        return installer, binary, bus, services, calls

    def test_install_reinstall_uninstall_restores_files_and_bindings(self):
        with tempfile.TemporaryDirectory() as home:
            installer, binary, bus, services, calls = self.setup_install(home)
            previous = copy.deepcopy(bus.keys)
            target = Path(home) / m.FILES[0]; target.parent.mkdir(parents=True); target.write_bytes(b"prior executable")
            installer.install(binary)
            self.assertEqual(target.read_bytes(), binary.read_bytes())
            installer.install(binary)
            saved = installer.load()
            self.assertEqual(saved["shortcuts"]["klipper"], previous[tuple(m.KLIPPER)])
            installer.uninstall()
            self.assertEqual(target.read_bytes(), b"prior executable")
            self.assertEqual(bus.keys, previous)
            self.assertFalse((Path(home) / m.FILES[1]).exists())
            self.assertEqual(services, {"enabled": False, "active": False})
            self.assertFalse(installer.manifest.exists())

    def test_failure_rolls_back_and_changed_files_are_protected(self):
        with tempfile.TemporaryDirectory() as home:
            installer, binary, bus, services, calls = self.setup_install(home)
            original = copy.deepcopy(bus.keys)
            handover = installer.shortcuts.handover
            def fail(saved):
                handover(saved)
                raise RuntimeError("synthetic failure after shortcut handover")
            installer.shortcuts.handover = fail
            with self.assertRaises(RuntimeError): installer.install(binary)
            self.assertEqual(bus.keys, original)
            self.assertFalse((Path(home) / m.FILES[0]).exists())
            installer.shortcuts.handover = handover
            installer.install(binary)
            target = Path(home) / m.FILES[0]; target.write_bytes(b"user changed this")
            before = len(calls)
            with self.assertRaises(RuntimeError): installer.uninstall()
            self.assertEqual(len(calls), before)
            self.assertEqual(target.read_bytes(), b"user changed this")

    def test_klipper_limit_and_policy_install_restore(self):
        for initial in (None, "[General]\nMaxClipItems=20\nOther=true\n", "[General]\nOther=true\n", "[Other]\nx=1\n"):
            with self.subTest(initial=initial), tempfile.TemporaryDirectory() as home:
                installer, binary, bus, services, calls = self.setup_install(home)
                config = installer.klipper.path
                if initial is not None:
                    config.parent.mkdir(parents=True, exist_ok=True);config.write_text(initial)
                installer.install(binary)
                self.assertIn("MaxClipItems=100", config.read_text())
                self.assertTrue(installer.policy.exists())
                self.assertEqual(installer.load()["policy"]["owned"], True)
                self.assertTrue(any(args[0] == "busctl" and args[-1] == "reloadConfig" for args in calls))
                snapshot = installer.load()["klipper"]
                installer.install(binary)
                self.assertEqual(installer.load()["klipper"], snapshot)
                installer.uninstall()
                self.assertFalse(installer.policy.exists())
                self.assertEqual(config.read_text() if config.exists() else None, initial)

    def test_klipper_limit_never_lowered_and_later_change_kept(self):
        with tempfile.TemporaryDirectory() as home:
            installer, binary, *_ = self.setup_install(home)
            config = installer.klipper.path; config.parent.mkdir(parents=True, exist_ok=True)
            config.write_text("[General]\nMaxClipItems=250\n")
            installer.install(binary)
            self.assertEqual(config.read_text(), "[General]\nMaxClipItems=250\n")
            installer.uninstall()
            self.assertEqual(config.read_text(), "[General]\nMaxClipItems=250\n")
        with tempfile.TemporaryDirectory() as home:
            installer, binary, *_ = self.setup_install(home)
            config = installer.klipper.path; config.parent.mkdir(parents=True, exist_ok=True)
            config.write_text("[General]\nMaxClipItems=200\n")
            installer.install(binary)
            config.write_text("[General]\nMaxClipItems=100\n")
            installer.uninstall()
            self.assertEqual(config.read_text(), "[General]\nMaxClipItems=100\n")
        with tempfile.TemporaryDirectory() as home:
            installer, binary, *_ = self.setup_install(home)
            config = installer.klipper.path; config.parent.mkdir(parents=True, exist_ok=True)
            config.write_text("[General]\nMaxClipItems=20\n")
            installer.install(binary)
            config.write_text("[General]\nMaxClipItems=200\n")
            installer.uninstall()
            self.assertEqual(config.read_text(), "[General]\nMaxClipItems=200\n")

    def test_config_restore_preserves_unrelated_later_settings(self):
        with tempfile.TemporaryDirectory() as home:
            installer, binary, *_ = self.setup_install(home);installer.install(binary)
            config = installer.klipper.path
            config.write_text(config.read_text() + "Later=true\n[Other]\nx=1\n")
            installer.uninstall()
            self.assertNotIn("MaxClipItems", config.read_text())
            self.assertIn("Later=true", config.read_text());self.assertIn("x=1", config.read_text())

    def test_existing_policy_not_removed_and_modified_policy_refused(self):
        with tempfile.TemporaryDirectory() as home:
            installer, binary, *rest = self.setup_install(home)
            installer.policy.parent.mkdir();shutil.copyfile(SOURCE / m.POLICY, installer.policy)
            installer.install(binary);self.assertFalse(installer.load()["policy"]["owned"])
            installer.policy.write_text("user change")
            before = len(rest[-1])
            with self.assertRaises(RuntimeError): installer.uninstall()
            self.assertEqual(len(rest[-1]), before)
            shutil.copyfile(SOURCE / m.POLICY, installer.policy)
            installer.uninstall();self.assertTrue(installer.policy.exists())

    def test_upgrade_from_clear_only_installs_new_policy_and_restores_exact_state(self):
        for preexisting in (False, True):
            with self.subTest(preexisting=preexisting), tempfile.TemporaryDirectory() as home:
                installer, binary, *_ = self.setup_install(home)
                if preexisting:
                    installer.policy.parent.mkdir()
                    shutil.copyfile(SOURCE / m.POLICY, installer.policy)
                    shutil.copyfile(SOURCE / m.LIMIT_POLICY, installer.limit_policy)
                installer.install(binary)
                saved = installer.load()
                # Today's installation: unchanged clear policy and no limit-policy snapshot.
                saved.pop("limit_policy")
                if not preexisting:
                    installer.limit_policy.unlink()
                m.atomic_json(installer.manifest, saved)
                old_clear = installer.policy.read_bytes()
                old_files = copy.deepcopy(saved["files"])
                installer.install(binary)
                self.assertEqual(installer.policy.read_bytes(), old_clear)
                self.assertEqual(installer.limit_policy.read_bytes(), (SOURCE / m.LIMIT_POLICY).read_bytes())
                self.assertEqual(installer.load()["files"], old_files)
                self.assertEqual(installer.load()["limit_policy"]["owned"], not preexisting)
                installer.install(binary)
                installer.uninstall()
                self.assertEqual(installer.policy.exists(), preexisting)
                self.assertEqual(installer.limit_policy.exists(), preexisting)
                if preexisting:
                    self.assertEqual(installer.policy.read_bytes(), old_clear)
                    self.assertEqual(installer.limit_policy.read_bytes(), (SOURCE / m.LIMIT_POLICY).read_bytes())

    def test_upgrade_failure_removes_only_new_policy_and_retains_original_snapshots(self):
        with tempfile.TemporaryDirectory() as home:
            installer, binary, *_ = self.setup_install(home)
            installer.policy.parent.mkdir()
            shutil.copyfile(SOURCE / m.POLICY, installer.policy)
            installer.install(binary)
            saved = installer.load(); saved.pop("limit_policy");installer.limit_policy.unlink()
            m.atomic_json(installer.manifest, saved)
            old_clear = installer.policy.read_bytes()
            def fail(_):
                raise RuntimeError("synthetic upgrade failure")
            installer.shortcuts.handover = fail
            with self.assertRaisesRegex(RuntimeError, "synthetic upgrade failure"):
                installer.install(binary)
            self.assertFalse(installer.limit_policy.exists())
            self.assertEqual(installer.policy.read_bytes(), old_clear)
            self.assertEqual(installer.load()["files"], saved["files"])
            self.assertEqual(installer.load()["shortcuts"], saved["shortcuts"])

    def test_second_policy_removal_failure_restores_first_removed_policy(self):
        with tempfile.TemporaryDirectory() as home:
            installer, binary, *_ = self.setup_install(home);installer.install(binary)
            original = installer.runner
            def refuse_clear_removal(args, check=True):
                if args[:3] == ["sudo", "-n", "rm"] and args[-1] == installer.policy:
                    raise subprocess.CalledProcessError(1, args)
                return original(args, check)
            installer.runner = refuse_clear_removal
            with self.assertRaisesRegex(RuntimeError, "passwordless sudo"):
                installer.uninstall()
            self.assertEqual(installer.limit_policy.read_bytes(), (SOURCE / m.LIMIT_POLICY).read_bytes())
            self.assertTrue(installer.policy.exists());self.assertTrue(installer.load()["complete"])
            installer.runner = original;installer.uninstall()
            self.assertFalse(installer.limit_policy.exists());self.assertFalse(installer.policy.exists())

    def test_existing_limit_policy_modified_or_foreign_is_refused(self):
        with tempfile.TemporaryDirectory() as home:
            installer, binary, *_ = self.setup_install(home)
            installer.limit_policy.parent.mkdir();installer.limit_policy.write_text("foreign policy")
            with self.assertRaisesRegex(RuntimeError, "Existing polkit policy differs"):
                installer.install(binary)
            self.assertEqual(installer.limit_policy.read_text(), "foreign policy")
            installer.limit_policy.unlink();installer.install(binary)
            installer.limit_policy.write_text("user edit")
            with self.assertRaisesRegex(RuntimeError, "Managed polkit policy changed"):
                installer.install(binary)
            with self.assertRaisesRegex(RuntimeError, "Managed polkit policy changed"):
                installer.uninstall()
            self.assertEqual(installer.limit_policy.read_text(), "user edit")

    def test_dangling_managed_policy_symlink_is_refused(self):
        with tempfile.TemporaryDirectory() as home:
            installer, binary, *_ = self.setup_install(home);installer.install(binary)
            installer.limit_policy.unlink();installer.limit_policy.symlink_to(Path(home) / "missing")
            with self.assertRaisesRegex(RuntimeError, "Managed polkit policy changed"):
                installer.install(binary)
            with self.assertRaisesRegex(RuntimeError, "Managed polkit policy changed"):
                installer.uninstall()
            self.assertFalse((Path(home) / "missing").exists())

    def test_policy_authentication_defaults(self):
        for name, action_id in ((m.POLICY, "clear-history"), (m.LIMIT_POLICY, "change-limit")):
            root = ET.parse(SOURCE / name).getroot()
            action = root.find("action");self.assertEqual(action.attrib["id"], "org.local.clipboard-panel." + action_id)
            defaults = action.find("defaults")
            self.assertEqual(defaults.findtext("allow_active"), "auth_self")
            self.assertEqual(defaults.findtext("allow_inactive"), "no")
            self.assertEqual(defaults.findtext("allow_any"), "no")

    def test_sudo_failure_is_clear_and_does_not_install(self):
        with tempfile.TemporaryDirectory() as home:
            installer, binary, bus, services, calls = self.setup_install(home)
            previous = installer.runner
            def refused(args, check=True):
                if args[0] == "sudo": raise subprocess.CalledProcessError(1, args)
                return previous(args, check)
            installer.runner = refused
            with self.assertRaisesRegex(RuntimeError, "passwordless sudo"):
                installer.install(binary)
            self.assertFalse(installer.policy.exists())
            self.assertFalse((Path(home) / m.FILES[0]).exists())

    def test_uninstall_policy_removal_failure_is_retryable(self):
        with tempfile.TemporaryDirectory() as home:
            installer, binary, bus, services, calls = self.setup_install(home)
            target = Path(home) / m.FILES[0]
            target.parent.mkdir(parents=True);target.write_bytes(b"prior executable");target.chmod(0o700)
            config = installer.klipper.path
            config.parent.mkdir(parents=True, exist_ok=True)
            config.write_text("[General]\nMaxClipItems=20\nOther=true\n")
            original_config = config.read_bytes()
            original_bindings = copy.deepcopy(bus.keys)
            original_services = services.copy()
            installer.install(binary)
            installed_files = {rel: (Path(home) / rel).read_bytes() for rel in m.FILES}
            installed_bindings = copy.deepcopy(bus.keys)
            installed_services = services.copy()
            manifest = installer.manifest.read_bytes()
            previous = installer.runner
            def refused(args, check=True):
                if args[:3] == ["sudo", "-n", "rm"]:
                    raise subprocess.CalledProcessError(1, args)
                return previous(args, check)
            installer.runner = refused
            with self.assertRaisesRegex(RuntimeError, "passwordless sudo"):
                installer.uninstall()
            self.assertEqual({rel: (Path(home) / rel).read_bytes() for rel in m.FILES}, installed_files)
            self.assertEqual(bus.keys, installed_bindings)
            self.assertEqual(services, installed_services)
            self.assertEqual(installer.manifest.read_bytes(), manifest)
            self.assertIn("MaxClipItems=100", config.read_text())
            self.assertTrue(installer.policy.exists())
            installer.runner = previous
            installer.uninstall()
            self.assertEqual(target.read_bytes(), b"prior executable")
            self.assertEqual(target.stat().st_mode & 0o777, 0o700)
            self.assertTrue(all(not (Path(home) / rel).exists() for rel in m.FILES[1:]))
            self.assertEqual(config.read_bytes(), original_config)
            self.assertEqual(bus.keys, original_bindings)
            self.assertEqual(services, original_services)
            self.assertFalse(installer.policy.exists())
            self.assertFalse(installer.manifest.exists())

    def test_dry_run_does_not_call_tools_or_create_state(self):
        with tempfile.TemporaryDirectory() as home:
            import os
            env = dict(os.environ, HOME=home)
            result = subprocess.run(["python3", str(SOURCE / "installer.py"), "--dry-run"], env=env,
                                    text=True, capture_output=True, check=True)
            self.assertIn("DRY RUN", result.stdout)
            self.assertIn("MaxClipItems=100", result.stdout)
            self.assertIn("sudo -n install", result.stdout)
            self.assertIn("auth_self", result.stdout)
            self.assertEqual(list(Path(home).iterdir()), [])


if __name__ == "__main__":
    unittest.main()
