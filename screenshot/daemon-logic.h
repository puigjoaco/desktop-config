// Pure daemon decisions shared by quick-region-overlay.cpp and tests/daemon-logic-test.cpp: command-line handling,
// the ShortcutStatus answer and the capture state machine.
#pragma once
#include <QByteArray>
#include <QKeySequence>
#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <chrono>
#include <functional>
#include <linux/input.h>
#include <optional>

namespace DaemonLogic {

// Copy/paste keys written straight into the keyboard the user is still holding the shortcut on. Releasing Alt, Meta,
// Shift and the shortcut letters first means the app sees plain Ctrl+C/V while the fingers are still down; a key-up for
// a key that is not down is dropped by the kernel. Ctrl is always pressed and released here, so no bare letter can be
// typed if the user lets go of Ctrl meanwhile. The batch presses the Ctrl (and Shift) key the user is not holding: the
// kernel can interleave a physical release between injected events, and releasing the other key cannot clear this one.
// Keyboards report only changes, so the Alt/Shift keys the user was holding are pressed again at the end of the batch;
// otherwise the next C/S/V while still holding Ctrl+Alt would reach the app as Ctrl+C/S/V.
struct KeyEvent { quint16 type; quint16 code; qint32 value; };
inline quint16 freeModifierKey(bool leftHeld, quint16 left, quint16 right) { return leftHeld ? right : left; }
// Held keys the batch releases and then presses again: Alt and Shift. Meta is left released, since a lone
// Meta press/release opens the launcher; Ctrl is never released by the batch.
inline QList<quint16> restoredModifiers(const std::function<bool(int)> &down) {
    QList<quint16> keys;
    for (quint16 key : {KEY_LEFTALT, KEY_RIGHTALT, KEY_LEFTSHIFT, KEY_RIGHTSHIFT})
        if (down(key)) keys.append(key);
    return keys;
}
inline QList<KeyEvent> shortcutKeyEvents(bool copy, bool shift, quint16 ctrlKey = KEY_LEFTCTRL, quint16 shiftKey = KEY_LEFTSHIFT,
                                         const QList<quint16> &heldModifiers = {}) {
    QList<KeyEvent> events;
    const auto add = [&events](quint16 type, quint16 code, qint32 value) { events.append({type, code, value}); };
    for (quint16 key : {KEY_LEFTALT, KEY_RIGHTALT, KEY_LEFTMETA, KEY_RIGHTMETA, KEY_LEFTSHIFT, KEY_RIGHTSHIFT, KEY_C, KEY_V})
        add(EV_KEY, key, 0);
    add(EV_SYN, SYN_REPORT, 0);
    add(EV_KEY, ctrlKey, 1);
    if (shift) add(EV_KEY, shiftKey, 1);
    add(EV_SYN, SYN_REPORT, 0);
    const quint16 letter = copy ? KEY_C : KEY_V;
    add(EV_KEY, letter, 1);
    add(EV_SYN, SYN_REPORT, 0);
    add(EV_KEY, letter, 0);
    add(EV_SYN, SYN_REPORT, 0);
    if (shift) add(EV_KEY, shiftKey, 0);
    add(EV_KEY, ctrlKey, 0);
    add(EV_SYN, SYN_REPORT, 0);
    if (!heldModifiers.isEmpty()) {
        for (quint16 key : heldModifiers) add(EV_KEY, key, 1);
        add(EV_SYN, SYN_REPORT, 0);
    }
    return events;
}

// Only quick-region-overlay.service runs --daemon; --capture asks it for a capture over D-Bus.
enum class Command { Capture, Daemon, HoldClipboard, Usage };
inline Command parseCommand(const QStringList &args) {
    if (args == QStringList{QStringLiteral("--capture")}) return Command::Capture;
    if (args == QStringList{QStringLiteral("--daemon")}) return Command::Daemon;
    if (args.size() == 2 && args[0] == QLatin1String("--hold-clipboard") && !args[1].isEmpty()) return Command::HoldClipboard;
    return Command::Usage;
}
// Exit status for a usage error, and for "another daemon owns the name", which the unit's RestartPreventExitStatus
// keeps systemd from looping on.
inline constexpr int usageStatus = 2;
inline constexpr int alreadyRunningStatus = 3;
// The --daemon exit status when the D-Bus name is already owned, or nothing to continue.
inline std::optional<int> alreadyRunning(bool nameOwned) { return nameOwned ? std::optional<int>(alreadyRunningStatus) : std::nullopt; }

// One sequence from KGlobalAccel's shortcutKeys, which sends each as its four combined chords, unused ones 0.
inline QKeySequence keySequence(const QList<int> &chords) {
    int keys[4] = {};
    for (qsizetype i = 0; i < qMin<qsizetype>(chords.size(), 4); ++i) keys[i] = chords[i];
    return QKeySequence(keys[0], keys[1], keys[2], keys[3]);
}

// The keys that bind anything: KGlobalAccel keeps an empty sequence for a cleared key or a default it couldn't grant.
inline QList<QKeySequence> activeKeys(QList<QKeySequence> keys) {
    keys.removeAll(QKeySequence());
    return keys;
}

// ShortcutStatus: "registered:<keys>" while the action has keys, otherwise "none", or "none:<component/action>, ..."
// naming who holds the default key. Sequences are separated by "; " and the chords of one by ", ", so the result
// remains machine-parseable.
inline QString shortcutStatus(const QList<QKeySequence> &liveKeys, const QStringList &defaultKeyOwners) {
    const QList<QKeySequence> keys = activeKeys(liveKeys);
    if (!keys.isEmpty()) return QStringLiteral("registered:") + QKeySequence::listToString(keys);
    if (defaultKeyOwners.isEmpty()) return QStringLiteral("none");
    return QStringLiteral("none:") + defaultKeyOwners.join(QStringLiteral(", "));
}

// One press's way to an overlay. A press starts a capture; a layout change aborts it (the serial goes up, so KWin's
// late reply is discarded) and the press captures again after the windows are rebuilt, however often the layout
// changes, until giveUp has passed since the press. A reply whose shape doesn't fit an unchanged layout is retried
// once per press; the second one fails the capture.
class CaptureFlow {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr std::chrono::milliseconds giveUp{2000};
    enum class AfterRebuild { Nothing, Retry, Dropped };
    // What a shortcut press does: capture when idle, close an open overlay (so a stuck one can always be dismissed),
    // and nothing while a capture is in flight or waiting for a rebuild.
    enum class Press { Capture, Cancel, Busy };

    // A capture in flight, an overlay open or a press waiting for a rebuild.
    bool busy() const { return capturing || shown || retryPending; }
    bool overlayShown() const { return shown; }
    Press shortcutPress() const {
        if (shown) return Press::Cancel;
        return busy() ? Press::Busy : Press::Capture;
    }
    void pressed(Clock::time_point now) { pressedAt = now; shapeRetries = 0; }
    // The windows are being rebuilt; capture once they are.
    void defer() { retryPending = true; }
    quint64 begin() { capturing = true; return ++serial; }
    bool isCurrent(quint64 replySerial) const { return capturing && replySerial == serial; }
    // The layout changed: any reply in flight is stale. Returns whether that aborted a capture, which then retries.
    bool layoutChanged() {
        ++serial;
        if (!capturing) return false;
        capturing = false;
        retryPending = true;
        return true;
    }
    // The reply was for another layout; capture again after the rebuild the caller schedules.
    void retryAfterRebuild() { capturing = false; retryPending = true; }
    // The reply's shape doesn't fit the layout Qt reports. The first time in a press, retry after a rebuild like
    // retryAfterRebuild; the second time returns false and the caller fails the capture.
    bool retryShape() {
        if (++shapeRetries > 1) return false;
        retryAfterRebuild();
        return true;
    }
    AfterRebuild rebuilt(Clock::time_point now) {
        if (!retryPending) return AfterRebuild::Nothing;
        retryPending = false;
        if (!pressedAt || now - *pressedAt > giveUp) { pressedAt.reset(); return AfterRebuild::Dropped; }
        return AfterRebuild::Retry;
    }
    void captured() { capturing = false; shown = true; pressedAt.reset(); }
    void failed() { capturing = false; pressedAt.reset(); }
    void closed() { shown = false; }
private:
    bool capturing = false;
    bool shown = false;
    bool retryPending = false;
    quint64 serial = 0;
    int shapeRetries = 0;
    std::optional<Clock::time_point> pressedAt;
};

} // namespace DaemonLogic
