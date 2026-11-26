#include "stack-busy.h"
#include <QApplication>
#include <QDateTime>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QPainter>
#include <QScreen>
#include <QSharedPointer>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QUuid>
#include <QWidget>
#include <QWindow>
#include <LayerShellQt/window.h>

namespace {
const QString pointerScript = QStringLiteral("quick-region-overlay-stack-pointer");
QDBusMessage scripting(const QString &method) {
    return QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"), QStringLiteral("org.kde.kwin.Scripting"), method);
}
class Wheel final : public QWidget {
public:
    qreal angle = 0;
    explicit Wheel(QScreen *screen) {
        setWindowTitle(QStringLiteral("Clipboard stack busy"));
        setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
        resize(18, 18);
        winId();
        windowHandle()->setScreen(screen);
        auto *layer = LayerShellQt::Window::get(windowHandle());
        layer->setScreen(screen);
        layer->setLayer(LayerShellQt::Window::LayerOverlay);
        layer->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorLeft));
        layer->setExclusiveZone(-1);
        layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        layer->setActivateOnShow(false);
        // KWin treats unknown scopes as normal windows and fades their deleted surfaces out. Utility
        // surfaces bypass its normal-window and popup animations, so hide/output changes are immediate.
        layer->setScope(QStringLiteral("utility"));
        // Qt's Wayland backend turns WindowTransparentForInput into an empty wl_surface input region.
    }
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.translate(9, 9);
        painter.rotate(angle);
        const QRectF arc(-6, -6, 12, 12);
        painter.setPen(QPen(QColor(0, 0, 0, 70), 4, Qt::SolidLine, Qt::RoundCap));
        painter.drawArc(arc, 0, 270 * 16);
        painter.setPen(QPen(QColor(235, 235, 235, 230), 2, Qt::SolidLine, Qt::RoundCap));
        painter.drawArc(arc, 0, 270 * 16);
    }
};
}

StackBusy::StackBusy(QObject *parent) : QObject(parent) {
    unload(pointerScript); // Retire a tracker left by an interrupted daemon before accepting shortcuts.
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/StackBusy"), this, QDBusConnection::ExportAllSlots);
    linger.setSingleShot(true);
    linger.setTimerType(Qt::PreciseTimer);
    connect(&linger, &QTimer::timeout, this, &StackBusy::hide);
    animation.setInterval(17); // At most 59 fps; only the visible 18 x 18 surface repaints.
    animation.setTimerType(Qt::PreciseTimer);
    connect(&animation, &QTimer::timeout, this, [this] {
        if (!visible) return;
        static_cast<Wheel *>(visible.data())->angle = (rotation.elapsed() % 1000) * 0.36;
        visible->update();
    });
    connect(qApp, &QGuiApplication::screenRemoved, this, [this](QScreen *screen) { delete windows.take(screen); });
}
StackBusy::~StackBusy() {
    stopTracking();
    hide();
    qDeleteAll(windows);
}
void StackBusy::unload(const QString &name) {
    if (name.isEmpty()) return;
    auto message = scripting(QStringLiteral("unloadScript"));
    message << name;
    QDBusConnection::sessionBus().asyncCall(message, 1000);
}
void StackBusy::setBusy(bool busy) {
    if (active == busy) return;
    active = busy;
    if (busy) {
        linger.stop();
        if (!shown.isValid()) rotation.start();
        startTracking();
        return;
    }
    // Tracking belongs to the operation, not the minimum-visible tail. Shortcuts remain ready.
    if (shown.isValid()) stopTracking();
    if (shown.isValid() && shown.elapsed() < 400) linger.start(400 - int(shown.elapsed()));
    else if (!shown.isValid() && !token.isEmpty()) {
        // A fast save may finish before the asynchronous initial cursor report arrives.
        // Keep that one report alive so even a fast operation receives visible feedback.
    } else hide();
}
void StackBusy::hide() {
    linger.stop();
    animation.stop();
    for (auto *window : windows) window->hide();
    visible.clear();
    shown.invalidate();
    token.clear();
    qInfo() << QDateTime::currentMSecsSinceEpoch() << "stack-busy-hidden";
}
void StackBusy::stopTracking() {
    unload(scriptName);
    scriptName.clear();
    trackerFile.clear();
}
void StackBusy::startTracking() {
    if (!active) return;
    stopTracking(); // Also retire an initial cursor request from a just-completed fast operation.
    token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    scriptName = pointerScript;
    // Retain each file until its asynchronous load completes, even if another busy period starts meanwhile.
    auto file = QSharedPointer<QTemporaryFile>::create(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/qro-pointer-XXXXXX.js"));
    if (!file->open()) { qWarning() << "stack-busy-pointer-file-failed"; return; }
    trackerFile = file;
    file->write(QStringLiteral(
        "var timer = new QTimer(); timer.interval = 17; timer.singleShot = true;\n"
        "function report() { var p = workspace.cursorPos; callDBus('org.local.QuickRegionOverlay', '/StackBusy', 'org.local.QuickRegionOverlay.StackBusy', 'CursorPosition', '%1', p.x, p.y); }\n"
        "timer.timeout.connect(report);\n"
        "workspace.cursorPosChanged.connect(function() { if (!timer.active) timer.start(); });\n"
        "report();\n").arg(token).toUtf8());
    file->flush();
    auto message = scripting(QStringLiteral("loadScript"));
    message << file->fileName() << scriptName;
    const QString current = token;
    auto *watch = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, 1000), this);
    connect(watch, &QDBusPendingCallWatcher::finished, this, [this, watch, file, current] {
        const QDBusPendingReply<int> reply = *watch;
        if (!reply.isError() && reply.value() >= 0) {
            if (token == current) {
                auto run = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting/Script%1").arg(reply.value()), QStringLiteral("org.kde.kwin.Script"), QStringLiteral("run"));
                auto *started = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(run, 1000), this);
                connect(started, &QDBusPendingCallWatcher::finished, this, [started, file] {
                    const QDBusPendingReply<> result = *started;
                    if (result.isError()) qWarning() << "stack-busy-pointer-start-failed" << result.error().message();
                    started->deleteLater();
                });
            }
        } else qWarning() << "stack-busy-pointer-unavailable" << reply.error().message();
        watch->deleteLater();
    });
}
void StackBusy::CursorPosition(const QString &current, int x, int y) {
    if (token.isEmpty() || current != token || (!active && shown.isValid())) return;
    auto *screen = QGuiApplication::screenAt(QPoint(x, y));
    if (!screen) { if (visible) visible->hide(); visible.clear(); return; }
    auto *&window = windows[screen];
    if (!window) window = new Wheel(screen);
    if (visible && visible != window) visible->hide();
    const QPoint local = QPoint(x, y) - screen->geometry().topLeft();
    const int left = qBound(0, local.x() + 12, qMax(0, screen->geometry().width() - window->width()));
    const int top = qBound(0, local.y() + 12, qMax(0, screen->geometry().height() - window->height()));
    LayerShellQt::Window::get(window->windowHandle())->setMargins(QMargins(left, top, 0, 0));
    visible = window;
    window->show();
    if (!shown.isValid()) {
        shown.start();
        qInfo() << QDateTime::currentMSecsSinceEpoch() << "stack-busy-shown";
        if (!active) { stopTracking(); linger.start(400); }
    }
    if (!animation.isActive()) animation.start();
}
