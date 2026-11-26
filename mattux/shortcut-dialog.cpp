#include "shortcut-dialog.h"
#include <KGlobalAccel>
#include <KKeySequenceWidget>
#include <KKeySequenceRecorder>
#include <QApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHideEvent>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
// KGlobalAccel's modern wire format carries complete sequences: a(ai).
struct Sequence { QList<int> keys; };
QDBusArgument &operator<<(QDBusArgument &arg, const Sequence &sequence) {
    arg.beginStructure(); arg << sequence.keys; arg.endStructure(); return arg;
}
const QDBusArgument &operator>>(const QDBusArgument &arg, Sequence &sequence) {
    arg.beginStructure(); arg >> sequence.keys; arg.endStructure(); return arg;
}
}
Q_DECLARE_METATYPE(Sequence)
Q_DECLARE_METATYPE(QList<Sequence>)

QList<QKeySequence> readShortcut(const QString &id) {
    return KGlobalAccel::self()->globalShortcut(QStringLiteral("quick-region-overlay"), id);
}

ShortcutDialog::ShortcutDialog(const Mattux::Shortcut &shortcut, QWidget *parent) : QDialog(parent) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(QString::fromUtf8(shortcut.title) + QStringLiteral(" Shortcut — Mattux"));
    connect(qApp, &QCoreApplication::aboutToQuit, this, &ShortcutDialog::stopRecording);
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto *primary = new KKeySequenceWidget(this);
    auto *alternate = new KKeySequenceWidget(this);
    const auto keys = readShortcut(QString::fromUtf8(shortcut.id));
    for (auto *widget : {primary, alternate}) {
        widget->setMultiKeyShortcutsAllowed(false);
        widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        widget->setClearButtonShown(true);
        // Do not exclude the entire daemon component: sibling actions must still conflict.
        widget->setCheckForConflictsAgainst(KKeySequenceWidget::GlobalShortcuts | KKeySequenceWidget::StandardShortcuts);
    }
    primary->setKeySequence(keys.value(0)); alternate->setKeySequence(keys.value(1));
    form->addRow(QStringLiteral("Shortcut:"), primary);
    form->addRow(QStringLiteral("Alternate:"), alternate);
    layout->addLayout(form);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::RestoreDefaults | QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::RestoreDefaults)->setText(QStringLiteral("Defaults"));
    layout->addWidget(buttons);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this, [this, primary, alternate, shortcut] {
        stopRecording();
        primary->setKeySequence(shortcut.defaults.value(0), KKeySequenceWidget::Validate);
        alternate->setKeySequence(shortcut.defaults.value(1), KKeySequenceWidget::Validate);
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this, primary, alternate, shortcut] {
        stopRecording();
        const auto first = primary->keySequence(), second = alternate->keySequence();
        if (!first.isEmpty() && first == second) {
            QMessageBox::warning(this, QStringLiteral("Mattux"), QStringLiteral("Shortcut and Alternate must use different keys."));
            return;
        }
        primary->applyStealShortcut(); alternate->applyStealShortcut();
        qDBusRegisterMetaType<Sequence>(); qDBusRegisterMetaType<QList<Sequence>>();
        QList<Sequence> sequences;
        // Keep an empty primary when only Alternate is set, so both slots round-trip.
        for (const auto &key : {first, second}) {
            Sequence sequence;
            for (int i = 0; i < 4; ++i) sequence.keys << (i < key.count() ? key[i].toCombined() : 0);
            sequences << sequence;
        }
        auto message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.kglobalaccel"), QStringLiteral("/kglobalaccel"), QStringLiteral("org.kde.KGlobalAccel"), QStringLiteral("setForeignShortcutKeys"));
        message << QStringList{QStringLiteral("quick-region-overlay"), QString::fromUtf8(shortcut.id), QStringLiteral("Quick Region Overlay"), QString::fromUtf8(shortcut.title)} << QVariant::fromValue(sequences);
        const auto reply = QDBusConnection::sessionBus().call(message, QDBus::Block, 1500);
        if (reply.type() == QDBusMessage::ErrorMessage) {
            QMessageBox::warning(this, QStringLiteral("Mattux"), reply.errorMessage()); return;
        }
        accept();
    });
    setFixedSize(sizeHint().expandedTo(QSize(360, 0)));
}

void ShortcutDialog::stopRecording() {
    // KKeySequenceWidget has no cancel slot; its public recorder child does.
    // Cancel before hiding/deferred deletion, so no keyboard inhibitor survives
    // a close, Cancel, OK, or application quit. Preserve the displayed key and
    // suppress conflict prompts while the recorder restores its old sequence.
    for (auto *widget : findChildren<KKeySequenceWidget *>()) {
        if (!widget->isRecording()) continue;
        const auto key = widget->keySequence();
        const auto checks = widget->checkForConflictsAgainst();
        widget->setCheckForConflictsAgainst(KKeySequenceWidget::ShortcutTypes{});
        for (auto *recorder : widget->findChildren<KKeySequenceRecorder *>())
            if (recorder->isRecording()) recorder->cancelRecording();
        widget->setKeySequence(key);
        widget->setCheckForConflictsAgainst(checks);
    }
}
ShortcutDialog::~ShortcutDialog() { stopRecording(); }
void ShortcutDialog::done(int result) { stopRecording(); QDialog::done(result); }
void ShortcutDialog::hideEvent(QHideEvent *event) { stopRecording(); QDialog::hideEvent(event); }
