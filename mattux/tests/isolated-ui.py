#!/usr/bin/python3
"""Manual integration test: heavy python3 isolated-ui.py /absolute/mattux/build.

All widgets, key events, D-Bus names, configuration and service changes are private.
The screenshot service is a mock; daemon paste is covered by screenshot's stack E2E.
"""
import contextlib
import json
import os
import pathlib
import shutil
import signal
import subprocess
import sys
import tempfile
import time

HERE = pathlib.Path(__file__).resolve()
BUILD = pathlib.Path(sys.argv[1]).resolve()
OUT = pathlib.Path(os.environ.get('MATTUX_TEST_OUT', pathlib.Path.home() / '.cache/agent-scratch/desktop-config/mattux-ui'))
OUT.mkdir(parents=True, exist_ok=True)

if '--inner' not in sys.argv and '--fake-stack' not in sys.argv:
    runtime = pathlib.Path(tempfile.mkdtemp(prefix='r-', dir=BUILD.parent))
    os.chmod(runtime, 0o700)
    environment = os.environ.copy()
    environment.update(XDG_RUNTIME_DIR=str(runtime), WAYLAND_DISPLAY='mattux-test-0',
                       XDG_CONFIG_HOME=str(OUT / 'config'), XDG_DATA_HOME=str(OUT / 'data'),
                       XDG_STATE_HOME=str(OUT / 'state'), XDG_CACHE_HOME=str(OUT / 'cache'),
                       QT_QPA_PLATFORM='wayland', KWIN_COMPOSE='O2', QT_QUICK_BACKEND='software',
                       XDG_CURRENT_DESKTOP='KDE', KDE_FULL_SESSION='true',
                       MATTUX_ISOLATED_TEST='1', MATTUX_TEST_OUT=str(OUT))
    for name in ('DISPLAY', 'SESSION_MANAGER', 'DBUS_STARTER_ADDRESS', 'DBUS_STARTER_BUS_TYPE', 'QT_WAYLAND_RECONNECT'):
        environment.pop(name, None)
    config = OUT / 'config'
    config.mkdir(exist_ok=True)
    (config / 'kglobalshortcutsrc').write_text('[services][org.kde.spectacle.desktop]\n_launch=none\n')
    child = subprocess.Popen(['dbus-run-session', '--', sys.executable, str(HERE), str(BUILD), '--inner'],
                             env=environment, start_new_session=True)
    (OUT / 'processes.jsonl').open('a').write(json.dumps({'name': 'private-bus-group', 'pid': child.pid}) + '\n')
    try:
        result = child.wait()
    finally:
        try:
            os.killpg(child.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        shutil.rmtree(runtime, ignore_errors=True)
    sys.exit(result)

import dbus
from dbus.mainloop.glib import DBusGMainLoop
DBusGMainLoop(set_as_default=True)
assert os.environ['MATTUX_ISOLATED_TEST'] == '1'
assert pathlib.Path(os.environ['XDG_CONFIG_HOME']) == OUT / 'config'
BUS = dbus.SessionBus()

if '--fake-stack' in sys.argv:
    import dbus.service
    from gi.repository import GLib
    BUS = dbus.SessionBus()
    daemon_name = 'org.local.QuickRegionOverlay'
    interface = daemon_name + '.Stack'

    class Stack(dbus.service.Object):
        count = 8

        @dbus.service.method(interface, out_signature='s')
        def State(self):
            with (OUT / 'stack-calls.log').open('a') as log:
                log.write('State\n')
            return '0:' + str(self.count)

        @dbus.service.method(interface)
        def Clear(self):
            self.count = 0
            self.Changed(self.count, '0:0')

        @dbus.service.method(interface)
        def Paste(self):
            pass

        @dbus.service.signal(interface, signature='is')
        def Changed(self, count, state):
            pass

    class Control(dbus.service.Object):
        @dbus.service.method('org.local.MattuxTestDaemon', in_signature='b')
        def SetRunning(self, running):
            if running:
                BUS.request_name(daemon_name)
            else:
                BUS.release_name(daemon_name)

    class Watcher(dbus.service.Object):
        items = []

        @dbus.service.method('org.kde.StatusNotifierWatcher', in_signature='s', sender_keyword='sender')
        def RegisterStatusNotifierItem(self, service, sender=None):
            self.items.append(str(sender) + service if service.startswith('/') else service + '/StatusNotifierItem')

        @dbus.service.method('org.freedesktop.DBus.Properties', in_signature='ss', out_signature='v')
        def Get(self, interface, name):
            return self.GetAll(interface)[name]

        @dbus.service.method('org.freedesktop.DBus.Properties', in_signature='s', out_signature='a{sv}')
        def GetAll(self, interface):
            return {'IsStatusNotifierHostRegistered': dbus.Boolean(True), 'ProtocolVersion': dbus.Int32(0),
                    'RegisteredStatusNotifierItems': dbus.Array(self.items, signature='s')}

    BUS.request_name('org.kde.StatusNotifierWatcher')
    BUS.request_name('org.local.MattuxTestDaemon')
    BUS.request_name(daemon_name)
    stack = Stack(BUS, '/Stack')
    control = Control(BUS, '/Control')
    watcher = Watcher(BUS, '/StatusNotifierWatcher')
    GLib.MainLoop().run()
    sys.exit(0)

def wait(predicate, description, timeout=10):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        try:
            result = predicate()
            if result:
                return result
        except (dbus.DBusException, ValueError, StopIteration):
            pass
        time.sleep(0.05)
    raise RuntimeError('Timed out: ' + description)

def clean(node):
    return {'id': int(node[0]), 'props': {str(k): str(v) for k, v in node[1].items() if k != 'icon-data'},
            'children': [clean(child) for child in node[2]]}

def flatten(node):
    yield node
    for child in node[2]:
        yield from flatten(child)

def properties():
    return BUS.get_object(sni, '/StatusNotifierItem').GetAll('org.kde.StatusNotifierItem',
                                                            dbus_interface='org.freedesktop.DBus.Properties')

def layout():
    return menu.GetLayout(0, -1, [])[1]

def find(label):
    return next(node for node in flatten(layout()) if str(node[1].get('label', '')).split('\t')[0] == label)

def event(label):
    menu.Event(find(label)[0], 'clicked', dbus.Int32(0, variant_level=1), dbus.UInt32(0))

def passed(name, **details):
    entry = dict(check=name, **details)
    with (OUT / 'results.jsonl').open('a') as log:
        log.write(json.dumps(entry) + '\n')
    print(json.dumps(entry), flush=True)

with contextlib.ExitStack() as cleanup:
    def start(name, args):
        log = cleanup.enter_context((OUT / (name + '.log')).open('w'))
        process = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT)
        def stop():
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
        cleanup.callback(stop)
        with (OUT / 'processes.jsonl').open('a') as record:
            record.write(json.dumps({'name': name, 'pid': process.pid}) + '\n')
        print(name, 'PID', process.pid, flush=True)
        return process

    runtime = pathlib.Path(os.environ['XDG_RUNTIME_DIR'])
    start('kwin', ['kwin_wayland', '--virtual', '--width', '1024', '--height', '768', '--socket',
                   'mattux-test-0', '--no-lockscreen', '--no-kactivities'])
    wait(lambda: (runtime / 'mattux-test-0').exists() and BUS.name_has_owner('org.kde.KWin'), 'private KWin')
    test_args = os.environ.get('MATTUX_TEST_FUNCTION', '').split()
    test = start('shortcut-dialog-test', [str(BUILD / 'shortcut-dialog-test'), *test_args])
    assert test.wait(timeout=100) == 0, (OUT / 'shortcut-dialog-test.log').read_text()
    passed('shortcut recording, Defaults, Cancel, conflict Cancel/Reassign, foreign save and close/hide cleanup')
    test = start('quit-recording-test', [str(BUILD / 'shortcut-dialog-test'), '--quit-test'])
    assert test.wait(timeout=30) == 0, 'application quit left recording active'
    passed('application quit cancels active shortcut recording')

    # Test Enabled through the real menu with a private service controller.
    enabled_file = OUT / 'enabled'
    enabled_file.write_text('enabled')
    bin_dir = OUT / 'bin'
    bin_dir.mkdir(exist_ok=True)
    shim = bin_dir / 'systemctl'
    shim.write_text('''#!/usr/bin/python3
import dbus, os, pathlib, sys
out = pathlib.Path(os.environ['MATTUX_TEST_OUT'])
args = sys.argv[1:]
assert args[0] == '--user' and args[-1] == 'quick-region-overlay.service', args
with (out / 'systemctl.log').open('a') as log: log.write(' '.join(args) + '\\n')
if args[1] == 'is-enabled':
    state = (out / 'enabled').read_text()
    print(state)
    sys.exit(0 if state == 'enabled' else 1)
assert args[1] in ('enable', 'disable') and args[2] == '--now', args
on = args[1] == 'enable'
dbus.SessionBus().get_object('org.local.MattuxTestDaemon', '/Control').SetRunning(on, dbus_interface='org.local.MattuxTestDaemon')
(out / 'enabled').write_text('enabled' if on else 'disabled')
''')
    shim.chmod(0o700)
    os.environ['PATH'] = str(bin_dir) + ':' + os.environ['PATH']
    start('mock-stack', [sys.executable, str(HERE), str(BUILD), '--fake-stack'])
    wait(lambda: BUS.name_has_owner('org.local.QuickRegionOverlay'), 'mock daemon')
    render = start('menu-render', [str(BUILD / 'shortcut-dialog-test'), '--menu-render'])
    assert render.wait(timeout=20) == 0, 'native menu rendering failed'
    tray = start('mattux', [str(BUILD / 'mattux')])
    wait(lambda: BUS.name_has_owner('org.local.Mattux'), 'Mattux')
    def find_sni():
        items = BUS.get_object('org.kde.StatusNotifierWatcher', '/StatusNotifierWatcher').Get(
            'org.kde.StatusNotifierWatcher', 'RegisteredStatusNotifierItems', dbus_interface='org.freedesktop.DBus.Properties')
        for item in items:
            name = str(item).partition('/')[0]
            if BUS.name_has_owner(name) and BUS.get_object(name, '/StatusNotifierItem').Get(
                    'org.kde.StatusNotifierItem', 'Title', dbus_interface='org.freedesktop.DBus.Properties') == 'Mattux':
                return name
    sni = wait(find_sni, 'Mattux SNI')
    menu = dbus.Interface(BUS.get_object(sni, str(properties()['Menu'])), 'com.canonical.dbusmenu')
    wait(lambda: str(properties()['ToolTip'][3]) == '8 items', 'stack badge count')
    title = find('Screenshot & clipboard')
    assert title[1]['x-kde-title'] and not title[1]['enabled'], title
    history = find('Clipboard history')
    history_title = next(node for node in flatten(history) if node[1].get('label') == 'History size')
    assert history_title[1]['x-kde-title'], history_title
    (OUT / 'menu-enabled.json').write_text(json.dumps(clean(layout()), indent=2))
    assert str(find('Region screenshot…')[1]['label']).endswith('\tMeta+Shift+S, Print'), str(find('Region screenshot…')[1]['label'])
    enabled_icon = bytes(properties()['IconPixmap'][0][2])
    passed('exported KDE section titles, literal ampersand, both shortcut texts, badge count 8')
    event('Enabled')
    wait(lambda: str(properties()['ToolTip'][3]) == 'Not running', 'disabled icon')
    wait(lambda: enabled_file.read_text() == 'disabled' and not BUS.name_has_owner('org.local.QuickRegionOverlay'), 'service disabled')
    wait(lambda: 'enabled' not in find('Enabled')[1], 'service toggle completion')
    assert not find('Paste stack')[1]['enabled'] and not find('Clear stack')[1]['enabled']
    disabled_icon = bytes(properties()['IconPixmap'][0][2])
    assert disabled_icon != enabled_icon and max(disabled_icon[0::4]) <= 103
    calls = (OUT / 'stack-calls.log').read_text()
    for _ in range(3):
        menu.AboutToShow(0)
        time.sleep(0.15)
    assert (OUT / 'stack-calls.log').read_text() == calls, 'disabled Mattux called daemon'
    assert not BUS.name_has_owner('org.local.QuickRegionOverlay'), 'disabled menu activated daemon'
    (OUT / 'menu-disabled.json').write_text(json.dumps(clean(layout()), indent=2))
    passed('Enabled disables persistently, dims icon, suppresses daemon calls on repeated refresh')
    event('Enabled')
    wait(lambda: str(properties()['ToolTip'][3]) == '8 items', 'enabled again')
    assert enabled_file.read_text() == 'enabled'
    passed('Enabled restores daemon and stack count')
    event('Clear stack')
    wait(lambda: str(properties()['ToolTip'][3]) == 'Empty', 'Changed updates empty icon')
    assert not find('Paste stack')[1]['enabled']
    passed('Changed updates badge and actions without polling')
    event('Quit')
    assert tray.wait(timeout=5) == 0
    assert not BUS.name_has_owner('org.local.Mattux')
    passed('Quit exits Mattux')
print('PASS; evidence:', OUT, flush=True)
