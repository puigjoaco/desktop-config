#include "tray.h"
#include "icon.h"
#include "logic.h"
#include "shortcut-dialog.h"
#include <KConfigGroup>
#include <KSharedConfig>
#include <QActionGroup>
#include <QApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QEvent>
#include <QMessageBox>
#include <QProcess>
#include <QSignalBlocker>
#include <QStyleOptionMenuItem>
#include <QPainter>
#include <QTimer>
#include <QToolButton>
#include <QWidgetAction>

namespace {
constexpr auto service = "org.local.QuickRegionOverlay";
constexpr auto interface = "org.local.QuickRegionOverlay.Stack";
class MenuTitle final : public QToolButton {
public:
    using QToolButton::QToolButton;
    QSize sizeHint() const override {
        QStyleOptionMenuItem option;
        option.initFrom(this);
        option.menuItemType = QStyleOptionMenuItem::Separator;
        option.text = defaultAction()->text();
        option.font = font();
        return style()->sizeFromContents(QStyle::CT_MenuItem, &option, QSize(fontMetrics().horizontalAdvance(option.text), fontMetrics().height()), this);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QStyleOptionMenuItem option;
        option.initFrom(this);
        // The action stays disabled for menu semantics, but a section heading
        // uses Breeze's readable title colour rather than disabled-item text.
        option.state |= QStyle::State_Enabled;
        option.palette.setCurrentColorGroup(QPalette::Active);
        option.menuItemType = QStyleOptionMenuItem::Separator;
        option.text = defaultAction()->text();
        option.font = font();
        QPainter painter(this);
        style()->drawControl(QStyle::CE_MenuItem, &option, &painter, this);
    }
};
bool daemonEnabled() {
    QProcess process;
    process.start(QStringLiteral("systemctl"), {QStringLiteral("--user"), QStringLiteral("is-enabled"), QStringLiteral("quick-region-overlay.service")});
    if (!process.waitForFinished(1500)) { process.kill(); process.waitForFinished(); return false; }
    return process.readAllStandardOutput().trimmed() == "enabled";
}
void title(QMenu *menu, const QString &text) {
    // KDE's title form: a QWidgetAction wrapping a QToolButton's default action.
    auto *action = new QWidgetAction(menu);
    action->setText(text);
    action->setObjectName(QStringLiteral("kmenu_title"));
    action->setEnabled(false);
    auto *widget = new MenuTitle(menu);
    auto *label = new QAction(text, widget);
    label->setEnabled(false);
    widget->setDefaultAction(label);
    widget->setAutoRaise(true);
    widget->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    auto font = widget->font(); font.setBold(true); widget->setFont(font);
    action->setDefaultWidget(widget);
    menu->addAction(action);
}
void reloadKlipper() {
    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.klipper"), QStringLiteral("/klipper"), QStringLiteral("org.kde.klipper.klipper"), QStringLiteral("reloadConfig"));
    message.setAutoStartService(false);
    auto *watch = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, 1500), qApp);
    QObject::connect(watch, &QDBusPendingCallWatcher::finished, watch, [watch] {
        const QDBusPendingReply<> reply = *watch;
        if (reply.isError()) QMessageBox::warning(nullptr, QStringLiteral("Mattux"), QStringLiteral("Settings saved; Klipper could not reload them: ") + reply.error().message());
        watch->deleteLater();
    });
}
}
Tray::Tray(QObject *parent) : QObject(parent) {
    item.setTitle(QStringLiteral("Mattux"));
    item.setCategory(KStatusNotifierItem::SystemServices);
    item.setStatus(KStatusNotifierItem::Active);
    item.setStandardActionsEnabled(false);
    item.setIsMenu(true);
    item.setContextMenu(menu);
    // Each tweak adds its section here; shared tray behaviour stays outside it.
    addScreenshotSection();
    menu->addSeparator();
    menu->addAction(QIcon::fromTheme(QStringLiteral("application-exit")), QStringLiteral("Quit"), qApp, &QCoreApplication::quit);
    connect(menu, &QMenu::aboutToShow, this, &Tray::refresh);
    qApp->installEventFilter(this);
    auto *watcher = new QDBusServiceWatcher(QString::fromLatin1(service), QDBusConnection::sessionBus(), QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, &Tray::ownerChanged);
    QDBusConnection::sessionBus().connect(QString::fromLatin1(service), QStringLiteral("/Stack"), QString::fromLatin1(interface), QStringLiteral("Changed"), this, SLOT(stackChanged(int,QString)));
    // Another shortcut editor can change the keys while the menu is closed.
    QTimer::singleShot(0, this, &Tray::refresh);
}
void Tray::addScreenshotSection() {
    title(menu, QStringLiteral("Screenshot && clipboard"));
    paste = menu->addAction(QIcon::fromTheme(QStringLiteral("edit-paste")), QStringLiteral("Paste stack"), this, [this] { invokeStack(QStringLiteral("Paste")); });
    clear = menu->addAction(QIcon::fromTheme(QStringLiteral("edit-clear-all")), QStringLiteral("Clear stack"), this, [this] { invokeStack(QStringLiteral("Clear")); });
    newStack = menu->addAction(QStringLiteral("Start a new stack after paste"));
    newStack->setCheckable(true);
    connect(newStack, &QAction::triggered, this, [this](bool checked) {
        auto config = KSharedConfig::openConfig(QStringLiteral("quick-region-overlayrc"), KConfig::SimpleConfig);
        KConfigGroup group(config, QStringLiteral("Stack"));
        group.writeEntry("NewStackAfterPaste", checked);
        if (!group.sync()) QMessageBox::warning(nullptr, QStringLiteral("Mattux"), QStringLiteral("Could not save the stack setting."));
        refresh();
    });
    history = menu->addMenu(QIcon::fromTheme(QStringLiteral("view-history")), QStringLiteral("Clipboard history"));
    connect(history, &QMenu::aboutToShow, this, &Tray::rebuildHistory);
    auto *shortcuts = menu->addMenu(QIcon::fromTheme(QStringLiteral("configure-shortcuts")), QStringLiteral("Shortcuts"));
    for (const auto &shortcut : Mattux::shortcuts()) {
        if (!shortcutActions.isEmpty() && shortcutActions.size() == 1) shortcuts->addSeparator();
        auto *action = shortcuts->addAction(QString::fromUtf8(shortcut.label));
        shortcutActions << action;
        connect(action, &QAction::triggered, this, [this, shortcut] {
            if (!openDialog) {
                openDialog = new ShortcutDialog(shortcut);
                connect(openDialog, &QDialog::finished, this, [this] { refresh(); });
                openDialog->show();
            }
            openDialog->raise(); openDialog->activateWindow();
        });
    }
    enabledAction = menu->addAction(QStringLiteral("Enabled"));
    enabledAction->setCheckable(true);
    connect(enabledAction, &QAction::triggered, this, &Tray::setEnabled);
}
void Tray::refresh() {
    if (!switching) enabled = daemonEnabled();
    QSignalBlocker blocker(enabledAction);
    enabledAction->setChecked(enabled);
    enabledAction->setEnabled(!switching);
    auto config = KSharedConfig::openConfig(QStringLiteral("quick-region-overlayrc"), KConfig::SimpleConfig);
    config->reparseConfiguration();
    newStack->setChecked(KConfigGroup(config, QStringLiteral("Stack")).readEntry("NewStackAfterPaste", true));
    rebuildHistory();
    for (int i = 0; i < shortcutActions.size(); ++i) {
        const auto &shortcut = Mattux::shortcuts()[i];
        shortcutActions[i]->setText(QString::fromUtf8(shortcut.label) + QLatin1Char('\t') + Mattux::keyText(readShortcut(QString::fromUtf8(shortcut.id))));
    }
    running = enabled && QDBusConnection::sessionBus().interface()->isServiceRegistered(QString::fromLatin1(service)).value();
    if (running) refreshStack();
    updateDisplay();
}
void Tray::rebuildHistory() {
    history->clear();
    auto config = KSharedConfig::openConfig(QStringLiteral("klipperrc"), KConfig::SimpleConfig);
    config->reparseConfiguration();
    KConfigGroup group(config, QStringLiteral("General"));
    const int current = group.readEntry("MaxClipItems", 20);
    title(history, QStringLiteral("History size"));
    auto *sizes = new QActionGroup(history);
    for (int size : Mattux::historySizes(current)) {
        auto *action = history->addAction(QStringLiteral("%1 items").arg(size));
        action->setCheckable(true); action->setChecked(size == current);
        sizes->addAction(action);
        connect(action, &QAction::triggered, history, [size] {
            KConfigGroup config(KSharedConfig::openConfig(QStringLiteral("klipperrc"), KConfig::SimpleConfig), QStringLiteral("General"));
            config.writeEntry("MaxClipItems", size);
            if (config.sync()) reloadKlipper();
            else QMessageBox::warning(nullptr, QStringLiteral("Mattux"), QStringLiteral("Could not save the history size."));
        });
    }
    // The group owns no actions; dispose of it with this menu generation.
    connect(history->actions().first(), &QObject::destroyed, sizes, &QObject::deleteLater);
    history->addSeparator();
    auto *images = history->addAction(QStringLiteral("Include images"));
    images->setCheckable(true); images->setChecked(!group.readEntry("IgnoreImages", false));
    connect(images, &QAction::triggered, history, [](bool checked) {
        KConfigGroup config(KSharedConfig::openConfig(QStringLiteral("klipperrc"), KConfig::SimpleConfig), QStringLiteral("General"));
        config.writeEntry("IgnoreImages", !checked);
        if (config.sync()) reloadKlipper();
        else QMessageBox::warning(nullptr, QStringLiteral("Mattux"), QStringLiteral("Could not save the image setting."));
    });
}
void Tray::refreshStack() {
    if (!enabled || !running) return;
    const auto requestGeneration = generation;
    auto message = QDBusMessage::createMethodCall(QString::fromLatin1(service), QStringLiteral("/Stack"), QString::fromLatin1(interface), QStringLiteral("State"));
    message.setAutoStartService(false);
    auto *watch = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, 1000), this);
    connect(watch, &QDBusPendingCallWatcher::finished, this, [this, watch, requestGeneration] {
        const QDBusPendingReply<QString> reply = *watch;
        if (enabled && running && generation == requestGeneration && !reply.isError()) {
            const auto state = reply.value();
            stackChanged(state.section(QLatin1Char(':'), 1).toInt(), state);
        }
        watch->deleteLater();
    });
}
void Tray::stackChanged(int newCount, const QString &state) {
    if (!enabled || !running) return;
    ++generation; // A signal supersedes any earlier snapshot still awaiting delivery.
    count = newCount; busy = Mattux::working(state);
    updateDisplay();
}
void Tray::ownerChanged(const QString &, const QString &, const QString &newOwner) {
    ++generation;
    running = enabled && !newOwner.isEmpty();
    if (running) refreshStack();
    updateDisplay();
}
void Tray::updateDisplay() {
    paste->setText(Mattux::pasteLabel(count, running) + QLatin1Char('\t') + Mattux::firstKeyText(readShortcut(QStringLiteral("stack-paste"))));
    clear->setText(QStringLiteral("Clear stack\t") + Mattux::firstKeyText(readShortcut(QStringLiteral("stack-clear"))));
    paste->setEnabled(running && count > 0 && !busy && !switching);
    clear->setEnabled(running && count > 0 && !busy && !switching);
    const auto icon = Mattux::stackIcon(count, running, qApp->palette());
    item.setIconByPixmap(icon);
    item.setToolTip(icon, QStringLiteral("Clipboard stack"), Mattux::subtitle(count, running, busy));
    QApplication::setWindowIcon(Mattux::stackIcon(0, true, qApp->palette()));
}
void Tray::invokeStack(const QString &method) {
    if (!enabled || !running || busy) return;
    // Let the menu release its input grab before the daemon injects paste.
    QTimer::singleShot(0, this, [this, method] {
        if (!enabled || !running || busy) return;
        auto message = QDBusMessage::createMethodCall(QString::fromLatin1(service), QStringLiteral("/Stack"), QString::fromLatin1(interface), method);
        message.setAutoStartService(false);
        auto *watch = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, 1500), this);
        connect(watch, &QDBusPendingCallWatcher::finished, this, [watch] {
            const QDBusPendingReply<> reply = *watch;
            if (reply.isError()) QMessageBox::warning(nullptr, QStringLiteral("Mattux"), reply.error().message());
            watch->deleteLater();
        });
    });
}
void Tray::setEnabled(bool on) {
    if (switching) return;
    switching = true;
    // Suppress calls immediately, including owner-change callbacks during systemctl.
    enabled = false; running = false; ++generation; updateDisplay();
    enabledAction->setEnabled(false);
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
        switching = false;
        if (status != QProcess::NormalExit || code != 0)
            QMessageBox::warning(nullptr, QStringLiteral("Mattux"), QStringLiteral("Could not change the screenshot service:\n") + QString::fromUtf8(process->readAllStandardError()));
        process->deleteLater(); refresh();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        switching = false; process->deleteLater(); refresh();
        QMessageBox::warning(nullptr, QStringLiteral("Mattux"), QStringLiteral("Could not start systemctl."));
    });
    process->start(QStringLiteral("systemctl"), {QStringLiteral("--user"), on ? QStringLiteral("enable") : QStringLiteral("disable"), QStringLiteral("--now"), QStringLiteral("quick-region-overlay.service")});
    QTimer::singleShot(10000, process, [process] { if (process->state() != QProcess::NotRunning) process->kill(); });
}
bool Tray::eventFilter(QObject *object, QEvent *event) {
    if (event->type() == QEvent::ApplicationPaletteChange || event->type() == QEvent::ThemeChange) updateDisplay();
    return QObject::eventFilter(object, event);
}
