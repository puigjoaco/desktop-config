#include "../logic.h"
#include <QTest>
class LogicTest : public QObject {
    Q_OBJECT
private slots:
    void labels() {
        QCOMPARE(Mattux::pasteLabel(0, true), QStringLiteral("Paste stack"));
        QCOMPARE(Mattux::pasteLabel(5, false), QStringLiteral("Paste stack"));
        QCOMPARE(Mattux::pasteLabel(1, true), QStringLiteral("Paste 1 item"));
        QCOMPARE(Mattux::pasteLabel(3, true), QStringLiteral("Paste 3 items"));
        QCOMPARE(Mattux::subtitle(4, false, true), QStringLiteral("Not running"));
        QCOMPARE(Mattux::subtitle(0, true, false), QStringLiteral("Empty"));
        QCOMPARE(Mattux::subtitle(1, true, false), QStringLiteral("1 item"));
        QCOMPARE(Mattux::subtitle(3, true, false), QStringLiteral("3 items"));
        QCOMPARE(Mattux::subtitle(100, true, false), QStringLiteral("100 items (full)"));
        QCOMPARE(Mattux::subtitle(100, true, true), QStringLiteral("Working…"));
        QVERIFY(!Mattux::working(QStringLiteral("0:8")));
        QVERIFY(Mattux::working(QStringLiteral("9:8")));
    }
    void keys() {
        QCOMPARE(Mattux::keyText({}), QStringLiteral("None"));
        QCOMPARE(Mattux::keyText({QKeySequence()}), QStringLiteral("None"));
        QCOMPARE(Mattux::keyText(Mattux::shortcuts().first().defaults), QStringLiteral("Meta+Shift+S, Print"));
        QCOMPARE(Mattux::keyText({{}, QKeySequence(Qt::Key_Print)}), QStringLiteral("Print"));
        QCOMPARE(Mattux::firstKeyText({}), QString());
        QCOMPARE(Mattux::firstKeyText({{}, QKeySequence(Qt::Key_Print)}), QStringLiteral("Print"));
        QCOMPARE(Mattux::firstKeyText(Mattux::shortcuts().first().defaults), QStringLiteral("Meta+Shift+S"));
    }
    void sizes() {
        QCOMPARE(Mattux::historySizes(40), QList<int>({10, 20, 40, 100, 200}));
        QCOMPARE(Mattux::historySizes(25), QList<int>({10, 20, 25, 40, 100, 200}));
        QCOMPARE(Mattux::historySizes(500), QList<int>({10, 20, 40, 100, 200, 500}));
        QCOMPARE(Mattux::historySizes(0), QList<int>({10, 20, 40, 100, 200}));
    }
};
QTEST_GUILESS_MAIN(LogicTest)
#include "logic-test.moc"
