#include "../shortcut-dialog.h"
#include "../tray.h"
#include <KGlobalAccel>
#include <KKeySequenceWidget>
#include <QAction>
#include <QApplication>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QTest>
#include <QTimer>

class ShortcutDialogTest : public QObject {
    Q_OBJECT
    QList<QAction *> actions;
    ShortcutDialog *dialog = nullptr;
    QList<KKeySequenceWidget *> keys;
    QDialogButtonBox *buttons = nullptr;
    void open(int index = 1) {
        dialog = new ShortcutDialog(Mattux::shortcuts()[index]);
        // Verify inhibition ends before deferred deletion, while the object lives.
        dialog->setAttribute(Qt::WA_DeleteOnClose, false);
        keys = dialog->findChildren<KKeySequenceWidget *>();
        buttons = dialog->findChild<QDialogButtonBox *>();
        dialog->show();
        QVERIFY(QTest::qWaitForWindowExposed(dialog));
        QCOMPARE(keys.size(), 2);
    }
    void record(KKeySequenceWidget *key, Qt::Key value, Qt::KeyboardModifiers modifiers) {
        key->captureKeySequence();
        QVERIFY(key->isRecording());
        QTest::keyClick(dialog->windowHandle(), value, modifiers);
        QTRY_VERIFY(!key->isRecording());
    }
private slots:
    void initTestCase() {
        QVERIFY(qEnvironmentVariable("MATTUX_ISOLATED_TEST") == QStringLiteral("1"));
        for (const auto &shortcut : Mattux::shortcuts()) {
            auto *action = new QAction(QString::fromUtf8(shortcut.title), this);
            action->setObjectName(QString::fromUtf8(shortcut.id));
            action->setProperty("componentName", QStringLiteral("quick-region-overlay"));
            action->setProperty("componentDisplayName", QStringLiteral("Quick Region Overlay"));
            QVERIFY(KGlobalAccel::self()->setGlobalShortcut(action, shortcut.defaults));
            actions << action;
        }
    }
    void cleanupTestCase() {
        for (int i = 0; i < actions.size(); ++i)
            QVERIFY(KGlobalAccel::self()->setShortcut(actions[i], Mattux::shortcuts()[i].defaults, KGlobalAccel::NoAutoloading));
    }
    void cleanup() { delete dialog; dialog = nullptr; }
    void recordingAndSave() {
        open();
        QCOMPARE(keys[0]->keySequence(), Mattux::shortcuts()[1].defaults[0]);
        QVERIFY(keys[1]->keySequence().isEmpty());
        record(keys[0], Qt::Key_B, Qt::ControlModifier | Qt::AltModifier);
        record(keys[1], Qt::Key_B, Qt::MetaModifier | Qt::AltModifier);
        const QList<QKeySequence> expected{keys[0]->keySequence(), keys[1]->keySequence()};
        dialog->grab().save(qEnvironmentVariable("MATTUX_TEST_OUT") + QStringLiteral("/shortcut-recorded.png"));
        buttons->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(dialog->result(), int(QDialog::Accepted));
        QTRY_COMPARE(readShortcut(QStringLiteral("stack-copy")), expected);
        delete dialog; dialog = nullptr;
        open();
        QCOMPARE(keys[0]->keySequence(), expected[0]);
        QCOMPARE(keys[1]->keySequence(), expected[1]);
    }
    void cancelAndDefaults() {
        open();
        const auto before = readShortcut(QStringLiteral("stack-copy"));
        keys[0]->clearKeySequence(); keys[1]->clearKeySequence();
        buttons->button(QDialogButtonBox::Cancel)->click();
        QCOMPARE(readShortcut(QStringLiteral("stack-copy")), before);
        delete dialog; dialog = nullptr;
        open();
        keys[0]->captureKeySequence();
        QVERIFY(keys[0]->isRecording());
        buttons->button(QDialogButtonBox::RestoreDefaults)->click();
        QVERIFY(!keys[0]->isRecording());
        QCOMPARE(keys[0]->keySequence(), Mattux::shortcuts()[1].defaults[0]);
        QVERIFY(keys[1]->keySequence().isEmpty());
        buttons->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(readShortcut(QStringLiteral("stack-copy")).value(0), Mattux::shortcuts()[1].defaults[0]);
        QVERIFY(readShortcut(QStringLiteral("stack-copy")).value(1).isEmpty());
    }
    void conflictCancelAndReassign() {
        open();
        const auto conflict = Mattux::shortcuts()[3].defaults[0];
        const auto original = keys[0]->keySequence();
        auto answer = [this](const QString &text, int &seen) {
            auto *timer = new QTimer(dialog);
            connect(timer, &QTimer::timeout, dialog, [timer, text, &seen] {
                auto *modal = QApplication::activeModalWidget();
                if (!modal) return;
                for (auto *button : modal->findChildren<QPushButton *>()) {
                    if (button->text().remove(QLatin1Char('&')).contains(text)) {
                        ++seen; timer->stop(); button->click(); return;
                    }
                }
            });
            timer->start(30);
        };
        int seen = 0;
        answer(QStringLiteral("Cancel"), seen);
        keys[0]->setKeySequence(conflict, KKeySequenceWidget::Validate);
        QCOMPARE(seen, 1);
        QCOMPARE(keys[0]->keySequence(), original);
        QCOMPARE(readShortcut(QStringLiteral("stack-paste")).value(0), conflict);
        answer(QStringLiteral("Reassign"), seen);
        keys[0]->setKeySequence(conflict, KKeySequenceWidget::Validate);
        QCOMPARE(seen, 2);
        QCOMPARE(keys[0]->keySequence(), conflict);
        QVERIFY(!readShortcut(QStringLiteral("stack-paste")).contains(conflict));
        buttons->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(readShortcut(QStringLiteral("stack-copy")).value(0), conflict);
    }
    void alternateOnlyRoundTrip() {
        open();
        keys[0]->clearKeySequence();
        keys[1]->setKeySequence(QKeySequence(Qt::META | Qt::ALT | Qt::Key_B));
        buttons->button(QDialogButtonBox::Ok)->click();
        const auto saved = readShortcut(QStringLiteral("stack-copy"));
        QVERIFY(saved.value(0).isEmpty());
        QCOMPARE(saved.value(1), QKeySequence(Qt::META | Qt::ALT | Qt::Key_B));
    }
    void closeAndHideRecording() {
        open();
        keys[0]->captureKeySequence();
        QVERIFY(keys[0]->isRecording());
        dialog->grab().save(qEnvironmentVariable("MATTUX_TEST_OUT") + QStringLiteral("/shortcut-recording.png"));
        dialog->reject();
        QVERIFY(!keys[0]->isRecording());
        dialog->show();
        QVERIFY(QTest::qWaitForWindowExposed(dialog));
        keys[1]->captureKeySequence();
        QVERIFY(keys[1]->isRecording());
        dialog->hide();
        QVERIFY(!keys[1]->isRecording());
    }
};
int main(int argc, char **argv) {
    // Refuse accidental execution on the live session, even if called directly.
    if (qEnvironmentVariable("MATTUX_ISOLATED_TEST") != QStringLiteral("1")) return 77;
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("mattux-test"));
    app.setQuitOnLastWindowClosed(false);
    if (app.arguments().contains(QStringLiteral("--menu-render"))) {
        Tray tray;
        for (auto *widget : app.topLevelWidgets()) {
            auto *menu = qobject_cast<QMenu *>(widget);
            if (!menu || menu->actions().isEmpty() || menu->actions().first()->text() != QStringLiteral("Screenshot && clipboard")) continue;
            menu->popup(QPoint(200, 100));
            if (!QTest::qWaitForWindowExposed(menu)) return 1;
            QTest::qWait(100);
            menu->grab().save(qEnvironmentVariable("MATTUX_TEST_OUT") + QStringLiteral("/menu-native.png"));
            menu->hide();
            for (auto *submenu : menu->findChildren<QMenu *>()) {
                if (submenu->title() != QStringLiteral("Clipboard history")) continue;
                submenu->popup(QPoint(200, 100));
                if (!QTest::qWaitForWindowExposed(submenu)) return 2;
                submenu->grab().save(qEnvironmentVariable("MATTUX_TEST_OUT") + QStringLiteral("/history-native.png"));
                submenu->hide();
            }
            return 0;
        }
        return 3;
    }
    if (app.arguments().contains(QStringLiteral("--quit-test"))) {
        const auto before = readShortcut(QStringLiteral("capture-region"));
        auto *dialog = new ShortcutDialog(Mattux::shortcuts()[0]);
        dialog->setAttribute(Qt::WA_DeleteOnClose, false);
        dialog->show();
        if (!QTest::qWaitForWindowExposed(dialog)) return 1;
        const auto keys = dialog->findChildren<KKeySequenceWidget *>();
        keys[0]->captureKeySequence();
        if (!keys[0]->isRecording()) return 2;
        bool stopped = false;
        QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&] { stopped = !keys[0]->isRecording(); });
        QTimer::singleShot(0, &app, &QCoreApplication::quit);
        app.exec();
        const auto after = readShortcut(QStringLiteral("capture-region"));
        qInfo() << "Quit preserves capture keys:" << before << after;
        delete dialog;
        return stopped && before == after ? 0 : 3;
    }
    ShortcutDialogTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "shortcut-dialog-test.moc"
