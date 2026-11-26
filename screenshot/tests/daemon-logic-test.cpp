// Daemon decision checks for quick-region-overlay. Run: ctest --test-dir <build dir> --output-on-failure
#include "../daemon-logic.h"
#include <QTest>

using namespace DaemonLogic;
using namespace std::chrono_literals;
using Flow = CaptureFlow;

class DaemonLogicTest final : public QObject {
    Q_OBJECT
private slots:
    void shortcutKeysReleaseHeldKeysFirst() {
        const auto events = DaemonLogic::shortcutKeyEvents(false, false);
        const auto at = [&events](quint16 type, quint16 code, qint32 value) {
            for (qsizetype i = 0; i < events.size(); ++i)
                if (events[i].type == type && events[i].code == code && events[i].value == value) return i;
            return qsizetype(-1);
        };
        QVERIFY(at(EV_KEY, KEY_LEFTALT, 0) >= 0 && at(EV_KEY, KEY_LEFTALT, 0) < at(EV_KEY, KEY_V, 1));
        QVERIFY(at(EV_KEY, KEY_V, 0) < at(EV_KEY, KEY_LEFTCTRL, 1)); // a still-held V is released before the new press
        QVERIFY(at(EV_KEY, KEY_LEFTCTRL, 1) < at(EV_KEY, KEY_V, 1));
        QVERIFY(at(EV_KEY, KEY_V, 1) < at(EV_KEY, KEY_LEFTCTRL, 0));
        QCOMPARE(events.last().type, quint16(EV_SYN));
        QCOMPARE(at(EV_KEY, KEY_LEFTSHIFT, 1), qsizetype(-1));
        const auto terminal = DaemonLogic::shortcutKeyEvents(true, true);
        QVERIFY(std::any_of(terminal.begin(), terminal.end(), [](const auto &e) { return e.code == KEY_C && e.value == 1; }));
        QVERIFY(std::any_of(terminal.begin(), terminal.end(), [](const auto &e) { return e.code == KEY_LEFTSHIFT && e.value == 1; }));
        // With left Ctrl physically held, the batch uses right Ctrl, which a physical left-Ctrl release cannot clear.
        QCOMPARE(DaemonLogic::freeModifierKey(true, KEY_LEFTCTRL, KEY_RIGHTCTRL), quint16(KEY_RIGHTCTRL));
        QCOMPARE(DaemonLogic::freeModifierKey(false, KEY_LEFTCTRL, KEY_RIGHTCTRL), quint16(KEY_LEFTCTRL));
        const auto right = DaemonLogic::shortcutKeyEvents(false, false, KEY_RIGHTCTRL);
        QVERIFY(std::none_of(right.begin(), right.end(), [](const auto &e) { return e.code == KEY_LEFTCTRL; }));
        QVERIFY(std::any_of(right.begin(), right.end(), [](const auto &e) { return e.code == KEY_RIGHTCTRL && e.value == 1; }));
        // Still-held Alt is pressed again after Ctrl+V, so holding Ctrl+Alt keeps the next shortcut working.
        const auto held = DaemonLogic::restoredModifiers([](int key) { return key == KEY_LEFTALT || key == KEY_LEFTMETA; });
        QCOMPARE(held, QList<quint16>({KEY_LEFTALT}));
        const auto again = DaemonLogic::shortcutKeyEvents(false, false, KEY_LEFTCTRL, KEY_LEFTSHIFT, held);
        qsizetype ctrlUp = -1, altDown = -1, altUp = -1;
        for (qsizetype i = 0; i < again.size(); ++i) {
            if (again[i].code == KEY_LEFTCTRL && again[i].value == 0) ctrlUp = i;
            if (again[i].code == KEY_LEFTALT && again[i].value == 1) altDown = i;
            if (again[i].code == KEY_LEFTALT && again[i].value == 0 && altUp < 0) altUp = i;
        }
        QVERIFY(altUp >= 0 && altUp < ctrlUp && ctrlUp < altDown);
        QCOMPARE(again.last().type, quint16(EV_SYN));
    }
    void commands() {
        QCOMPARE(parseCommand({QStringLiteral("--capture")}), Command::Capture);
        QCOMPARE(parseCommand({QStringLiteral("--daemon")}), Command::Daemon);
        QCOMPARE(parseCommand({QStringLiteral("--hold-clipboard"), QStringLiteral("/tmp/snapshot.qrs")}), Command::HoldClipboard);
        QCOMPARE(parseCommand({QStringLiteral("--hold-clipboard")}), Command::Usage);
        QCOMPARE(parseCommand({QStringLiteral("--hold-clipboard"), QString()}), Command::Usage);
        QCOMPARE(parseCommand({QStringLiteral("--hold-clipboard"), QStringLiteral("file"), QStringLiteral("extra")}), Command::Usage);
        QCOMPARE(parseCommand({}), Command::Usage);
        QCOMPARE(parseCommand({QStringLiteral("--daemon"), QStringLiteral("--capture")}), Command::Usage);
        QCOMPARE(parseCommand({QStringLiteral("--capture"), QStringLiteral("extra")}), Command::Usage);
        QCOMPARE(parseCommand({QStringLiteral("daemon")}), Command::Usage);
        QCOMPARE(usageStatus, 2);
    }
    void alreadyRunningExitsThree() {
        QCOMPARE(alreadyRunning(true), std::optional<int>(3));
        QCOMPARE(alreadyRunning(false), std::nullopt);
    }
    void shortcutStatusText() {
        const QKeySequence metaShiftS(Qt::META | Qt::SHIFT | Qt::Key_S);
        const QKeySequence metaF12(Qt::META | Qt::Key_F12);
        QCOMPARE(shortcutStatus({metaShiftS}, {}), QStringLiteral("registered:Meta+Shift+S"));
        QCOMPARE(shortcutStatus({metaShiftS, metaF12}, {}), QStringLiteral("registered:Meta+Shift+S; Meta+F12"));
        QCOMPARE(shortcutStatus({QKeySequence(Qt::META | Qt::SHIFT | Qt::Key_S), QKeySequence(Qt::Key_Print)}, {}), QStringLiteral("registered:Meta+Shift+S; Print"));
        // A chord keeps its later keys after ", ", so install.sh's "; " split can't take it for Meta+Shift+S.
        const QKeySequence chord(Qt::META | Qt::SHIFT | Qt::Key_S, Qt::CTRL | Qt::Key_C);
        QCOMPARE(shortcutStatus({chord, QKeySequence(Qt::Key_Print)}, {}), QStringLiteral("registered:Meta+Shift+S, Ctrl+C; Print"));
        // Keys win over owners: the owners list only explains an empty binding.
        QCOMPARE(shortcutStatus({metaF12}, {QStringLiteral("other/action")}), QStringLiteral("registered:Meta+F12"));
        // KGlobalAccel reports a cleared binding as an empty sequence.
        QCOMPARE(shortcutStatus({QKeySequence()}, {}), QStringLiteral("none"));
        QCOMPARE(shortcutStatus({}, {}), QStringLiteral("none"));
        QCOMPARE(shortcutStatus({}, {QStringLiteral("quick-region-screenshot-v3.desktop/_launch")}), QStringLiteral("none:quick-region-screenshot-v3.desktop/_launch"));
        QCOMPARE(shortcutStatus({}, {QStringLiteral("a/b"), QStringLiteral("c/d")}), QStringLiteral("none:a/b, c/d"));
        // Two defaults KGlobalAccel couldn't grant come back as two empty sequences: no keys, so the owners show.
        QCOMPARE(shortcutStatus({QKeySequence(), QKeySequence()}, {QStringLiteral("a/b")}), QStringLiteral("none:a/b"));
    }
    void activeKeysDropEmptySequences() {
        QVERIFY(activeKeys({QKeySequence(), QKeySequence()}).isEmpty());
        QVERIFY(activeKeys({}).isEmpty());
        const QKeySequence print(Qt::Key_Print);
        QCOMPARE(activeKeys({QKeySequence(), print}), QList<QKeySequence>{print});
    }
    void keySequencesFromShortcutKeys() {
        // Recorded shortcutKeys sequences: [[301989971,0,0,0]] and [[16777225,0,0,0]].
        QCOMPARE(keySequence({301989971, 0, 0, 0}), QKeySequence(Qt::META | Qt::SHIFT | Qt::Key_S));
        QCOMPARE(keySequence({16777225, 0, 0, 0}), QKeySequence(Qt::Key_Print));
        const QKeySequence chord = keySequence({301989971, (Qt::CTRL | Qt::Key_C).toCombined(), 0, 0});
        QCOMPARE(chord.count(), 2);
        QCOMPARE(chord, QKeySequence(Qt::META | Qt::SHIFT | Qt::Key_S, Qt::CTRL | Qt::Key_C));
        QCOMPARE(keySequence({1, 2, 3, 4, 5}).count(), 4);
        QCOMPARE(keySequence({16777225}), QKeySequence(Qt::Key_Print));
        QVERIFY(keySequence({0, 0, 0, 0}).isEmpty());
        QVERIFY(keySequence({}).isEmpty());
    }
    void captureSerialDiscardsStaleReplies() {
        Flow flow;
        flow.pressed(Flow::Clock::time_point{0ms});
        const quint64 first = flow.begin();
        QVERIFY(flow.isCurrent(first));
        QVERIFY(flow.busy());
        QVERIFY(flow.layoutChanged());     // aborted the capture in flight
        QVERIFY(!flow.isCurrent(first));   // its late reply is dropped
        QCOMPARE(flow.rebuilt(Flow::Clock::time_point{100ms}), Flow::AfterRebuild::Retry);
        const quint64 second = flow.begin();
        QVERIFY(second != first);
        QVERIFY(!flow.isCurrent(first));
        QVERIFY(flow.isCurrent(second));
    }
    void pressSurvivesRepeatedLayoutChanges() {
        Flow flow;
        flow.pressed(Flow::Clock::time_point{0ms});
        for (int change = 1; change <= 5; ++change) {
            flow.begin();
            QVERIFY(flow.layoutChanged());
            QVERIFY(flow.busy());   // a second press now is busy, not a second capture
            QCOMPARE(flow.rebuilt(Flow::Clock::time_point{change * 300ms}), Flow::AfterRebuild::Retry);
        }
        flow.begin();
        flow.captured();
        QVERIFY(flow.overlayShown());
        // A layout change with the overlay open aborts no capture; the caller closes the overlay.
        QVERIFY(!flow.layoutChanged());
        flow.closed();
        QVERIFY(!flow.busy());
        QCOMPARE(flow.rebuilt(Flow::Clock::time_point{5s}), Flow::AfterRebuild::Nothing);
    }
    void staleShapeRetriesAfterRebuild() {
        Flow flow;
        flow.pressed(Flow::Clock::time_point{0ms});
        const quint64 serial = flow.begin();
        flow.retryAfterRebuild();          // the reply was rendered for another layout
        QVERIFY(!flow.isCurrent(serial));
        QVERIFY(!flow.layoutChanged());    // the rebuild the caller schedules; nothing in flight any more
        QCOMPARE(flow.rebuilt(Flow::Clock::time_point{250ms}), Flow::AfterRebuild::Retry);
    }
    void pressDuringRebuildCapturesAfterIt() {
        Flow flow;
        flow.pressed(Flow::Clock::time_point{0ms});
        flow.defer();
        QVERIFY(flow.busy());
        QCOMPARE(flow.rebuilt(Flow::Clock::time_point{10ms}), Flow::AfterRebuild::Retry);
        QVERIFY(!flow.busy());
    }
    void pressGivesUpAfterTwoSeconds() {
        Flow flow;
        flow.pressed(Flow::Clock::time_point{0ms});
        flow.begin();
        QVERIFY(flow.layoutChanged());
        QCOMPARE(flow.rebuilt(Flow::Clock::time_point{Flow::giveUp}), Flow::AfterRebuild::Retry);
        flow.begin();
        QVERIFY(flow.layoutChanged());
        QCOMPARE(flow.rebuilt(Flow::Clock::time_point{Flow::giveUp + 1ms}), Flow::AfterRebuild::Dropped);
        QVERIFY(!flow.busy());
        // The next press starts afresh.
        flow.pressed(Flow::Clock::time_point{10s});
        flow.begin();
        QVERIFY(flow.layoutChanged());
        QCOMPARE(flow.rebuilt(Flow::Clock::time_point{10s + 100ms}), Flow::AfterRebuild::Retry);
    }
    void secondShapeMismatchFails() {
        Flow flow;
        flow.pressed(Flow::Clock::time_point{0ms});
        const quint64 first = flow.begin();
        QVERIFY(flow.retryShape());        // the first mismatch retries after a rebuild
        QVERIFY(!flow.isCurrent(first));
        QCOMPARE(flow.rebuilt(Flow::Clock::time_point{250ms}), Flow::AfterRebuild::Retry);
        const quint64 second = flow.begin();
        QVERIFY(!flow.retryShape());       // the second in the same press fails
        QVERIFY(flow.isCurrent(second));   // retryShape left it to the caller's failed()
        flow.failed();
        QVERIFY(!flow.busy());
        QCOMPARE(flow.rebuilt(Flow::Clock::time_point{300ms}), Flow::AfterRebuild::Nothing);
        // A layout change between the mismatches doesn't reset the count; only a new press does.
        flow.pressed(Flow::Clock::time_point{1s});
        flow.begin();
        QVERIFY(flow.retryShape());
        QCOMPARE(flow.rebuilt(Flow::Clock::time_point{1s + 250ms}), Flow::AfterRebuild::Retry);
        flow.begin();
        QVERIFY(flow.layoutChanged());
        QCOMPARE(flow.rebuilt(Flow::Clock::time_point{1s + 300ms}), Flow::AfterRebuild::Retry);
        flow.begin();
        QVERIFY(!flow.retryShape());
        flow.failed();
        flow.pressed(Flow::Clock::time_point{5s});
        flow.begin();
        QVERIFY(flow.retryShape());
    }
    void shortcutPressCancelsOpenOverlay() {
        Flow flow;
        QCOMPARE(flow.shortcutPress(), Flow::Press::Capture);
        flow.pressed(Flow::Clock::time_point{0ms});
        flow.begin();
        QCOMPARE(flow.shortcutPress(), Flow::Press::Busy);     // a capture in flight ignores presses
        flow.captured();
        QCOMPARE(flow.shortcutPress(), Flow::Press::Cancel);   // an open overlay closes
        flow.closed();
        QCOMPARE(flow.shortcutPress(), Flow::Press::Capture);
        // A press waiting for a rebuild, or retrying after a layout change, is busy too.
        flow.pressed(Flow::Clock::time_point{1s});
        flow.defer();
        QCOMPARE(flow.shortcutPress(), Flow::Press::Busy);
        QCOMPARE(flow.rebuilt(Flow::Clock::time_point{1s + 10ms}), Flow::AfterRebuild::Retry);
        flow.begin();
        QVERIFY(flow.layoutChanged());
        QCOMPARE(flow.shortcutPress(), Flow::Press::Busy);
    }
    void failedCaptureEndsThePress() {
        Flow flow;
        flow.pressed(Flow::Clock::time_point{0ms});
        const quint64 serial = flow.begin();
        flow.failed();
        QVERIFY(!flow.isCurrent(serial));
        QVERIFY(!flow.busy());
        QVERIFY(!flow.layoutChanged());
        QCOMPARE(flow.rebuilt(Flow::Clock::time_point{100ms}), Flow::AfterRebuild::Nothing);
    }
};

QTEST_APPLESS_MAIN(DaemonLogicTest)
#include "daemon-logic-test.moc"
