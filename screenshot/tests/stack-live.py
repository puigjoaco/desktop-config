#!/usr/bin/python3
"""Narrow live ydotool/modifier check, only in this task's dedicated probe window.

--save starts the probe and retains the user's full clipboard in memory before install.
--verify runs through heavy after install, restores the saved clipboard through the daemon, then closes the probe.
No saved clipboard contents are printed or written to disk.
"""
import base64
import json
import os
import pathlib
import subprocess
import sys
import time
import hashlib
BUILD = pathlib.Path(sys.argv[1]).resolve()
OUT = pathlib.Path.home() / '.cache/agent-scratch/desktop-config/clipboard-stack/live'
OUT.mkdir(parents=True, exist_ok=True)
SERVICE = os.environ.get('QRO_LIVE_PROBE_SERVICE', 'org.local.StackProbe')

def run(*args):
    return subprocess.run(args, capture_output=True, text=True, check=True, timeout=15).stdout.strip()
def probe(method, *args):
    return run('qdbus6', SERVICE, '/', 'org.local.StackProbe.' + method, *map(str, args))
def stack(method):
    return run('qdbus6', 'org.local.QuickRegionOverlay', '/Stack', 'org.local.QuickRegionOverlay.Stack.' + method)
def wait(predicate, label, timeout=8):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        try:
            value = predicate()
            if value: return value
        except subprocess.CalledProcessError: pass
        time.sleep(0.05)
    raise RuntimeError('Timed out: ' + label)
if sys.flags.optimize:
    sys.exit('Run without -O/PYTHONOPTIMIZE: the safety checks use assert.')
def unlocked():
    return run('qdbus6', 'org.freedesktop.ScreenSaver', '/ScreenSaver', 'org.freedesktop.ScreenSaver.GetActive') == 'false'
def guard():
    assert unlocked(), 'Desktop is locked; no synthetic input allowed'
    assert stack('Focus') == 'qro-stack-live-probe', 'Dedicated test editor is not focused; no synthetic input allowed'
def keys(*events):
    guard()
    run('/usr/bin/ydotool', 'key', '--key-delay=5', *events)
def shortcut(code):
    keys('29:1', '56:1', f'{code}:1', f'{code}:0', '56:0', '29:0')

if sys.argv[2] == '--save':
    assert unlocked(), 'Desktop is locked'
    # Refuse to share another probe; this service belongs exclusively to this test.
    services = run('qdbus6')
    assert SERVICE not in services
    log = (OUT / 'probe.log').open('a')
    process = subprocess.Popen([str(BUILD / 'stack-probe'), '--live', '--service', SERVICE], stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    (OUT / 'probe.pid').write_text(str(process.pid))
    wait(lambda: probe('Events') == '[]', 'live probe')
    probe('SaveClipboard')
    digest = wait(lambda: (value if (value := probe('SavedState')).startswith('ready:') else ''), 'save original clipboard', timeout=70)
    (OUT / 'original-digest').write_text(digest.removeprefix('ready:'))
    print('Live probe PID', process.pid, '; original clipboard retained in memory; no input sent', flush=True)
    sys.exit(0)

assert sys.argv[2] == '--verify'
assert unlocked(), 'Desktop is locked; no synthetic input allowed'
focus_script = OUT / 'focus.js'
focus_script.write_text("for (const w of workspace.windowList()) { if (String(w.resourceClass) === 'qro-stack-live-probe') { w.minimized = false; workspace.activeWindow = w; break; } }\n")
focus_name = 'qro-stack-live-test-focus'
run('qdbus6', 'org.kde.KWin', '/Scripting', 'org.kde.kwin.Scripting.unloadScript', focus_name)
script_id = run('qdbus6', 'org.kde.KWin', '/Scripting', 'org.kde.kwin.Scripting.loadScript', str(focus_script), focus_name)
try:
    run('qdbus6', 'org.kde.KWin', '/Scripting/Script' + script_id, 'org.kde.kwin.Script.run')
    wait(lambda: stack('Focus') == 'qro-stack-live-probe', 'dedicated live editor focus')
finally:
    run('qdbus6', 'org.kde.KWin', '/Scripting', 'org.kde.kwin.Scripting.unloadScript', focus_name)
guard()
assert stack('Count') == '0', 'Refusing to change a pre-existing live stack'
expected_digest = (OUT / 'original-digest').read_text().strip()
assert probe('SavedState') == 'ready:' + expected_digest
passed = []
history_before = hashlib.sha256(run('qdbus6', 'org.kde.klipper', '/klipper', 'org.kde.klipper.klipper.getClipboardHistoryMenu').encode()).hexdigest()
held = False
try:
    # The dedicated editor observes the synthetic copy key but does not publish clipboard data. This tests real
    # ydotool and physical modifiers without adding test entries to the user's finite Klipper history.
    probe('RestoreSaved')
    time.sleep(0.15)
    probe('SuppressCopy', 'true')
    probe('Prepare', 'qro-live-key-probe')
    probe('ResetEvents')
    keys('29:1', '56:1', '46:1', '46:0'); held = True
    # The held keyboard is writable, so the daemon writes Alt-up + Ctrl+C into it without waiting for a release.
    event = wait(lambda: (events[0] if (events := json.loads(probe('Events'))) else None), 'copy key while held')
    assert event == {'_copy': True, 'alt': False, 'shift': False}, event
    guard(); run('/usr/bin/ydotool', 'key', '--key-delay=5', '56:0', '29:0'); held = False
    wait(lambda: stack('State') == '0:0', 'nothing-copied restoration')
    passed.append('Ctrl+C reached the dedicated editor while Ctrl+Alt were still held; no Alt/Shift contamination')
    probe('ReadClipboard')
    wait(lambda: probe('Digest') == expected_digest, 'full-MIME original restored after no-copy')
    passed.append('Nothing-copied path retained original full-MIME clipboard without adding history items')
    assert stack('Count') == '0'
    # Exercise a real live stack copy/paste with a test item explicitly marked secret, so this proof also leaves
    # the user's finite clipboard history unchanged. The daemon then owns the restored original after the probe exits.
    probe('SuppressCopy', 'false')
    probe('Custom', 'binary', 'unmarked fixture control')
    assert probe('SourceIsSecret') == 'false', 'Negative history-hint control failed'
    probe('Custom', 'secret', 'qro-live-transfer')
    assert probe('SourceIsSecret') == 'true', 'Refusing to copy an unmarked live test item'
    shortcut(46); wait(lambda: stack('Count') == '1', 'live stack append')
    probe('ResetEvents')
    keys('29:1', '47:1', '47:0', '29:0')
    event = wait(lambda: (events[0] if (events := json.loads(probe('Events'))) else None), 'normal last-item paste')
    assert base64.b64decode(event['text/plain']) == b'qro-live-transfer'
    probe('RestoreSaved'); time.sleep(0.1)
    probe('ResetEvents'); shortcut(47)
    event = wait(lambda: (events[0] if (events := json.loads(probe('Events'))) else None), 'live stack paste', timeout=70)
    assert base64.b64decode(event['text/plain']) == b'qro-live-transfer'
    wait(lambda: stack('State') == '0:1', 'live clipboard restoration')
    passed.append('Real live append, normal Ctrl+V and stack paste; test item kept out of user history')
    probe('ReadClipboard')
    wait(lambda: probe('Digest') == expected_digest, 'original clipboard intact', timeout=70)
    shortcut(45); wait(lambda: stack('Count') == '0', 'clear live test item')
    assert hashlib.sha256(run('qdbus6', 'org.kde.klipper', '/klipper', 'org.kde.klipper.klipper.getClipboardHistoryMenu').encode()).hexdigest() == history_before, 'Live history changed'
    passed.append('Live Klipper history unchanged')
    probe('Quit')
    (OUT / 'results.json').write_text(json.dumps(passed, indent=2))
    print(json.dumps(passed), flush=True)
except BaseException:
    # Keep the task-owned probe alive as the clipboard owner if verification fails; the original stays in memory.
    try: probe('RestoreSaved')
    except subprocess.CalledProcessError: pass
    raise
finally:
    if held:
        # Release only this task's synthetic modifiers even if focus changed; never leave them stuck.
        run('/usr/bin/ydotool', 'key', '--key-delay=0', '46:0', '56:0', '29:0')
