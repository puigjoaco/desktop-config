#!/usr/bin/python3
"""Orderly restart/clipboard ownership checks in a private KWin session. Run through heavy."""
import base64
import contextlib
import hashlib
import json
import os
import pathlib
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import uuid

if not __debug__ or os.environ.get('PYTHONOPTIMIZE'):
    raise SystemExit('Run without -O/PYTHONOPTIMIZE: assertions are acceptance checks.')
HERE = pathlib.Path(__file__).resolve().parent
BUILD = pathlib.Path(sys.argv[1]).resolve()
OUT = pathlib.Path.home() / '.cache/agent-scratch/desktop-config/clipboard-stack/shutdown'
OUT.mkdir(parents=True, exist_ok=True)
runtime = pathlib.Path(os.environ.get('QRO_SHUTDOWN_RUNTIME') or tempfile.mkdtemp(prefix='qro-stop-'))
os.environ['QRO_SHUTDOWN_RUNTIME'] = str(runtime)
runtime.mkdir(exist_ok=True, mode=0o700)
os.chmod(runtime, 0o700)
manager_env = dict(os.environ, XDG_RUNTIME_DIR=f'/run/user/{os.getuid()}',
                   DBUS_SESSION_BUS_ADDRESS=f'unix:path=/run/user/{os.getuid()}/bus')
os.environ.update(XDG_RUNTIME_DIR=str(runtime), WAYLAND_DISPLAY='qro-stack-test-shutdown',
                  XDG_CONFIG_HOME=str(OUT / 'config'), XDG_DATA_HOME=str(OUT / 'data'),
                  XDG_STATE_HOME=str(OUT / 'state'), XDG_CACHE_HOME=str(OUT / 'cache'),
                  QT_QPA_PLATFORM='wayland', KWIN_COMPOSE='O2', QT_QUICK_BACKEND='software',
                  XDG_CURRENT_DESKTOP='KDE', KDE_FULL_SESSION='true')
for name in ('DISPLAY', 'SESSION_MANAGER', 'DBUS_STARTER_ADDRESS', 'DBUS_STARTER_BUS_TYPE', 'QT_WAYLAND_RECONNECT'):
    os.environ.pop(name, None)
if '--inner' not in sys.argv:
    child = subprocess.Popen(['dbus-run-session', '--', sys.executable, str(__file__), str(BUILD), '--inner'], start_new_session=True)
    try:
        result = child.wait()
    finally:
        try: os.killpg(child.pid, signal.SIGTERM)
        except ProcessLookupError: pass
        shutil.rmtree(runtime, ignore_errors=True)
    sys.exit(result)

bin_dir = OUT / 'bin'
bin_dir.mkdir(exist_ok=True)
shim = bin_dir / 'ydotool'
shim.write_text('#!/bin/sh\nexec "' + str(BUILD / 'stack-input') + '" "$@"\n')
shim.chmod(0o700)
os.environ['PATH'] = str(bin_dir) + ':' + os.environ['PATH']
apps = OUT / 'data/applications'
apps.mkdir(parents=True, exist_ok=True)
(apps / 'qro-stack-input.desktop').write_text('[Desktop Entry]\nType=Application\nName=QRO isolated input test\nExec=' + str(BUILD / 'stack-input') + '\nNoDisplay=true\nX-KDE-Wayland-Interfaces=org_kde_kwin_fake_input\n')
(OUT / 'config').mkdir(exist_ok=True)
(OUT / 'config/kglobalshortcutsrc').write_text('[services][org.kde.spectacle.desktop]\n_launch=none\n')
subprocess.run(['kbuildsycoca6', '--noincremental'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)

def run(*args, env=None, check=True, timeout=15):
    return subprocess.run(args, env=env, check=check, capture_output=True, text=True, timeout=timeout).stdout.strip()

def manager(*args, check=True):
    return run(*args, env=manager_env, check=check)

def dbus(service, path, method, *args):
    return run('qdbus6', service, path, method, *map(str, args))

def probe(method, *args):
    return dbus('org.local.StackProbe', '/', 'org.local.StackProbe.' + method, *args)

def stack(method):
    return dbus('org.local.QuickRegionOverlay', '/Stack', 'org.local.QuickRegionOverlay.Stack.' + method)

def wait(predicate, description, timeout=8):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        try:
            result = predicate()
            if result: return result
        except (subprocess.CalledProcessError, ValueError, json.JSONDecodeError): pass
        time.sleep(0.02)
    raise RuntimeError('Timed out: ' + description)

def clipboard():
    probe('ReadClipboard')
    result = wait(lambda: json.loads(probe('Clipboard')) or None, 'full MIME snapshot', 70)
    assert result.pop('_error') == ''
    return {key: base64.b64decode(value) for key, value in result.items()}

def digest(item):
    return hashlib.sha256(b''.join(key.encode() + b'\0' + item[key] for key in sorted(item))).hexdigest()

def shortcut(letter):
    assert stack('Focus') == 'qro-stack-probe', 'dedicated editor must own focus before synthetic input'
    code = {'c': 46, 'v': 47, 'x': 45}[letter]
    run(str(BUILD / 'stack-input'), '29:1', '56:1', f'{code}:1', f'{code}:0', '56:0', '29:0')

def units():
    return {line.split()[0] for line in manager('systemctl', '--user', 'list-units', '--all', '--plain', '--no-legend', 'qro-clipboard-holder-*').splitlines()}

def properties(unit):
    return dict(line.split('=', 1) for line in manager('systemctl', '--user', 'show', unit, '-p', 'MainPID', '-p', 'ControlGroup', '-p', 'ActiveState').splitlines())

results = []
def passed(name, **evidence):
    results.append(dict(check=name, **evidence))
    (OUT / 'results.json').write_text(json.dumps(results, indent=2))
    print(json.dumps(results[-1]), flush=True)

with contextlib.ExitStack() as cleanup:
    owned_units = set()
    def stop_process(process):
        if process.poll() is not None: return
        process.terminate()
        try: process.wait(timeout=6)
        except subprocess.TimeoutExpired: process.kill(); process.wait(timeout=5)
    def start(name, args):
        log = cleanup.enter_context((OUT / (name + '.log')).open('a'))
        process = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT)
        cleanup.callback(stop_process, process)
        print(name, 'PID', process.pid, flush=True)
        return process
    def cleanup_units():
        for unit in list(owned_units): manager('systemctl', '--user', 'stop', unit, check=False)
    kwin = start('kwin', ['kwin_wayland', '--virtual', '--width', '1024', '--height', '768', '--socket', 'qro-stack-test-shutdown', '--no-lockscreen', '--no-kactivities'])
    wait(lambda: (runtime / 'qro-stack-test-shutdown').exists() and dbus('org.kde.KWin', '/KWin', 'org.kde.KWin.activeOutputName'), 'nested compositor')
    start('input-keeper', [str(BUILD / 'stack-input'), '--keepalive'])
    editor = start('probe', [str(BUILD / 'stack-probe')])
    wait(lambda: probe('Events') == '[]', 'editor service')
    cleanup.callback(cleanup_units) # Stop services before disconnecting their compositor.

    def start_daemon(name):
        unit = 'qro-stack-shutdown-' + str(uuid.uuid4())
        owned_units.add(unit + '.service')
        args = ['systemd-run', '--user', '--collect', '--quiet', '--unit=' + unit,
                '--property=StandardOutput=append:' + str(OUT / (name + '.log')),
                '--property=StandardError=append:' + str(OUT / (name + '.log')),
                '--property=UnsetEnvironment=QT_WAYLAND_RECONNECT DISPLAY']
        for key in ('WAYLAND_DISPLAY', 'XDG_RUNTIME_DIR', 'DBUS_SESSION_BUS_ADDRESS', 'XDG_STATE_HOME', 'XDG_CONFIG_HOME', 'XDG_DATA_HOME', 'XDG_CACHE_HOME', 'PATH', 'QT_QPA_PLATFORM', 'KDE_FULL_SESSION', 'XDG_CURRENT_DESKTOP'):
            args.append('--setenv=' + key + '=' + os.environ[key])
        manager(*args, str(BUILD / 'quick-region-overlay'), '--daemon')
        wait(lambda: stack('State'), 'daemon start')
        wait(lambda: stack('Focus') == 'qro-stack-probe', 'dedicated editor focus')
        return unit + '.service'

    def stop_daemon(unit, expected, baseline, label):
        daemon_info = properties(unit)
        started = time.monotonic()
        manager('systemctl', '--user', 'stop', unit)
        elapsed = time.monotonic() - started
        new = wait(lambda: units() - baseline, 'independent holder')
        assert len(new) == 1, new
        holder = new.pop()
        owned_units.add(holder)
        info = properties(holder)
        assert info['ActiveState'] == 'active' and int(info['MainPID']) > 0, info
        assert info['ControlGroup'] != daemon_info['ControlGroup']
        assert not pathlib.Path('/proc/' + daemon_info['MainPID']).exists(), daemon_info
        after = clipboard()
        assert after == expected, (sorted(after), sorted(expected))
        assert not list((OUT / 'state/quick-region-overlay').glob('clipboard-holder-*.qrs')), 'unacknowledged snapshot'
        passed(label, formats=sorted(expected), digest=digest(after), stop_seconds=round(elapsed, 3), daemon=daemon_info, holder=dict(unit=holder, **info))
        return holder

    def replace_and_check_exit(holder):
        info = properties(holder)
        probe('Custom', 'binary', 'another app copied'); probe('Offer')
        wait(lambda: not pathlib.Path('/proc/' + info['MainPID']).exists(), 'holder exits on cancellation')
        wait(lambda: holder not in units(), 'holder unit collected')
        owned_units.discard(holder)

    daemon = start_daemon('initial')
    shortcut('x'); wait(lambda: stack('Count') == '0', 'clear stack')
    probe('Custom', 'binary', 'stack item'); shortcut('c')
    wait(lambda: stack('Count') == '1', 'stack copy')
    for mode in ('image', 'binary', 'secret'):
        probe('Custom', mode, '#123456' if mode == 'image' else 'pre-paste original')
        probe('Offer'); before = clipboard()
        assert (secret := before.get('x-kde-passwordManagerHint')) == (b'secret' if mode == 'secret' else None)
        probe('IgnorePaste', 'true')
        baseline = units()
        shortcut('v'); wait(lambda: stack('State').startswith('6:'), 'mid-paste intermediate source')
        # Reading the intermediate ourselves would count as consumption and advance the paste.
        assert stack('State') == '6:1'
        holder = stop_daemon(daemon, before, baseline, 'mid-paste shutdown restores ' + mode)
        daemon = start_daemon('restart-' + mode)
        assert stack('Count') == '1'
        assert clipboard() == before, 'restart must not disturb holder'
        replace_and_check_exit(holder)
        passed('holder exits after another app copies: ' + mode)
    probe('IgnorePaste', 'false')
    for mode in ('image', 'binary'):
        probe('Custom', mode, '#abcdef' if mode == 'image' else 'completed original'); probe('Offer')
        before = clipboard(); baseline = units()
        probe('ResetEvents'); shortcut('v')
        wait(lambda: len(json.loads(probe('Events'))) == 1, 'target reads paste')
        wait(lambda: stack('State') == '0:1', 'paste completes')
        assert clipboard() == before
        holder = stop_daemon(daemon, before, baseline, 'completed-paste shutdown preserves ' + mode)
        daemon = start_daemon('restart-complete-' + mode)
        assert clipboard() == before
        replace_and_check_exit(holder)
    # Exercise ownership cancellation in the daemon itself, after it restored a source it owned.
    probe('ResetEvents'); shortcut('v')
    wait(lambda: len(json.loads(probe('Events'))) == 1, 'paste before external replacement')
    wait(lambda: stack('State') == '0:1', 'restore before external replacement')
    probe('Custom', 'binary', 'external replacement'); probe('Offer')
    assert clipboard().get('text/plain') == b'external replacement'
    baseline = units()
    manager('systemctl', '--user', 'stop', daemon)
    assert units() == baseline, 'no live source must not start a holder'
    passed('shutdown after external replacement owns nothing and starts no holder')
    # Standalone holder must reject malformed/nonprivate snapshots without acknowledging them.
    state_dir = OUT / 'state/quick-region-overlay'
    invalid = state_dir / 'clipboard-holder-invalid.qrs'
    invalid.write_bytes(b'bad'); invalid.chmod(0o600)
    rejected = subprocess.run([str(BUILD / 'quick-region-overlay'), '--hold-clipboard', str(invalid)], capture_output=True, timeout=5)
    assert rejected.returncode == 1 and invalid.exists()
    invalid.unlink()
    passed('invalid holder snapshot is retained on failure')
    # A launcher failure must retain the original; replaying that file proves its exact contents.
    daemon = start_daemon('failure-retention')
    probe('Custom', 'binary', 'failed-launch original'); probe('Offer')
    before = clipboard()
    probe('IgnorePaste', 'true'); shortcut('v')
    wait(lambda: stack('State').startswith('6:'), 'mid-paste before launch failure')
    failed_launcher = bin_dir / 'systemd-run'
    failed_launcher.write_text('#!/bin/sh\nexit 1\n'); failed_launcher.chmod(0o700)
    try: manager('systemctl', '--user', 'stop', daemon)
    finally: failed_launcher.unlink()
    snapshots = list(state_dir.glob('clipboard-holder-*.qrs'))
    assert len(snapshots) == 1 and snapshots[0].stat().st_mode & 0o777 == 0o600
    snapshot = snapshots[0]
    recovery = 'qro-clipboard-holder-recovery-' + str(uuid.uuid4())
    owned_units.add(recovery + '.service')
    args = ['systemd-run', '--user', '--collect', '--quiet', '--unit=' + recovery]
    for key in ('WAYLAND_DISPLAY', 'XDG_RUNTIME_DIR', 'DBUS_SESSION_BUS_ADDRESS'):
        args.append('--setenv=' + key + '=' + os.environ[key])
    manager(*args, str(BUILD / 'quick-region-overlay'), '--hold-clipboard', str(snapshot))
    wait(lambda: not snapshot.exists(), 'recovery holder acknowledges saved original')
    assert clipboard() == before
    replace_and_check_exit(recovery + '.service')
    passed('failed handoff retains a private, byte-exact original snapshot; recovery holder serves it')
    # Only day-old snapshots may be cleaned up at startup.
    old = state_dir / 'clipboard-holder-old.qrs'
    recent = state_dir / 'clipboard-holder-recent.qrs'
    for path in (old, recent): path.write_bytes(b'preserved failure snapshot'); path.chmod(0o600)
    os.utime(old, (time.time() - 90000, time.time() - 90000))
    daemon = start_daemon('stale-cleanup')
    assert not old.exists() and recent.exists()
    manager('systemctl', '--user', 'stop', daemon)
    recent.unlink()
    passed('startup removes only snapshots older than one day')
print('PASS; logs and results:', OUT, flush=True)
