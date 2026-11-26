#include "stack-controller.h"
#include "stack-holder.h"
#include "stack-storage.h"
#include "daemon-logic.h"
#include <QBuffer>
#include <QDateTime>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QImage>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <limits>

using namespace ClipboardStack;
namespace {
// EVIOCGKEY is a state query, not an event reader: no keystrokes are collected or retained.
enum class ModifierState { Released, Held, Unavailable };
// Closing an evdev fd waits for an RCU grace period (~16 ms each; ~450 ms for 28 devices), so device fds stay
// open between queries. EVIOCSMASK with an empty type mask stops the kernel queueing any events for them.
struct InputDevice { ino_t inode = 0; int fd = -1; bool keyboard = false; bool writable = false; };
QHash<QString, InputDevice> inputDevices;
// The last probe's writable keyboard with a modifier held, or -1; key() writes the copy/paste keys into it.
int heldKeyboard = -1;
ModifierState physicalModifiers() {
    bool keyboard = false;
    heldKeyboard = -1;
    constexpr int modifierKeys[]{KEY_LEFTCTRL, KEY_RIGHTCTRL, KEY_LEFTALT, KEY_RIGHTALT, KEY_LEFTSHIFT, KEY_RIGHTSHIFT, KEY_LEFTMETA, KEY_RIGHTMETA};
    const auto bit = [](const unsigned long *bits, int key) { return (bits[key / (8 * sizeof(long))] >> (key % (8 * sizeof(long)))) & 1UL; };
    const auto drop = [](InputDevice &device) { if (device.fd >= 0) close(device.fd); device.fd = -1; };
    QSet<QString> present;
    for (const auto &name : QDir(QStringLiteral("/dev/input")).entryList({QStringLiteral("event*")}, QDir::System)) {
        const QByteArray path = QFile::encodeName(QStringLiteral("/dev/input/") + name);
        struct stat info {};
        if (stat(path.constData(), &info) != 0) continue;
        present.insert(name);
        auto &device = inputDevices[name];
        if (device.inode != info.st_ino) {
            // New or replugged node: (re)probe it once.
            drop(device);
            device = InputDevice{info.st_ino};
            int fd = open(path.constData(), O_RDWR | O_CLOEXEC | O_NONBLOCK);
            device.writable = fd >= 0;
            if (fd < 0) fd = open(path.constData(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
            if (fd < 0) { device.inode = 0; continue; }
            unsigned long supported[(KEY_MAX + 8 * sizeof(long)) / (8 * sizeof(long))] = {};
            if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof supported), supported) >= 0)
                for (int key : modifierKeys) device.keyboard = device.keyboard || bit(supported, key);
            unsigned long noTypes[(EV_MAX + 8 * sizeof(long)) / (8 * sizeof(long))] = {};
            input_mask mask{0, sizeof noTypes, quint64(reinterpret_cast<quintptr>(noTypes))};
            if (device.keyboard && ioctl(fd, EVIOCSMASK, &mask) >= 0) device.fd = fd;
            else close(fd); // Not a keyboard, or events could not be masked: never hold it open.
        }
        if (!device.keyboard) continue;
        int fd = device.fd;
        const bool transient = fd < 0;
        if (transient) fd = open(path.constData(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
        if (fd < 0) { device.inode = 0; continue; }
        unsigned long state[(KEY_MAX + 8 * sizeof(long)) / (8 * sizeof(long))] = {};
        const bool ok = ioctl(fd, EVIOCGKEY(sizeof state), state) >= 0;
        const int error = errno;
        if (transient) close(fd);
        if (!ok) {
            if (error == ENODEV) { drop(device); device.inode = 0; continue; }
            return ModifierState::Unavailable;
        }
        keyboard = true;
        for (int key : modifierKeys) {
            if (!bit(state, key)) continue;
            if (!transient && device.writable) heldKeyboard = device.fd;
            return ModifierState::Held;
        }
    }
    for (auto it = inputDevices.begin(); it != inputDevices.end();) {
        if (present.contains(it.key())) { ++it; continue; }
        drop(it.value());
        it = inputDevices.erase(it);
    }
    return keyboard ? ModifierState::Released : ModifierState::Unavailable;
}
bool writeShortcutKeys(int fd, bool copy, bool shift) {
    unsigned long state[(KEY_MAX + 8 * sizeof(long)) / (8 * sizeof(long))] = {};
    if (ioctl(fd, EVIOCGKEY(sizeof state), state) < 0) return false;
    const auto down = [&state](int key) { return (state[key / (8 * sizeof(long))] >> (key % (8 * sizeof(long)))) & 1UL; };
    const auto ctrl = DaemonLogic::freeModifierKey(down(KEY_LEFTCTRL), KEY_LEFTCTRL, KEY_RIGHTCTRL);
    const auto shiftKey = DaemonLogic::freeModifierKey(down(KEY_LEFTSHIFT), KEY_LEFTSHIFT, KEY_RIGHTSHIFT);
    QList<input_event> events;
    for (const auto &key : DaemonLogic::shortcutKeyEvents(copy, shift, ctrl, shiftKey, DaemonLogic::restoredModifiers(down))) {
        input_event event{};
        event.type = key.type;
        event.code = key.code;
        event.value = key.value;
        events.append(event);
    }
    const auto bytes = qsizetype(events.size() * sizeof(input_event));
    return write(fd, events.constData(), size_t(bytes)) == bytes;
}
QDBusMessage call(const QString &service, const QString &path, const QString &interface, const QString &method) {
    return QDBusMessage::createMethodCall(service, path, interface, method);
}
bool locked() {
    const auto reply = QDBusConnection::sessionBus().call(call(QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("/ScreenSaver"), QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("GetActive")), QDBus::Block, 500);
    // A missing service is normal in a virtual session without a lock screen. Other failures are uncertain state:
    // refuse input rather than risk pasting into a lock screen while its service is not responding.
    if (reply.type() == QDBusMessage::ErrorMessage)
        return reply.errorName() != QLatin1String("org.freedesktop.DBus.Error.ServiceUnknown") && reply.errorName() != QLatin1String("org.freedesktop.DBus.Error.NameHasNoOwner");
    return reply.arguments().size() != 1 || reply.arguments()[0].metaType() != QMetaType::fromType<bool>() || reply.arguments()[0].toBool();
}
}

StackController::StackController(QObject *parent) : QObject(parent), clipboard(this) {
    storage.setMaxThreadCount(1);
    storage.setExpiryTimeout(1000);
    // Probe input devices once at startup: closing the non-keyboards takes ~0.5 s, which would otherwise delay the first stack shortcut.
    QTimer::singleShot(0, this, [] { physicalModifiers(); });
    qRegisterMetaType<ClipboardStack::Item>();
    directory = stateDirectory();
    cleanupClipboardHolders();
    load();
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/Stack"), this, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals);
    deadline.setSingleShot(true);
    grace.setSingleShot(true);
    modifiers.setInterval(15);
    connect(&modifiers, &QTimer::timeout, this, [this] {
        if (modifiers.interval() != 15) modifiers.setInterval(15);
        const auto state = physicalModifiers();
        if (state == ModifierState::Released || (state == ModifierState::Held && heldKeyboard >= 0)) {
            modifiers.stop();
            if (!sameTarget() || locked()) { finish(QStringLiteral("Focus changed")); return; }
            auto next = std::move(afterModifiers);
            next();
        } else if (state == ModifierState::Unavailable || modifierTime.elapsed() >= 1500) {
            finish(state == ModifierState::Unavailable ? QStringLiteral("Keyboard state unavailable") : QStringLiteral("Release modifier keys"));
        }
    });
    connect(&keys, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        keyRunning = false;
        if (code != 0 || status != QProcess::NormalExit) {
            releaseSyntheticKeys();
            if (operation == Operation::Pasting) restore(QStringLiteral("Paste key failed"));
            else finish(QStringLiteral("Copy key failed"));
            return;
        }
        auto next = std::move(afterKeys);
        if (next) next();
    });
    connect(&keys, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        keyRunning = false;
        releaseSyntheticKeys();
        if (operation == Operation::Pasting) restore(QStringLiteral("ydotool unavailable"));
        else finish(QStringLiteral("ydotool unavailable"));
    });
    connect(&deadline, &QTimer::timeout, this, [this] {
        if (keyRunning) { keys.kill(); return; }
        if (operation == Operation::Copying) finish(QStringLiteral("Nothing copied"));
        else if (operation == Operation::Pasting || operation == Operation::Offering) restore(QStringLiteral("Paste stopped: no read"));
    });
    connect(&clipboard, &StackClipboard::changed, this, [this] {
        if (operation == Operation::Copying) {
            copyChanged = true;
            if (!keyRunning) { setOperation(Operation::CopySnapshot); deadline.stop(); takeSnapshot(); }
        }
    });
    connect(&clipboard, &StackClipboard::snapshotReady, this, [this](quint64 id, const Item &item, const QString &error) {
        if (id != serial) return;
        if (!error.isEmpty()) { finish(error); return; }
        if (operation == Operation::CopySnapshot) {
            if (!restorePending) original.clear();
            if (item.isEmpty()) finish(QStringLiteral("Nothing copied")); else append(item);
        }
    });
    connect(&clipboard, &StackClipboard::originalReady, this, [this](quint64 id, const Item &item, const QString &error, const QString &path, qint64 ms) {
        if (id != serial || operation != Operation::RestoreSnapshot) return;
        qInfo().noquote() << QDateTime::currentMSecsSinceEpoch() << "stack-paste-original" << path << QStringLiteral("ms=%1").arg(ms);
        if (!error.isEmpty()) { finish(error); return; }
        original = item;
        restorePending = true;
        pasteItems = plan(items);
        pasteIndex = 0;
        nextPaste();
    });
    connect(&clipboard, &StackClipboard::offered, this, [this](quint64 id, bool ok) {
        if (id != serial) return;
        if (operation == Operation::PreparingImage) {
            const auto item = std::move(pendingImage);
            // Persistence already committed: keep memory and disk aligned even if ownership failed.
            appended(item, pendingImageMode, {});
            if (!ok) osd(QStringLiteral("Saved to stack; clipboard offer failed"));
            return;
        }
        if (operation == Operation::Restoring) {
            if (ok) restorePending = false;
            if (ok && pasteSucceeded) {
                const QString path = directory, message = completion;
                const qsizetype count = items.size();
                storage.start([this, path, count, message] {
                    const bool saved = savePasteMarker(path, count);
                    QMetaObject::invokeMethod(this, [this, saved, message] {
                        if (operation == Operation::Stopping) return;
                        if (saved) pasted = true;
                        finish(saved ? message : message + QStringLiteral("; cannot save stack marker"));
                    }, Qt::QueuedConnection);
                });
                return;
            }
            finish(ok ? completion : QStringLiteral("Clipboard restore failed")); return;
        }
        if (operation != Operation::Offering) return;
        if (!ok) { restore(QStringLiteral("Clipboard offer failed")); return; }
        if (!sameTarget() || locked()) { restore(QStringLiteral("Focus changed")); return; }
        setOperation(Operation::Pasting);
        readSeen = false;
        key(false, kind(pasteItems[pasteIndex]) == Kind::Image, [this] {
            if (readSeen) grace.start(100);
        });
    });
    connect(&clipboard, &StackClipboard::consumed, this, [this](quint64 id, const QString &format) {
        if (id != serial || operation != Operation::Pasting) return;
        readSeen = true;
        qInfo().noquote() << "stack-consumed" << pasteIndex << format;
        if (!keyRunning) grace.start(100);
    });
    connect(&grace, &QTimer::timeout, this, [this] {
        if (operation != Operation::Pasting) return;
        deadline.stop();
        ++pasteIndex;
        nextPaste();
    });

    // KWin 6's public D-Bus API has no non-interactive active-window query. One small script monitors it.
    scriptName = QStringLiteral("quick-region-overlay-stack-focus");
    auto unload = call(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"), QStringLiteral("org.kde.kwin.Scripting"), QStringLiteral("unloadScript"));
    unload << scriptName;
    QDBusConnection::sessionBus().call(unload, QDBus::Block, 500);
    script.setFileTemplate(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/qro-focus-XXXXXX.js"));
    if (script.open()) {
        script.write("function report(w) { callDBus('org.local.QuickRegionOverlay', '/Stack', 'org.local.QuickRegionOverlay.Stack', 'ActiveWindow', w ? String(w.internalId) : '', w ? String(w.resourceClass) : ''); }\nworkspace.windowActivated.connect(report); report(workspace.activeWindow);\n");
        script.flush();
        auto message = call(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"), QStringLiteral("org.kde.kwin.Scripting"), QStringLiteral("loadScript"));
        message << script.fileName() << scriptName;
        auto *watch = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, 1000), this);
        connect(watch, &QDBusPendingCallWatcher::finished, this, [this, watch] {
            const QDBusPendingReply<int> reply = *watch;
            if (!reply.isError() && reply.value() >= 0) {
                QDBusConnection::sessionBus().asyncCall(call(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting/Script%1").arg(reply.value()), QStringLiteral("org.kde.kwin.Script"), QStringLiteral("run")), 1000);
            } else qWarning() << "stack-focus-unavailable; using normal copy/paste keys";
            watch->deleteLater();
        });
    }
}
StackController::~StackController() {
    shutdown();
    storage.waitForDone(); // No worker can enqueue a callback after this receiver is destroyed.
    auto message = call(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"), QStringLiteral("org.kde.kwin.Scripting"), QStringLiteral("unloadScript"));
    message << scriptName;
    QDBusConnection::sessionBus().call(message, QDBus::Block, 500);
    if (keys.state() != QProcess::NotRunning) { keys.kill(); keys.waitForFinished(1000); releaseSyntheticKeys(); }
    if (releaseKeys.state() != QProcess::NotRunning && !releaseKeys.waitForFinished(1000)) { releaseKeys.kill(); releaseKeys.waitForFinished(250); }
}
void StackController::setOperation(Operation next) {
    operation = next;
    busyWheel.setBusy(next != Operation::Idle && next != Operation::Stopping);
    notifyChanged();
}
void StackController::notifyChanged() {
    const QString state = State();
    if (state == lastPublishedState) return;
    lastPublishedState = state;
    emit Changed(Count(), state);
}
void StackController::Paste() {
    if (captureBusy && captureBusy()) { osd(QStringLiteral("Capture busy")); return; }
    paste();
}
void StackController::Clear() {
    if (captureBusy && captureBusy()) { osd(QStringLiteral("Capture busy")); return; }
    clear();
}
void StackController::shutdown() {
    if (operation == Operation::Stopping) return;
    setOperation(Operation::Stopping);
    ++serial; // Invalidate all queued offer/snapshot/consumer callbacks before stopping key work.
    modifiers.stop(); deadline.stop(); grace.stop();
    afterKeys = {}; afterModifiers = {};
    keys.disconnect(this);
    if (keyRunning || keys.state() != QProcess::NotRunning) {
        if (keys.state() != QProcess::NotRunning) { keys.kill(); keys.waitForFinished(250); }
        releaseSyntheticKeys();
    }
    keyRunning = false;
    // The worker remains alive serving the current offer while systemd starts the independent holder.
    const auto owned = clipboard.ownedItem();
    if (owned) handoffClipboard(restorePending ? original : *owned);
}
void StackController::ActiveWindow(const QString &id, const QString &windowClass) {
    activeId = id;
    activeClass = windowClass;
    // Leaving the target cancels the operation even if focus later returns.
    if (operation != Operation::Idle && operation != Operation::Stopping && !targetId.isEmpty() && id != targetId) {
        targetLeft = true;
        if (keyRunning) keys.kill();
    }
}
QString StackController::State() const { return QStringLiteral("%1:%2").arg(int(operation)).arg(items.size()); }
void StackController::osd(const QString &text) {
    qInfo().noquote() << "stack-osd" << text;
    auto message = call(QStringLiteral("org.kde.plasmashell"), QStringLiteral("/org/kde/osdService"), QStringLiteral("org.kde.osdService"), QStringLiteral("showText"));
    message << QStringLiteral("edit-paste") << text;
    QDBusConnection::sessionBus().asyncCall(message, 500);
}
void StackController::load() {
    cleanupRetiredStacks(directory);
    const QDir dir(directory);
    const auto files = dir.entryList({QStringLiteral("*.qrs")}, QDir::Files, QDir::Name);
    if (files.size() > maxItems) { loadError = QStringLiteral("Stack storage invalid; clear it"); return; }
    for (qsizetype i = 0; i < files.size(); ++i) {
        if (files[i] != QStringLiteral("%1.qrs").arg(i, 3, 10, QLatin1Char('0'))) { loadError = QStringLiteral("Stack storage invalid; clear it"); break; }
        QFile file(dir.filePath(files[i]));
        Item item;
        if (!file.open(QIODevice::ReadOnly) || file.size() > maxBytes + 128 * 1024 || !decode(file.readAll(), item)) { loadError = QStringLiteral("Stack storage invalid; clear it"); break; }
        items << item;
    }
    if (!loadError.isEmpty()) items.clear();
    pasted = hasPasteMarker(directory, items.size());
}
bool StackController::available() {
    if (busy()) { osd(QStringLiteral("Stack busy")); return false; }
    if (!loadError.isEmpty()) { osd(loadError); return false; }
    if (!clipboard.ready()) { osd(QStringLiteral("Clipboard unavailable")); return false; }
    return true;
}
namespace {
// Runs on the storage worker. Returns an empty string once the item is durably saved.
QString persistItem(const QString &path, qsizetype count, AddMode mode, const Item &item) {
    if (mode == AddMode::Replace) {
        const QString staged = stageStackReplacement(path, item);
        if (staged.isEmpty()) return QStringLiteral("Cannot save stack");
        const bool committed = exchangeStackDirectories(path, staged);
        if (!QDir(staged).removeRecursively()) qWarning() << "stack-replace-cleanup-failed" << staged;
        return committed ? QString() : QStringLiteral("Cannot replace stack");
    }
    if (!saveStackItem(path, count, item)) return QStringLiteral("Cannot save stack");
    QFile::remove(path + QStringLiteral("/pasted")); // Count mismatch already invalidates it durably.
    return {};
}
}
void StackController::appended(const Item &item, AddMode mode, const QString &error) {
    if (operation == Operation::Stopping) return; // Already on disk; load() picks it up after restart.
    if (!error.isEmpty()) { finish(error); return; }
    if (mode == AddMode::Replace) items = {item}; else items << item;
    pasted = false;
    qInfo().noquote() << QDateTime::currentMSecsSinceEpoch() << "stack-copy-saved" << QStringLiteral("ms=%1").arg(addTime.elapsed());
    finish(mode == AddMode::Replace ? QStringLiteral("New stack: 1 item") : items.size() == 1 ? QStringLiteral("Stack: 1 item") : QStringLiteral("Stack: %1 items").arg(items.size()));
}
void StackController::append(const Item &item) {
    if (!loadError.isEmpty()) { finish(loadError); return; }
    const auto mode = addMode(items.size(), pasted, newStackAfterPaste());
    if (mode == AddMode::Full) { finish(QStringLiteral("Stack full (100 items)")); return; }
    if (!hasPayload(item)) { finish(QStringLiteral("Nothing copied")); return; }
    if (size(item) > maxBytes) { finish(QStringLiteral("Item exceeds 64 MB")); return; }
    setOperation(Operation::Saving);
    const QString path = directory;
    const qsizetype count = items.size();
    storage.start([this, path, count, mode, item] {
        const QString error = persistItem(path, count, mode, item);
        QMetaObject::invokeMethod(this, [this, item, mode, error] { appended(item, mode, error); }, Qt::QueuedConnection);
    });
}
void StackController::appendImage(const QImage &image) {
    if (busy()) { osd(QStringLiteral("Stack busy")); return; }
    if (!loadError.isEmpty()) { osd(loadError); return; }
    const auto mode = addMode(items.size(), pasted, newStackAfterPaste());
    if (mode == AddMode::Full) { osd(QStringLiteral("Stack full (100 items)")); return; }
    addTime.start();
    qInfo() << QDateTime::currentMSecsSinceEpoch() << "stack-capture-closed";
    setOperation(Operation::Saving);
    const QString path = directory;
    const qsizetype count = items.size();
    // Encode and save in one job, so shutdown's waitForDone() also drains the save of an accepted screenshot.
    storage.start([this, image, path, count, mode] {
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        QString error;
        if (!image.save(&buffer, "PNG")) error = QStringLiteral("Cannot encode image");
        const Item encoded{{QStringLiteral("image/png"), png}};
        if (error.isEmpty() && size(encoded) > maxBytes) error = QStringLiteral("Item exceeds 64 MB");
        if (error.isEmpty()) error = persistItem(path, count, mode, encoded);
        QMetaObject::invokeMethod(this, [this, encoded, mode, error] {
            if (operation == Operation::Stopping) return;
            if (!error.isEmpty()) { appended(encoded, mode, error); return; }
            // Ordinary screenshot offer: unlike intermediate paste offers, Klipper must record this.
            pendingImage = encoded;
            pendingImageMode = mode;
            setOperation(Operation::PreparingImage);
            clipboard.offer(encoded, ++serial);
        }, Qt::QueuedConnection);
    });
}
void StackController::clear() {
    if (busy()) { osd(QStringLiteral("Stack busy")); return; }
    setOperation(Operation::Saving);
    // Rename is the commit point; a crash during deletion cannot resurrect a partially cleared stack.
    const QString trash = directory + QStringLiteral("-cleared-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (QDir(directory).exists() && !QDir().rename(directory, trash)) { finish(QStringLiteral("Cannot clear stack")); return; }
    items.clear();
    pasted = false;
    loadError.clear();
    notifyChanged();
    storage.start([this, trash] {
        if (QDir(trash).exists() && !QDir(trash).removeRecursively()) qWarning() << "stack-clear-cleanup-failed" << trash;
        QMetaObject::invokeMethod(this, [this] {
            if (operation != Operation::Stopping) finish(QStringLiteral("Stack cleared"));
        }, Qt::QueuedConnection);
    });
}
bool StackController::sameTarget() const { return !targetLeft && (targetId.isEmpty() || activeId == targetId); }
void StackController::waitModifiers(std::function<void()> next) {
    targetId = activeId;
    targetLeft = false;
    targetTerminal = terminal(activeClass);
    afterModifiers = std::move(next);
    setOperation(Operation::Modifiers);
    modifierTime.start();
    modifiers.start(0); // First check at once; the handler then polls every 15 ms.
}
void StackController::copy() {
    qInfo() << QDateTime::currentMSecsSinceEpoch() << "stack-copy-pressed";
    if (!canAdd()) return;
    addTime.start();
    waitModifiers([this] {
        setOperation(Operation::Copying);
        copyChanged = false;
        key(true, false, [this] {
            if (copyChanged) { setOperation(Operation::CopySnapshot); deadline.stop(); takeSnapshot(); }
        });
    });
}
void StackController::paste() {
    qInfo() << QDateTime::currentMSecsSinceEpoch() << "stack-paste-pressed";
    if (!available()) return;
    if (items.isEmpty()) { osd(QStringLiteral("Stack empty")); return; }
    pasteSucceeded = false;
    waitModifiers([this] {
        qInfo() << QDateTime::currentMSecsSinceEpoch() << "stack-paste-released";
        setOperation(Operation::RestoreSnapshot);
        const quint64 id = ++serial;
        clipboard.prepareOriginal(id);
        QTimer::singleShot(700, this, [this, id] {
            if (id == serial && operation == Operation::RestoreSnapshot) osd(QStringLiteral("Reading clipboard…"));
        });
    });
}
void StackController::takeSnapshot(qint64 limit) {
    const quint64 id = ++serial;
    clipboard.snapshot(id, limit);
    QTimer::singleShot(700, this, [this, id] {
        if (id == serial && (operation == Operation::CopySnapshot || operation == Operation::RestoreSnapshot)) osd(QStringLiteral("Reading clipboard…"));
    });
}
void StackController::key(bool copy, bool image, std::function<void()> next) {
    const auto state = physicalModifiers();
    const bool inject = state == ModifierState::Held && heldKeyboard >= 0;
    if (state != ModifierState::Released && !inject) {
        if (copy) finish(QStringLiteral("Release modifier keys"));
        else restore(QStringLiteral("Release modifier keys"));
        return;
    }
    const bool shift = targetTerminal && !image;
    if (inject) {
        // Still holding the shortcut: write the keys into that keyboard so they need no release first.
        qInfo().noquote() << QDateTime::currentMSecsSinceEpoch() << "stack-key" << (copy ? "copy" : "paste") << "terminal=" << targetTerminal << "image=" << image << "via=keyboard";
        afterKeys = std::move(next);
        keyRunning = true;
        deadline.start(1200);
        const bool ok = writeShortcutKeys(heldKeyboard, copy, shift);
        QTimer::singleShot(0, this, [this, ok] {
            if (!keyRunning) return;
            keyRunning = false;
            if (!ok) {
                if (operation == Operation::Pasting) restore(QStringLiteral("Paste key failed"));
                else finish(QStringLiteral("Copy key failed"));
                return;
            }
            auto after = std::move(afterKeys);
            if (after) after();
        });
        return;
    }
    QStringList args{QStringLiteral("key"), QStringLiteral("--key-delay=5"), QStringLiteral("29:1")};
    if (shift) args << QStringLiteral("42:1");
    args << (copy ? QStringLiteral("46:1") : QStringLiteral("47:1")) << (copy ? QStringLiteral("46:0") : QStringLiteral("47:0"));
    if (shift) args << QStringLiteral("42:0");
    args << QStringLiteral("29:0");
    qInfo().noquote() << QDateTime::currentMSecsSinceEpoch() << "stack-key" << (copy ? "copy" : "paste") << "terminal=" << targetTerminal << "image=" << image;
    afterKeys = std::move(next);
    keyRunning = true;
    deadline.start(1200);
    keys.start(QStringLiteral("ydotool"), args);
}
void StackController::releaseSyntheticKeys() {
    if (releaseKeys.state() != QProcess::NotRunning) return;
    // ydotoold's virtual device outlives its CLI. A killed CLI must not leave Ctrl/Shift pressed on that device.
    releaseKeys.start(QStringLiteral("ydotool"), {QStringLiteral("key"), QStringLiteral("--key-delay=0"), QStringLiteral("46:0"), QStringLiteral("47:0"), QStringLiteral("42:0"), QStringLiteral("29:0")});
    QTimer::singleShot(1000, this, [this] { if (releaseKeys.state() != QProcess::NotRunning) releaseKeys.kill(); });
}
void StackController::nextPaste() {
    if (!sameTarget() || locked()) { restore(QStringLiteral("Focus changed")); return; }
    if (pasteIndex >= pasteItems.size()) { pasteSucceeded = true; restore(QStringLiteral("Pasted %1 items").arg(items.size())); return; }
    auto item = pasteItems[pasteIndex];
    item.insert(secretHint, QByteArrayLiteral("secret"));
    setOperation(Operation::Offering);
    deadline.start(1500);
    clipboard.offer(item, ++serial);
}
void StackController::restore(const QString &message) {
    grace.stop(); deadline.stop();
    completion = message;
    setOperation(Operation::Restoring);
    clipboard.offer(original, ++serial);
}
void StackController::finish(const QString &message) {
    modifiers.stop(); deadline.stop(); grace.stop();
    setOperation(Operation::Idle);
    pasteSucceeded = false;
    pasteItems.clear(); if (!restorePending) original.clear(); afterKeys = {}; afterModifiers = {};
    osd(message);
}
bool StackController::canAdd() {
    if (!available()) return false;
    if (addMode(items.size(), pasted, newStackAfterPaste()) == AddMode::Full) {
        osd(QStringLiteral("Stack full (100 items)")); return false;
    }
    return true;
}
