#!/usr/bin/python3
"""Isolated KWin/Wayland integration check. Run through heavy, never on the live display.

Usage: heavy python3 tests/stack-e2e.py /absolute/build/path
The outer process starts a private D-Bus session; all XDG state, offers and input stay there.
"""
import base64
import contextlib
import json
import os
import pathlib
import subprocess
import signal
import sys
import time
import tempfile
import shutil
import re
from PIL import Image

HERE = pathlib.Path(__file__).resolve().parent
BUILD = pathlib.Path(sys.argv[1]).resolve()
OUT = pathlib.Path(os.environ.get('QRO_STACK_OUT') or pathlib.Path.home() / '.cache/agent-scratch/desktop-config/clipboard-stack/e2e')
OUT.mkdir(parents=True, exist_ok=True)
TERMINAL_ONLY = os.environ.get('QRO_TERMINAL_ONLY') == '1'
# Keep the socket path short (sockaddr_un allows only 108 bytes), alongside the scratch build.
runtime = pathlib.Path(os.environ.get('QRO_STACK_RUNTIME') or tempfile.mkdtemp(prefix='r-', dir=BUILD.parent))
os.environ['QRO_STACK_RUNTIME'] = str(runtime)
runtime.mkdir(exist_ok=True, mode=0o700)
os.chmod(runtime, 0o700)
os.environ.update(XDG_RUNTIME_DIR=str(runtime), WAYLAND_DISPLAY='qro-stack-test-0',
                  XDG_CONFIG_HOME=str(OUT / 'config'), XDG_DATA_HOME=str(OUT / 'data'),
                  XDG_STATE_HOME=str(OUT / 'state'), XDG_CACHE_HOME=str(OUT / 'cache'),
                  QT_QPA_PLATFORM='wayland', KWIN_COMPOSE='O2', QT_QUICK_BACKEND='software',
                  XDG_CURRENT_DESKTOP='KDE', KDE_FULL_SESSION='true')
os.environ.pop('DISPLAY', None)
os.environ.pop('SESSION_MANAGER', None)
os.environ.pop('DBUS_STARTER_ADDRESS', None)
os.environ.pop('DBUS_STARTER_BUS_TYPE', None)
os.environ.pop('QT_WAYLAND_RECONNECT', None)
if '--inner' not in sys.argv:
    child = subprocess.Popen(['dbus-run-session', '--', sys.executable, str(__file__), str(BUILD), '--inner'], start_new_session=True)
    try: result = child.wait()
    finally:
        # Includes any portal/desktop service D-Bus activated in this test's private process group.
        try: os.killpg(child.pid, signal.SIGTERM)
        except ProcessLookupError: pass
        shutil.rmtree(runtime, ignore_errors=True)
    sys.exit(result)
(OUT / 'bus-address').write_text(os.environ['DBUS_SESSION_BUS_ADDRESS'])
os.environ['QT_LOGGING_RULES'] = 'org.kde.klipper.debug=true;kf.guiaddons.debug=true'
bin_dir = OUT / 'bin'
bin_dir.mkdir(exist_ok=True)
shim = bin_dir / 'ydotool'
shim.write_text('#!/bin/sh\nexec "' + str(BUILD / 'stack-input') + '" "$@"\n')
shim.chmod(0o700)
os.environ['PATH'] = str(bin_dir) + ':' + os.environ['PATH']
apps = OUT / 'data/applications'
apps.mkdir(parents=True, exist_ok=True)
(apps / 'quick-region-overlay.desktop').write_text('[Desktop Entry]\nType=Application\nName=Quick Region Overlay\nExec=' + str(BUILD / 'quick-region-overlay') + ' --capture\nNoDisplay=true\nX-KDE-DBUS-Restricted-Interfaces=org.kde.KWin.ScreenShot2\n')
(apps / 'qro-stack-input.desktop').write_text('[Desktop Entry]\nType=Application\nName=QRO isolated input test\nExec=' + str(BUILD / 'stack-input') + '\nNoDisplay=true\nX-KDE-Wayland-Interfaces=org_kde_kwin_fake_input\n')
(apps / 'qro-stack-probe.desktop').write_text('[Desktop Entry]\nType=Application\nName=QRO isolated clipboard probe\nExec=' + str(BUILD / 'stack-probe') + '\nNoDisplay=true\nX-KDE-DBUS-Restricted-Interfaces=org.kde.KWin.ScreenShot2\n')
config = OUT / 'config'
config.mkdir(exist_ok=True)
(config / 'kglobalshortcutsrc').write_text('[services][org.kde.spectacle.desktop]\n_launch=none\n')
(config / 'klipperrc').write_text('[General]\nMaxClipItems=40\nKeepClipboardContents=true\nIgnoreImages=false\n')
(config / 'quick-region-overlayrc').write_text('[Stack]\nNewStackAfterPaste=true\n')
STACK_DIR = OUT / 'state/quick-region-overlay/stack'
MARKER = STACK_DIR / 'pasted'
subprocess.run(['kbuildsycoca6', '--noincremental'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)

def run(*args, check=True):
    return subprocess.run(args, check=check, capture_output=True, text=True, timeout=15).stdout.strip()

def dbus(service, path, method, *args, check=True):
    return run('qdbus6', service, path, method, *map(str, args), check=check)

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
        time.sleep(0.05)
    raise RuntimeError('Timed out: ' + description)

def stop(process):
    if process.poll() is not None: return
    process.terminate()
    try: process.wait(timeout=6)
    except subprocess.TimeoutExpired:
        process.kill(); process.wait(timeout=5)

def input_keys(*keys):
    run(str(BUILD / 'stack-input'), *keys)

def shortcut(letter):
    assert stack('Focus') == 'qro-stack-probe', 'test editor not focused'
    code = {'c': 46, 's': 31, 'v': 47, 'x': 45}[letter]
    input_keys('29:1', '56:1', f'{code}:1', f'{code}:0', '56:0', '29:0')

def clipboard():
    probe('ReadClipboard')
    result = wait(lambda: json.loads(probe('Clipboard')) or None, 'clipboard snapshot', timeout=70)
    error = result.pop('_error')
    assert error == '', error
    return {key: base64.b64decode(value) for key, value in result.items()}

def history():
    result = subprocess.run(['qdbus6', 'org.kde.klipper', '/klipper', 'org.kde.klipper.klipper.getClipboardHistoryMenu'], capture_output=True, text=True, timeout=10)
    if result.returncode:
        (OUT / 'history-error').write_text(result.stderr + '\n' + result.stdout)
    return result.stdout.strip()

def prepare(text):
    probe('Prepare', text)
    if probe('FocusReady') != 'true':
        info = dbus('org.kde.KWin', '/KWin', 'org.kde.KWin.getWindowInfo', stack('FocusId'))
        geometry = dict(line.split(': ', 1) for line in info.splitlines() if ': ' in line)
        x, y = int(float(geometry['x'])) + 100, int(float(geometry['y'])) + 100
        run(str(BUILD / 'stack-input'), 'drag', str(x), str(y), str(x), str(y))
        probe('Prepare', text)
        wait(lambda: probe('FocusReady') == 'true', 'test editor keyboard focus')

def copy(text, expected):
    prepare(text)
    shortcut('c')
    wait(lambda: stack('Count') == str(expected), f'copy {expected}')

def osd_count(text):
    return sum(path.read_text().count('stack-osd ' + text + '\n') for path in OUT.glob('daemon*.log'))

def setting(enabled):
    run('kwriteconfig6', '--file', 'quick-region-overlayrc', '--group', 'Stack', '--key', 'NewStackAfterPaste', str(enabled).lower())

def pasted(count):
    return stack('State') == f'0:{count}' and MARKER.exists() and MARKER.read_text() == f'{count}\n'

def paste_text(count, menu=False):
    probe('ResetEvents')
    if menu: stack('Paste')
    else: shortcut('v')
    wait(lambda: len(json.loads(probe('Events'))) == 1, 'merged text received')
    wait(lambda: pasted(count), 'successful paste marker')

def capture(expected):
    shortcut('s')
    wait(lambda: any(w['resourceClass'] == 'quick-region-overlay' and w['width'] == 1024 for w in json.loads(probe('Windows'))), 'stack capture overlay')
    previous_count = stack('Count')
    stack('Clear'); stack('Paste')
    assert stack('Count') == previous_count, 'menu operation bypassed capture guard'
    run(str(BUILD / 'stack-input'), 'drag', '300', '300', '420', '380')
    wait(lambda: stack('Count') == str(expected), 'stack screenshot')

def observe_windows():
    script = OUT / 'observe-windows.js'
    script.write_text("""
var previous = '';
function report() {
    var rows = workspace.stackingOrder.map(function(w) {
        return {id: String(w.internalId), caption: w.caption, resourceClass: w.resourceClass,
                active: w.active, wantsInput: w.wantsInput, hidden: w.hidden, deleted: w.deleted,
                x: w.x, y: w.y, width: w.width, height: w.height};
    });
    var state = JSON.stringify(rows);
    if (state !== previous) {
        previous = state;
        callDBus('org.local.StackProbe', '/', 'org.local.StackProbe', 'ObserveWindows', state);
    }
}
workspace.windowActivated.connect(function(w) {
    callDBus('org.local.StackProbe', '/', 'org.local.StackProbe', 'ObserveActivation', w ? String(w.internalId) : '', w ? w.caption : '');
});
var timer = new QTimer(); timer.interval = 17; timer.timeout.connect(report); timer.start(); report();
""")
    script_id = dbus('org.kde.KWin', '/Scripting', 'org.kde.kwin.Scripting.loadScript', script, 'qro-test-windows')
    dbus('org.kde.KWin', '/Scripting/Script' + script_id, 'org.kde.kwin.Script.run')
    wait(lambda: probe('Windows'), 'window observer')

def wheels():
    # KWin exposes the daemon's app_id for layer surfaces, rather than the layer-shell scope/title.
    return [w for w in json.loads(probe('Windows')) if is_wheel(w)]

def is_wheel(window):
    return (window.get('resourceClass') == 'quick-region-overlay' and
            window['width'] == 18 and window['height'] == 18 and not window['hidden'])

def daemon_text():
    return current_daemon_log.read_text()

def log_time(text, event):
    return int(re.search(r'(\d{13}) ' + re.escape(event), text)[1])

def measured_paste(count, expected_path):
    offset = len(daemon_text())
    probe('ResetEvents'); shortcut('v')
    wait(lambda: 'stack-key paste' in daemon_text()[offset:], 'first paste key')
    wait(lambda: pasted(count), 'measured paste completed')
    text = daemon_text()[offset:]
    originals = re.findall(r'stack-paste-original (\w+) ms=(\d+)', text)
    assert len(originals) == 1 and originals[0][0] == expected_path, originals
    evidence = dict(path=expected_path, original_ms=int(originals[0][1]),
                    press_to_key_ms=log_time(text, 'stack-key paste') - log_time(text, 'stack-paste-pressed'),
                    release_to_key_ms=log_time(text, 'stack-key paste') - log_time(text, 'stack-paste-released'))
    return evidence

def wheel_at(x, y):
    current = wheels()
    return current[0] if len(current) == 1 and (current[0]['x'], current[0]['y']) == (x, y) else None

def spinner_checks():
    focus = stack('FocusId')
    # The existing 250 ms scenario remains uninstrumented for timing comparison. This longer read leaves
    # time for compositor screenshots and input checks while exercising the same paste path.
    for color, name in [('#f5f5f5', 'light'), ('#202124', 'dark')]:
        wait(lambda: not wheels(), 'previous busy wheel unmapped')
        probe('Background', color)
        run(str(BUILD / 'stack-input'), 'mouse', '400', '400')
        probe('PasteDelay', 800); probe('ResetEvents')
        activation_start = len(json.loads(probe('Activations')))
        shortcut('v')
        wheel = wait(lambda: wheel_at(412, 412), 'busy wheel during slow paste')
        assert not wheel['active'] and not wheel['wantsInput'], wheel
        assert wheel['width'] == 18 and wheel['height'] == 18, wheel
        assert (wheel['x'], wheel['y']) == (412, 412), wheel
        assert stack('FocusId') == focus and probe('Active') == 'true'
        full = OUT / ('wheel-' + name + '.png')
        assert probe('Screenshot', full) == 'true'
        with Image.open(full) as image:
            image.crop((398, 398, 444, 444)).resize((460, 460), Image.Resampling.NEAREST).save(OUT / ('wheel-' + name + '-crop.png'))
        run(str(BUILD / 'stack-input'), 'mouse', '1100', '400')
        wait(lambda: wheel_at(1112, 412), 'wheel moves to second output only')
        run(str(BUILD / 'stack-input'), 'mouse', '460', '420')
        moved = wait(lambda: wheel_at(472, 432), 'wheel returns to first output only')
        clicks = int(probe('Clicks'))
        # Motion and press share a Wayland batch: hit the old wheel before the throttled tracker moves it.
        run(str(BUILD / 'stack-input'), 'mouse', str(int(moved['x']) + 9), str(int(moved['y']) + 9), '1')
        run(str(BUILD / 'stack-input'), 'mouse', str(int(moved['x']) + 9), str(int(moved['y']) + 9), '0')
        wait(lambda: int(probe('Clicks')) == clicks + 1, 'pointer click passes through busy wheel')
        moved = wait(lambda: wheel_at(493, 453), 'wheel after click')
        scrolls = int(probe('Scrolls'))
        run(str(BUILD / 'stack-input'), 'wheel', str(int(moved['x']) + 9), str(int(moved['y']) + 9))
        wait(lambda: int(probe('Scrolls')) == scrolls + 1, 'scroll passes through busy wheel')
        wait(lambda: stack('State') == '0:4', 'slow paste with busy wheel finishes')
        wait(lambda: not wheels(), 'busy wheel unmapped on completion', timeout=0.1)
        wait(lambda: dbus('org.kde.KWin', '/Scripting', 'org.kde.kwin.Scripting.isScriptLoaded', 'quick-region-overlay-stack-pointer') == 'false', 'pointer script unloaded while idle')
        run(str(BUILD / 'stack-input'), 'mouse', '600', '500')
        assert dbus('org.kde.KWin', '/Scripting', 'org.kde.kwin.Scripting.isScriptLoaded', 'quick-region-overlay-stack-pointer') == 'false'
        assert not wheels(), 'idle pointer motion restarted busy tracking'
        events = json.loads(probe('Events'))
        assert len(events) == 3 and 'image/png' in events[1], events
        assert base64.b64decode(events[0]['text/plain']) == b'one\ntwo'
        assert base64.b64decode(events[2]['text/plain']) == b'three'
        activations = json.loads(probe('Activations'))[activation_start:]
        assert activations == [], activations
        assert stack('FocusId') == focus and probe('Active') == 'true'
        passed('busy wheel: ' + name + ' background, tracking, click/scroll passthrough, no activation, successful paste, immediate hide', wheel=wheel)
    probe('PasteDelay', 0)
    # Capture the area just occupied by the busy wheel. The frozen image must contain only the app background.
    probe('SaveClipboard')
    wait(lambda: probe('SavedState').startswith('ready:'), 'save clipboard before capture check')
    input_keys('99:1', '99:0')
    wait(lambda: any(w['resourceClass'] == 'quick-region-overlay' and w['width'] == 1024 for w in json.loads(probe('Windows'))), 'Print capture overlay')
    run(str(BUILD / 'stack-input'), 'drag', '530', '490', '570', '530')
    image = clipboard()['image/png']
    (OUT / 'capture-without-spinner.png').write_bytes(image)
    assert probe('ClipboardImageUniform', '#202124') == 'true', 'busy wheel leaked into frozen capture'
    probe('RestoreSaved')
    wait(lambda: stack('FocusId') == focus, 'focus returns after capture')
    passed('Print frozen image excludes previous busy wheel', crop='530,490 41x41')
    (OUT / 'window-events.json').write_text(probe('WindowEvents'))
    (OUT / 'activations.json').write_text(probe('Activations'))

results = []
def passed(name, **evidence):
    results.append(dict(check=name, **evidence))
    (OUT / ('results-terminal.json' if TERMINAL_ONLY else 'results.json')).write_text(json.dumps(results, indent=2))
    print(json.dumps({key: value for key, value in results[-1].items() if key != 'events'}), flush=True)

with contextlib.ExitStack() as cleanup:
    def start(name, args):
        global current_daemon_log
        log = cleanup.enter_context((OUT / (name + '.log')).open('a'))
        environment = os.environ.copy()
        if args[0] == str(BUILD / 'quick-region-overlay'):
            current_daemon_log = OUT / (name + '.log')
            environment['LD_PRELOAD'] = str(BUILD / 'libstack-modifiers.so')
            environment['QRO_STACK_SAVE_GATE'] = str(OUT / 'save-gate')
        process = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT, env=environment)
        cleanup.callback(stop, process)
        print(name, 'PID', process.pid, flush=True)
        return process
    kwin = start('kwin', ['kwin_wayland', '--virtual', '--width', '1024', '--height', '768', '--output-count', '2', '--socket', 'qro-stack-test-0', '--no-lockscreen', '--no-kactivities'])
    wait(lambda: (runtime / 'qro-stack-test-0').exists() and dbus('org.kde.KWin', '/KWin', 'org.kde.KWin.activeOutputName'), 'virtual KWin')
    keyboard = start('input-keeper', [str(BUILD / 'stack-input'), '--keepalive'])
    shell = start('klipper', [str(BUILD / 'stack-klipper')])
    wait(lambda: dbus('org.kde.klipper', '/klipper', 'org.freedesktop.DBus.Peer.Ping') == '', 'Klipper', timeout=20)
    (OUT / 'klipper-methods').write_text(run('qdbus6', 'org.kde.klipper', '/klipper', check=False))
    editor = start('probe', [str(BUILD / 'stack-probe')])
    def save_window_evidence():
        if editor.poll() is None and not TERMINAL_ONLY:
            (OUT / 'window-events.json').write_text(probe('WindowEvents'))
            (OUT / 'activations.json').write_text(probe('Activations'))
    cleanup.callback(save_window_evidence)
    # KWin 6.6 uses scripts.size() for D-Bus script IDs. Keep this persistent observer below the
    # daemon scripts, so removing/reloading its focus tracker cannot collide with our observer's ID.
    if not TERMINAL_ONLY: observe_windows()
    if os.environ.get('QRO_TRACE_POINTER') == '1':
        start('pointer-dbus', ['dbus-monitor', "member='CursorPosition'", "member='ActiveWindow'"])
    start('stack-signals', ['dbus-monitor', "type='signal',interface='org.local.QuickRegionOverlay.Stack',member='Changed'"])
    daemon = start('daemon', [str(BUILD / 'quick-region-overlay'), '--daemon'])
    wait(lambda: stack('State'), 'daemon')
    wait(lambda: stack('Focus') == 'qro-stack-probe', 'editor focus')
    # Existing isolated data from an interrupted run belongs to this harness.
    shortcut('x'); wait(lambda: stack('Count') == '0', 'initial clear')
    if not TERMINAL_ONLY:
        # Actual installed Plasma plugin, private bus/config: file edits alone are not a reload.
        dbus('org.kde.klipper', '/klipper', 'org.kde.klipper.klipper.clearClipboardHistory')
        run('kwriteconfig6', '--file', 'klipperrc', '--group', 'General', '--key', 'MaxClipItems', '2')
        run('kwriteconfig6', '--file', 'klipperrc', '--group', 'General', '--key', 'IgnoreImages', '--type', 'bool', 'true')
        for number in range(3):
            value = f'klipper reload {number}'
            probe('Custom', 'binary', value); probe('Offer')
            wait(lambda: value in history(), 'Klipper ordinary text')
        assert len(history().splitlines()) == 3, history()
        dbus('org.kde.klipper', '/klipper', 'org.kde.klipper.klipper.reloadConfig')
        wait(lambda: len(history().splitlines()) == 2, 'Klipper live history limit')
        text_history = history()
        probe('Custom', 'image', '#664422'); probe('Offer')
        wait(lambda: 'image/png' in clipboard(), 'ignored image offered')
        time.sleep(0.25)
        assert history() == text_history, history()
        run('kwriteconfig6', '--file', 'klipperrc', '--group', 'General', '--key', 'MaxClipItems', '40')
        run('kwriteconfig6', '--file', 'klipperrc', '--group', 'General', '--key', 'IgnoreImages', '--type', 'bool', 'false')
        dbus('org.kde.klipper', '/klipper', 'org.kde.klipper.klipper.reloadConfig')
        probe('Custom', 'image', '#224466'); probe('Offer')
        wait(lambda: history() != text_history, 'Klipper keeps image after live reload')
        passed('Klipper reloadConfig applies history limit and image preference live', before_reload=3, after_reload=2, images_kept=history())
        prepare('alpha')
        wait(lambda: not wheels(), 'initial clear wheel finishes')
        copy_offset = len(daemon_text())
        # Hold the actual fsync in the isolated daemon, rather than sleeping in the test driver.
        gate = OUT / 'save-gate'
        gate.touch()
        try:
            shortcut('c')
            wait(lambda: stack('State') == '9:0', 'copy persistence barrier')
            wheel = wait(lambda: wheels(), 'busy wheel during disk save')[0]
            assert not wheel['active'] and not wheel['wantsInput']
            assert probe('Active') == 'true'
        finally:
            gate.unlink(missing_ok=True)
        wait(lambda: stack('State') == '0:1', 'copy save completion')
        copy_log = daemon_text()[copy_offset:]
        immediate_ms = log_time(copy_log, 'stack-busy-shown') - log_time(copy_log, 'stack-copy-pressed')
        assert 0 <= immediate_ms < 140, immediate_ms
        wait(lambda: not wheels(), 'disk-save wheel finishes')
        passed('wheel maps immediately and remains responsive during copy persistence', show_after_press_ms=immediate_ms)
        shortcut('x'); wait(lambda: stack('State') == '0:0', 'clear slow warmup copy')
        wait(lambda: wheels(), 'warmup clear wheel maps')
        wait(lambda: not wheels(), 'warmup clear wheel finishes')
        copy_offset = len(daemon_text())
        copy('alpha', 1)
        wait(lambda: wheels(), 'fast copy wheel')
        assert clipboard()['text/plain'] == b'alpha'
        before = clipboard()
        assert stack('State') == '0:1' and wheels(), 'paste fixture must start during linger'
        wait(lambda: dbus('org.kde.KWin', '/Scripting', 'org.kde.kwin.Scripting.isScriptLoaded', 'quick-region-overlay-stack-pointer') == 'false', 'pointer tracking stops during ready linger')
        timing = measured_paste(1, 'cached')
        assert clipboard() == before
        assert timing['press_to_key_ms'] < 140, timing
        wait(lambda: not wheels(), 'wheel hides after minimum display')
        copy_log = daemon_text()[copy_offset:]
        visible_ms = log_time(copy_log, 'stack-busy-hidden') - log_time(copy_log, 'stack-busy-shown')
        assert visible_ms >= 390, visible_ms
        passed('minimum visibility, cached exact restore, paste during linger', visible_ms=visible_ms, **timing)
        probe('ResetEvents')
        input_keys('29:1', '47:1', '47:0', '29:0')
        first = json.loads(probe('Events'))
        assert base64.b64decode(first[0]['text/plain']) == b'alpha'
        setting(False); copy('alpha', 2); setting(True)
        passed('GUI copy, identical copies, normal paste', count=2)
        wait(lambda: not wheels(), 'copy wheel tail finishes')
        clear_offset = len(daemon_text())
        clear_start = time.monotonic()
        shortcut('x'); wait(lambda: stack('Count') == '0', 'clear')
        clear_ms = (time.monotonic() - clear_start) * 1000
        assert clear_ms < 150, ('fast-operation fixture took too long', clear_ms)
        wait(lambda: wheels(), 'fast clear receives visible feedback')
        wait(lambda: not wheels(), 'fast clear minimum visibility finishes')
        clear_log = daemon_text()[clear_offset:]
        assert log_time(clear_log, 'stack-busy-hidden') - log_time(clear_log, 'stack-busy-shown') >= 390
        assert dbus('org.kde.KWin', '/Scripting', 'org.kde.kwin.Scripting.isScriptLoaded', 'quick-region-overlay-stack-pointer') == 'false'
        passed('fast clear maps wheel for minimum visibility and unloads pointer script', elapsed_ms=round(clear_ms, 1))
        copy('one', 1); copy('two', 2)
        probe('Custom', 'image', '#ff0000'); shortcut('c'); wait(lambda: stack('Count') == '3', 'copy image')
        copy('three', 4)
        probe('Custom', 'binary', 'restore sentinel'); probe('Offer')
        time.sleep(0.2)
        wait(lambda: 'restore sentinel' in history(), 'Klipper observes ordinary copy', timeout=5)
        history_before = history()
        before = clipboard()
        timing = measured_paste(4, 'snapshot')
        events = json.loads(probe('Events'))
        assert len(events) == 3, events
        assert base64.b64decode(events[0]['text/plain']) == b'one\ntwo'
        assert 'image/png' in events[1]
        assert base64.b64decode(events[2]['text/plain']) == b'three'
        after = clipboard()
        assert after == before, (after, before)
        assert history() == history_before, (history(), history_before)
        passed('Klipper excludes intermediate offers', before=history_before, after=history())
        passed('text,text,image,text order; external selection invalidates cache and restores exactly', events=events, **timing)
        probe('PasteDelay', 250); probe('ResetEvents'); shortcut('v')
        wait(lambda: len(json.loads(probe('Events'))) == 3, 'delayed target reads')
        wait(lambda: stack('State') == '0:4', 'delayed paste completion')
        wait(lambda: pasted(4), 'persisted paste marker')
        delayed = json.loads(probe('Events'))
        assert base64.b64decode(delayed[0]['text/plain']) == b'one\ntwo'
        assert 'image/png' in delayed[1]
        assert base64.b64decode(delayed[2]['text/plain']) == b'three'
        passed('repeat paste while marked preserves the same text/image/text set', count=4)
        spinner_checks()
        probe('PasteDelay', 0); probe('IgnorePaste', 'true'); probe('ResetEvents'); shortcut('v')
        wait(lambda: stack('State').startswith('6:'), 'target offered item')
        wait(lambda: stack('State') == '0:4', 'no-read timeout')
        assert json.loads(probe('Events')) == []
        assert clipboard() == before
        probe('IgnorePaste', 'false')
        wait(lambda: not wheels(), 'busy wheel unmapped after failure', timeout=0.1)
        passed('read-driven pacing survives 250 ms target delay; no-read timeout restores clipboard')
        stop(daemon)
        daemon = start('daemon-restart', [str(BUILD / 'quick-region-overlay'), '--daemon'])
        wait(lambda: stack('State') == '0:4', 'persisted stack')
        wait(lambda: stack('Focus') == 'qro-stack-probe', 'focus script after daemon restart')
        assert pasted(4)
        passed('daemon restart preserves stack and pasted marker', count=4)
        nothing_before = osd_count('Nothing copied')
        probe('SuppressCopy', 'true'); shortcut('c')
        wait(lambda: osd_count('Nothing copied') > nothing_before, 'failed copy OSD')
        assert pasted(4), 'failed copy changed old stack or marker'
        probe('SuppressCopy', 'false')
        new_before = osd_count('New stack: 1 item')
        copy('fresh after paste', 1)
        wait(lambda: osd_count('New stack: 1 item') > new_before, 'new-stack OSD')
        assert not MARKER.exists()
        copy('second fresh item', 2)
        passed('successful copy replaces marked stack, OSD; failed copy preserves it', count=2)
        # Abort on a stack that has never been successfully pasted: no marker, and next copy still appends.
        probe('IgnorePaste', 'true'); shortcut('v')
        wait(lambda: stack('State').startswith('6:'), 'unread offer before abort')
        wait(lambda: stack('State') == '0:2', 'unmarked no-read timeout')
        assert not MARKER.exists()
        probe('IgnorePaste', 'false'); copy('after aborted paste', 3)
        paste_text(3)
        setting(False); copy('OFF appends after marked paste', 4)
        assert not MARKER.exists()
        paste_text(4)
        setting(False); copy('OFF still appends', 5)
        setting(True); copy('ON after OFF append remains unmarked', 6)
        passed('aborted paste does not mark; live OFF appends and retires previous marker; ON resumes', count=6)
        paste_text(6, menu=True)
        stack('Clear'); wait(lambda: stack('Count') == '0', 'D-Bus clear persisted stack')
        wait(lambda: 'member=Changed' in (OUT / 'stack-signals.log').read_text(), 'stack change signal')
        passed('D-Bus Paste, Clear, Changed signal share shortcut behaviour')
        assert not MARKER.exists()
        stop(daemon)
        daemon = start('daemon-cleared', [str(BUILD / 'quick-region-overlay'), '--daemon'])
        wait(lambda: stack('State') == '0:0', 'clear survives restart')
        wait(lambda: stack('Focus') == 'qro-stack-probe', 'focus script after cleared daemon restart')
        assert not MARKER.exists()
        passed('clear persists', count=0)
        passed('shortcuts', capture=dbus('org.local.QuickRegionOverlay', '/', 'org.local.QuickRegionOverlay.ShortcutStatus'), stack=dbus('org.local.QuickRegionOverlay', '/', 'org.local.QuickRegionOverlay.StackShortcutStatus'))
        # Screenshot variants: a real crop offer, and no append through the two original shortcuts.
        old_history = history()
        for count, color in enumerate(('#404142', '#545556', '#68696a'), 1):
            wait(lambda: not wheels(), 'previous wheel finished before region selection')
            probe('Background', color)
            offset = len(daemon_text())
            capture(count)
            wait(lambda: wheels(), 'stack screenshot wheel appears after overlay closes')
            text = daemon_text()[offset:]
            show_ms = log_time(text, 'stack-busy-shown') - log_time(text, 'stack-capture-closed')
            assert 0 <= show_ms < 140, show_ms
        before = clipboard()
        assert set(before) == {'image/png'}, before.keys()
        wait(lambda: history() != old_history, 'Klipper records stack screenshot')
        screenshot_history = history()
        samples = []
        for repeat in range(3):
            samples.append(measured_paste(3, 'owned'))
            assert len(json.loads(probe('Events'))) == 3
            assert clipboard() == before
            assert history() == screenshot_history, (history(), screenshot_history)
        passed('three stack screenshots prepare owned paste and exact restore; Klipper records add and excludes intermediate offers', samples=samples)
        probe('SaveClipboard')
        wait(lambda: probe('SavedState').startswith('ready:'), 'save screenshot for same-byte comparison')
        same_bytes = []
        for repeat in range(3):
            probe('RestoreSaved')
            assert clipboard() == before
            same_bytes.append(measured_paste(3, 'snapshot'))
            assert clipboard() == before
        passed('identical screenshot bytes from another owner still invalidate preparation', samples=same_bytes)
        fallback_samples = []
        for repeat in range(3):
            probe('Custom', 'binary', 'external change after screenshots ' + str(repeat)); probe('Offer')
            replacement = clipboard()
            fallback_samples.append(measured_paste(3, 'snapshot'))
            assert clipboard() == replacement
        passed('external client changes after image add force snapshot and exact restoration', samples=fallback_samples)
        for sequence in [('99:1', '99:0'), ('125:1', '42:1', '31:1', '31:0', '42:0', '125:0')]:
            assert stack('Focus') == 'qro-stack-probe'
            input_keys(*sequence); time.sleep(0.3)
            run(str(BUILD / 'stack-input'), 'drag', '320', '320', '440', '400')
            time.sleep(0.2)
            assert stack('Count') == '3'
            assert 'image/png' in clipboard()
        passed('Ctrl+Alt+S appends; Print and Meta+Shift+S retain normal capture behavior')
        shortcut('x'); wait(lambda: stack('Count') == '0', 'clear before Konsole')
        for count in range(1, 101): copy('cap test', count)
        stack_dir = STACK_DIR
        assert len(list(stack_dir.glob('*.qrs'))) == 100
        assert stack_dir.stat().st_mode & 0o777 == 0o700
        assert all(path.stat().st_mode & 0o777 == 0o600 for path in stack_dir.glob('*.qrs'))
        probe('Prepare', 'overflow refused'); shortcut('c')
        time.sleep(0.1)
        assert stack('Count') == '100'
        passed('100-item cap refuses overflow; exact state path and private permissions')
        paste_text(100)
        assert MARKER.stat().st_mode & 0o777 == 0o600
        copy('new stack bypasses copy cap', 1)
        assert not MARKER.exists()
        for count in range(2, 101): copy('capture cap test', count)
        paste_text(100)
        new_before = osd_count('New stack: 1 item')
        capture(1)
        wait(lambda: osd_count('New stack: 1 item') > new_before, 'new screenshot stack OSD')
        assert not MARKER.exists()
        assert 'image/png' in clipboard()
        passed('full marked stack permits new copy and new screenshot, each replaces with one item', count=1)
        shortcut('x'); wait(lambda: stack('Count') == '0', 'clear capped stack')
    terminal_file = OUT / 'terminal-input'
    terminal_file.write_bytes(b'')
    terminal_file.with_suffix('.ready').unlink(missing_ok=True)
    console = start('konsole', ['konsole', '--separate', '--nofork', '--hide-menubar', '--hide-tabbar', '-e', sys.executable, str(HERE / 'stack-terminal.py'), str(terminal_file)])
    wait(lambda: stack('Focus') in ('konsole', 'org.kde.konsole'), 'Konsole focus')
    wait(lambda: terminal_file.with_suffix('.ready').exists(), 'Konsole child output')
    console_service = 'org.kde.konsole-' + str(console.pid)
    wait(lambda: 'konsole-selected' in dbus(console_service, '/Sessions/1', 'org.kde.konsole.Session.getAllDisplayedText', 'true'), 'Konsole displays child output')
    (OUT / 'konsole-dbus-paths').write_text(run('qdbus6', 'org.kde.konsole-' + str(console.pid), check=False))
    # Select the printed line with the mouse. Query KWin, since Wayland QWidget geometry has no global position.
    info = dbus('org.kde.KWin', '/KWin', 'org.kde.KWin.getWindowInfo', stack('FocusId'))
    geometry = dict(line.split(': ', 1) for line in info.splitlines() if ': ' in line)
    x, y = int(float(geometry['x'])), int(float(geometry['y']))
    run(str(BUILD / 'stack-input'), 'drag', str(x + 6), str(y + 40), str(x + 200), str(y + 40))
    try:
        input_keys('29:1', '42:1', '46:1', '46:0', '42:0', '29:0')
        selected = clipboard()
        assert selected.get('text/plain', selected.get('text/plain;charset=utf-8', b'')).strip() == b'konsole-selected', 'Konsole selection setup failed'
        run(str(BUILD / 'stack-input'), 'drag', str(x + 6), str(y + 40), str(x + 200), str(y + 40))
        input_keys('29:1', '56:1', '46:1', '46:0', '56:0', '29:0')
        wait(lambda: stack('Count') == '1', 'Konsole copy')
    except (RuntimeError, AssertionError):
        for path in ['/Sessions/1', '/Windows/1', '/konsole/MainWindow_1', '/konsole/MainWindow_1/actions']:
            (OUT / ('konsole-' + path.replace('/', '_') + '.methods')).write_text(run('qdbus6', console_service, path, check=False))
        run('spectacle', '-b', '-n', '-f', '-o', str(OUT / 'konsole-selection.png'), check=False)
        raise
    term_copy = clipboard()
    assert term_copy.get('text/plain', term_copy.get('text/plain;charset=utf-8', b'')).strip() == b'konsole-selected', term_copy
    assert terminal_file.read_bytes() == b'', terminal_file.read_bytes()
    input_keys('29:1', '56:1', '47:1', '47:0', '56:0', '29:0')
    wait(lambda: stack('State') == '0:1', 'Konsole paste')
    wait(lambda: b'konsole-selected' in terminal_file.read_bytes(), 'terminal receives text')
    passed('Konsole Ctrl+Shift+C/V, no interrupt', received=terminal_file.read_bytes().decode(errors='replace'))
    stop(console)
    wait(lambda: stack('Focus') == 'qro-stack-probe', 'editor focus after Konsole')
    shortcut('x'); wait(lambda: stack('Count') == '0', 'clear terminal selection')
    copy('terminal receiver text', 1)
    probe('Custom', 'image', '#0080ff'); shortcut('c'); wait(lambda: stack('Count') == '2', 'image for terminal receiver')
    probe('Quit'); editor.wait(timeout=5)
    editor = start('terminal-probe', [str(BUILD / 'stack-probe'), '--terminal'])
    wait(lambda: stack('Focus') in ('konsole', 'org.kde.konsole'), 'terminal-class MIME receiver')
    probe('Custom', 'binary', 'terminal restore'); probe('Offer'); time.sleep(0.1)
    input_keys('29:1', '56:1', '47:1', '47:0', '56:0', '29:0')
    wait(lambda: len(json.loads(probe('Events'))) == 2, 'terminal text/image paste')
    wait(lambda: stack('State') == '0:2', 'terminal image paste completion')
    routed = json.loads(probe('Events'))
    assert routed[0]['_key'] == 'Ctrl+Shift+V' and base64.b64decode(routed[0]['text/plain']) == b'terminal receiver text'
    assert routed[1]['_key'] == 'Ctrl+V' and 'image/png' in routed[1]
    passed('terminal text uses Ctrl+Shift+V; terminal image uses Ctrl+V', events=routed)
    input_keys('29:1', '56:1', '45:1', '45:0', '56:0', '29:0')
    wait(lambda: stack('Count') == '0', 'final clear')
print('PASS; logs and results:', OUT, flush=True)
