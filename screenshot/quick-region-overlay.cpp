#include <QAction>
#include <QApplication>
#include <QColor>
#include <QDateTime>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>
#include <QElapsedTimer>
#include <QEnterEvent>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPixelFormat>
#include <QPaintEvent>
#include <QPainter>
#include <QScreen>
#include <QSocketNotifier>
#include <QTimer>
#include <QVector>
#include <QWidget>
#include <QtMath>
#include <LayerShellQt/window.h>
#include <KGlobalAccel>
#include <KGlobalShortcutInfo>
#include <KSystemClipboard>
#include "capture-checks.h"
#include "clipboard-image.h"
#include "daemon-logic.h"
#include "selection-geometry.h"
#include "stack-controller.h"
#include "stack-holder.h"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <csignal>
#include <cstdio>
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <unistd.h>

static constexpr auto serviceName = "org.local.QuickRegionOverlay";
// KGlobalAccel identity of Meta+Shift+S; rollback.sh unregisters the same names.
static constexpr auto shortcutComponent = "quick-region-overlay";
static constexpr auto shortcutAction = "capture-region";
// The magnifier shows magnifierSample native pixels at magnifierZoom; an odd sample keeps the target pixel centred.
static constexpr int magnifierSample = 45;
static constexpr int magnifierZoom = 4;
static constexpr int magnifierSize = magnifierSample * magnifierZoom;
static constexpr int magnifierOffset = 18;
// A capture whose shape doesn't fit the layout was likely rendered for an output Qt hasn't reported yet; give Qt this
// long to catch up before the one retry a press gets (CaptureFlow::retryShape).
static constexpr int shapeRetryDelayMs = 250;
// KWin replies within tens of milliseconds; without a deadline, a call it never answers would keep every press busy
// for D-Bus's 25 s default.
static constexpr int captureReplyTimeoutMs = 2000;
// The daemon's own blocking calls to KGlobalAccel run on the event loop, so they get the same short deadline.
static constexpr int kglobalaccelTimeoutMs = 2000;
// KWin writes the pixels right after its reply; a capture whose memfd isn't full by then is given up.
static constexpr int captureWriteTimeoutMs = 2000;
static void logLine(const QString &s) { qInfo().noquote() << QDateTime::currentMSecsSinceEpoch() << s; }

class OverlayController;

class OverlayWindow final : public QWidget {
public:
    explicit OverlayWindow(OverlayController *controller);
    void setFrozen(const QImage *image, const QRect &desktopRect, QScreen *screen);
    void showFrozen() { show(); activateWindow(); update(); }
    void closeFrozen() { hide(); }
    QRect screenRect() const { return outputRect; }
protected:
    void paintEvent(QPaintEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void focusInEvent(QFocusEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
private:
    OverlayController *owner;
    const QImage *frozen = nullptr;
    QRect virtualRect;
    QRect outputRect;
    // Layer surfaces cover their whole output, so output origin + surface-local position is the global position,
    // including during the implicit grab of a drag that leaves this output.
    QPoint globalPoint(const QSinglePointEvent *ev) const { return outputRect.topLeft() + ev->position().toPoint(); }
    QRect magnifierRect(const QPoint &local) const;
};

class OverlayController final : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.local.QuickRegionOverlay")
public:
    explicit OverlayController(QObject *parent = nullptr) : QObject(parent) {
        stack.captureBusy = [this] { return flow.busy() || flow.overlayShown(); };
        connect(qApp, &QGuiApplication::screenAdded, this, [this](QScreen *) { layoutChanged(); });
        connect(qApp, &QGuiApplication::screenRemoved, this, [this](QScreen *) { layoutChanged(); });
        pollTimer.setTimerType(Qt::PreciseTimer);
        pollTimer.setInterval(1);
        connect(&pollTimer, &QTimer::timeout, this, [this] { pollCapture(); });
        rebuildWindows();
        // Connecting to the Wayland data-control clipboard takes about a second; do it before the first capture.
        KSystemClipboard::instance();
    }
    // KGlobalAccel delivers presses to this process as a D-Bus signal, so the shortcut spawns nothing. Like any KDE
    // app, the daemon offers both defaults: KGlobalAccel saves the bindings and keeps whatever the user sets in System
    // Settings, including cleared keys. A first registration while another shortcut holds either key saves the free
    // defaults only; install.sh retires conflicting pipeline owners before the first registration.
    void registerShortcut() {
        action = new QAction(QStringLiteral("Capture region to clipboard"), this);
        action->setObjectName(QString::fromLatin1(shortcutAction));
        action->setProperty("componentName", QString::fromLatin1(shortcutComponent));
        action->setProperty("componentDisplayName", QStringLiteral("Quick Region Overlay"));
        // KGlobalAccel triggers auto-repeating actions again while the key is held, which would close the overlay
        // the press just opened.
        action->setAutoRepeat(false);
        connect(action, &QAction::triggered, this, [this] { shortcutPressed(); });
        if (!QDBusConnection::sessionBus().interface()->isServiceRegistered(QStringLiteral("org.kde.kglobalaccel")))
            qCritical().noquote() << "shortcut-registration-error: org.kde.kglobalaccel is not on the session bus";
        registered = KGlobalAccel::self()->setGlobalShortcut(action, defaultShortcuts());
        if (!registered) qCritical().noquote() << "shortcut-registration-error: KGlobalAccel rejected the action";
        // KGlobalAccel's library drops D-Bus errors, so ask the server directly.
        QString error;
        liveKeys(&error);
        if (!error.isEmpty()) qCritical().noquote() << "shortcut-registration-error:" << error;
        logLine(QStringLiteral("shortcut-status %1").arg(ShortcutStatus()));
        const QList<QString> names{QStringLiteral("stack-copy"), QStringLiteral("stack-capture"), QStringLiteral("stack-paste"), QStringLiteral("stack-clear")};
        const QList<QString> labels{QStringLiteral("Copy to clipboard stack"), QStringLiteral("Capture region to clipboard stack"), QStringLiteral("Paste clipboard stack"), QStringLiteral("Clear clipboard stack")};
        const QList<Qt::Key> keys{Qt::Key_C, Qt::Key_S, Qt::Key_V, Qt::Key_X};
        for (int i = 0; i < names.size(); ++i) {
            auto *item = new QAction(labels[i], this);
            item->setObjectName(names[i]);
            item->setProperty("componentName", QString::fromLatin1(shortcutComponent));
            item->setProperty("componentDisplayName", QStringLiteral("Quick Region Overlay"));
            item->setAutoRepeat(false);
            connect(item, &QAction::triggered, this, [this, i] {
                if (i == 1) { shortcutPressed(true); return; }
                if (flow.busy() || flow.overlayShown()) { StackController::osd(QStringLiteral("Capture busy")); return; }
                if (i == 0) stack.copy();
                if (i == 2) stack.paste();
                if (i == 3) stack.clear();
            });
            stackActions << item;
            if (!KGlobalAccel::self()->setGlobalShortcut(item, {QKeySequence(Qt::CTRL | Qt::ALT | keys[i])}))
                qCritical() << "shortcut-registration-error" << names[i];
        }
        logLine(QStringLiteral("stack-shortcut-status %1").arg(StackShortcutStatus().join(QStringLiteral("; "))));
    }
    // Called after the event loop ends (SIGTERM from systemctl stop or restart). Marks the action inactive so
    // KGlobalAccel stops reporting a dead daemon as active; unlike unregistering, this keeps the saved binding.
    void deactivateShortcut() {
        for (auto *item : stackActions) deactivateAction({QString::fromLatin1(shortcutComponent), item->objectName(), QStringLiteral("Quick Region Overlay"), item->text()});
        if (registered) deactivateAction(actionId());
    }
    void shutdown() { stack.shutdown(); }
    void deactivateAction(const QStringList &id) {
        auto message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.kglobalaccel"), QStringLiteral("/kglobalaccel"), QStringLiteral("org.kde.KGlobalAccel"), QStringLiteral("setInactive"));
        message << id;
        const QDBusMessage reply = QDBusConnection::sessionBus().call(message, QDBus::Block, kglobalaccelTimeoutMs);
        if (reply.type() == QDBusMessage::ErrorMessage) qCritical().noquote() << "shortcut-deactivate-failed" << reply.errorMessage();
        else logLine(QStringLiteral("shortcut-deactivated"));
    }
public slots:
    void Ping() {}
    void Capture() { if (stack.busy()) return; appendSelection = false; startCapture(false); }
    QStringList StackShortcutStatus() const {
        QStringList result;
        for (auto *item : stackActions) {
            QString error;
            const auto keys = liveKeys(&error, {QString::fromLatin1(shortcutComponent), item->objectName(), QStringLiteral("Quick Region Overlay"), item->text()});
            result << item->objectName() + QLatin1Char('=') + (error.isEmpty() ? DaemonLogic::shortcutStatus(keys, {}) : QStringLiteral("error:") + error);
        }
        return result;
    }
    // For install.sh, from KGlobalAccel at call time: "registered:<keys>", "none" or "none:<owners of a default key>"
    // (DaemonLogic::shortcutStatus), or "error:<message>" when KGlobalAccel can't be asked. Calls queue until the
    // shortcut is registered, so the answer is never from before registration.
    QString ShortcutStatus() const {
        QString error;
        const auto keys = liveKeys(&error);
        if (!error.isEmpty()) return QStringLiteral("error:") + error;
        QStringList owners;
        if (DaemonLogic::activeKeys(keys).isEmpty()) {
            for (const auto &shortcut : defaultShortcuts())
                for (const auto &info : shortcutsByKey(shortcut, &error)) {
                    if (info.componentUniqueName() == QLatin1String(shortcutComponent)) continue;
                    const QString owner = info.componentUniqueName() + QLatin1Char('/') + info.uniqueName();
                    if (!owners.contains(owner)) owners << owner;
                }
            if (!error.isEmpty()) return QStringLiteral("error:") + error;
        }
        return DaemonLogic::shortcutStatus(keys, owners);
    }
private:
    // Who holds a single-chord key, as KGlobalAccel::globalShortcutsByKey(key) with its default MatchType::Equal
    // reports it, but with a deadline instead of D-Bus's 25 s default; error is set when the call fails. The
    // int-based getGlobalShortcutsByKey runs the same lookup and sends no QKeySequence: a malformed (ai) argument
    // aborts kglobalacceld, and with it KWin.
    static QList<KGlobalShortcutInfo> shortcutsByKey(const QKeySequence &key, QString *error) {
        auto message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.kglobalaccel"), QStringLiteral("/kglobalaccel"), QStringLiteral("org.kde.KGlobalAccel"), QStringLiteral("getGlobalShortcutsByKey"));
        message << key[0].toCombined();
        const QDBusMessage reply = QDBusConnection::sessionBus().call(message, QDBus::Block, kglobalaccelTimeoutMs);
        if (reply.type() != QDBusMessage::ReplyMessage) { *error = reply.errorMessage().isEmpty() ? QStringLiteral("no reply") : reply.errorMessage(); return {}; }
        if (reply.signature() != QLatin1String("a(ssssssaiai)")) { *error = QStringLiteral("unexpected getGlobalShortcutsByKey reply %1").arg(reply.signature()); return {}; }
        const auto argument = reply.arguments().constFirst().value<QDBusArgument>();
        QList<KGlobalShortcutInfo> result;
        argument.beginArray();
        while (!argument.atEnd()) {
            KGlobalShortcutInfo info;
            argument >> info;
            result << info;
        }
        argument.endArray();
        return result;
    }
    // Single chords: shortcutsByKey looks up only a sequence's first chord.
    static QList<QKeySequence> defaultShortcuts() { return {QKeySequence(Qt::META | Qt::SHIFT | Qt::Key_S), QKeySequence(Qt::Key_Print)}; }
    QStringList actionId() const { return {QString::fromLatin1(shortcutComponent), QString::fromLatin1(shortcutAction), QStringLiteral("Quick Region Overlay"), action ? action->text() : QString()}; }
    // The action's current keys as the KGlobalAccel server has them; error is set when the call fails. shortcutKeys
    // sends whole sequences as a(ai); the older shortcut method keeps only each sequence's first chord.
    QList<QKeySequence> liveKeys(QString *error, const QStringList &id = {}) const {
        auto message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.kglobalaccel"), QStringLiteral("/kglobalaccel"), QStringLiteral("org.kde.KGlobalAccel"), QStringLiteral("shortcutKeys"));
        message << (id.isEmpty() ? actionId() : id);
        const QDBusMessage reply = QDBusConnection::sessionBus().call(message, QDBus::Block, kglobalaccelTimeoutMs);
        if (reply.type() != QDBusMessage::ReplyMessage) { *error = reply.errorMessage().isEmpty() ? QStringLiteral("no reply") : reply.errorMessage(); return {}; }
        if (reply.signature() != QLatin1String("a(ai)")) { *error = QStringLiteral("unexpected shortcutKeys reply %1").arg(reply.signature()); return {}; }
        const auto argument = reply.arguments().constFirst().value<QDBusArgument>();
        QList<QKeySequence> keys;
        argument.beginArray();
        while (!argument.atEnd()) {
            QList<int> chords;
            argument.beginStructure();
            argument >> chords;
            argument.endStructure();
            keys << DaemonLogic::keySequence(chords);
        }
        argument.endArray();
        return keys;
    }
    // A press of the shortcut. Unlike the D-Bus Capture method, it also closes an open overlay, like Escape.
    void shortcutPressed(bool toStack = false) {
        if (stack.busy()) { StackController::osd(QStringLiteral("Stack busy")); return; }
        logLine(QStringLiteral("shortcut-pressed"));
        switch (flow.shortcutPress()) {
        case DaemonLogic::CaptureFlow::Press::Cancel: closeOverlay(); logLine(QStringLiteral("shortcut-cancel")); break;
        case DaemonLogic::CaptureFlow::Press::Busy: logLine(QStringLiteral("capture-busy")); break;
        case DaemonLogic::CaptureFlow::Press::Capture:
            if (toStack && !stack.canAdd()) return;
            appendSelection = toStack; startCapture(false); break;
        }
    }
    // retry marks the capture a press gets after a rebuild (CaptureFlow).
    void startCapture(bool retry) {
        if (flow.busy()) { logLine(QStringLiteral("capture-busy")); return; }
        if (!retry) flow.pressed(DaemonLogic::CaptureFlow::Clock::now());
        // Qt may not signal a scale-only change, so a press checks the layout itself and captures after the rebuild.
        if (!rebuildPending && currentLayout() != screens) { logLine(QStringLiteral("capture-layout-stale")); layoutChanged(); }
        if (rebuildPending) { flow.defer(); logLine(QStringLiteral("capture-after-rebuild")); return; }
        if (screens.isEmpty()) { flow.failed(); logLine(QStringLiteral("capture-no-screens")); return; }
        const quint64 serial = flow.begin();
        timer.restart();
        logLine(QStringLiteral("capture-request"));
        captureWorkspace(serial);
    }
    struct ScreenState {
        QScreen *screen;
        QRect geometry;
        qreal dpr;
        bool operator==(const ScreenState &other) const { return screen == other.screen && geometry == other.geometry && qFuzzyCompare(dpr, other.dpr); }
    };
    // A KWin reply whose pixels are still being written to the memfd.
    struct PendingCapture {
        int fd = -1;
        quint64 serial = 0;
        QVariantMap meta;
        qint64 needed = 0;
        qint64 replyMs = 0;
        QElapsedTimer waited;
    };
    QVector<OverlayWindow *> windows;
    QVector<ScreenState> screens;
    QImage desktop;
    QRect virtualRect;
    QElapsedTimer timer;
    DaemonLogic::CaptureFlow flow;
    PendingCapture pending;
    QTimer pollTimer;
    bool rebuildPending = false;
    bool mouseDown = false;
    bool dragging = false;
    bool shiftHeld = false;
    bool pointerKnown = false;
    QPoint pressGlobal;
    QPoint currentGlobal;
    QElapsedTimer dragTimer;
    QVector<qint64> dragIntervals;
    QElapsedTimer frameTimer;
    QVector<qint64> frameIntervals;
    QAction *action = nullptr;
    bool registered = false;
    QList<QAction *> stackActions;
    StackController stack{this};
    bool appendSelection = false;

    static QVector<ScreenState> currentLayout() {
        QVector<ScreenState> result;
        for (QScreen *screen : QGuiApplication::screens()) result.append({screen, screen->geometry(), screen->devicePixelRatio()});
        return result;
    }
    static QRect boundingRect(const QVector<ScreenState> &layout) {
        QRect result;
        for (const auto &state : layout) result |= state.geometry;
        return result;
    }
    static qreal maxScale(const QVector<ScreenState> &layout) {
        qreal result = 1;
        for (const auto &state : layout) result = qMax(result, state.dpr);
        return result;
    }
    void rebuildWindows() {
        rebuildPending = false;
        for (auto *window : windows) { window->closeFrozen(); window->deleteLater(); }
        windows.clear();
        const auto alive = QGuiApplication::screens();
        for (const auto &state : std::as_const(screens)) if (alive.contains(state.screen)) disconnect(state.screen, nullptr, this, nullptr);
        screens = currentLayout();
        for (const auto &state : std::as_const(screens)) {
            windows.append(new OverlayWindow(this));
            connect(state.screen, &QScreen::geometryChanged, this, [this](const QRect &) { layoutChanged(); });
            connect(state.screen, &QScreen::physicalDotsPerInchChanged, this, [this](qreal) { layoutChanged(); });
            connect(state.screen, &QScreen::logicalDotsPerInchChanged, this, [this](qreal) { layoutChanged(); });
        }
        logLine(QStringLiteral("layout screens=%1 virtual=%2x%3").arg(screens.size()).arg(boundingRect(screens).width()).arg(boundingRect(screens).height()));
        switch (flow.rebuilt(DaemonLogic::CaptureFlow::Clock::now())) {
        case DaemonLogic::CaptureFlow::AfterRebuild::Retry: QTimer::singleShot(0, this, [this] { startCapture(true); }); break;
        case DaemonLogic::CaptureFlow::AfterRebuild::Dropped: logLine(QStringLiteral("capture-dropped layout kept changing")); break;
        case DaemonLogic::CaptureFlow::AfterRebuild::Nothing: break;
        }
    }
    void layoutChanged() {
        if (!rebuildPending && currentLayout() == screens) return;
        scheduleRebuild(0);
    }
    void scheduleRebuild(int delayMs) {
        if (flow.layoutChanged()) { stopPolling(); logLine(QStringLiteral("capture-aborted-layout-change")); }
        if (flow.overlayShown()) { closeOverlay(); logLine(QStringLiteral("overlay-closed-layout-change")); }
        // Rebuild after Qt finishes its own screen bookkeeping; Capture waits until then.
        if (!rebuildPending) { rebuildPending = true; QTimer::singleShot(delayMs, this, [this] { rebuildWindows(); }); }
    }
    QPoint imagePointFor(const QPoint &global) const { return SelectionGeometry::imagePoint(global, virtualRect, desktop.size()); }
    QRect selectionGlobal() const { return SelectionGeometry::selectionRect(pressGlobal, currentGlobal); }
    void captureFailed(const QString &reason) {
        stopPolling();
        flow.failed();
        logLine(QStringLiteral("capture-error %1").arg(reason));
    }
    // KWin's ScreenShot2 replies first and then writes the pixels from a worker thread. A memfd lets it write the
    // whole image in one call instead of 64 KiB pipe chunks; the daemon waits until the file holds every byte,
    // then maps it without copying.
    void captureWorkspace(quint64 serial) {
        const int fd = memfd_create("quick-region-capture", MFD_CLOEXEC | MFD_ALLOW_SEALING);
        if (fd < 0) { captureFailed(QStringLiteral("memfd %1").arg(strerror(errno))); return; }
        // KWin only writes. Sealed against shrinking, the file can't be truncated under the mapping, which would
        // turn reads of the frozen image into SIGBUS.
        if (fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_SEAL) < 0) { const int error = errno; ::close(fd); captureFailed(QStringLiteral("memfd-seal %1").arg(strerror(error))); return; }
        QVariantMap options;
        options.insert(QStringLiteral("include-cursor"), false);
        // Also exclude any just-unmapped busy surface still awaiting a compositor frame.
        options.insert(QStringLiteral("hide-caller-windows"), true);
        options.insert(QStringLiteral("native-resolution"), true);
        auto message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/org/kde/KWin/ScreenShot2"), QStringLiteral("org.kde.KWin.ScreenShot2"), QStringLiteral("CaptureWorkspace"));
        message << options << QVariant::fromValue(QDBusUnixFileDescriptor(fd));
        auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, captureReplyTimeoutMs), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, fd, serial](QDBusPendingCallWatcher *) {
            QDBusPendingReply<QVariantMap> reply = *watcher;
            watcher->deleteLater();
            if (!flow.isCurrent(serial)) { ::close(fd); logLine(QStringLiteral("capture-discarded-stale")); return; }
            if (reply.isError()) { ::close(fd); captureFailed(QStringLiteral("kwin %1").arg(reply.error().message())); return; }
            captureReplied(fd, reply.value(), serial);
        });
    }
    static QSize metaSize(const QVariantMap &meta) { return QSize(meta.value(QStringLiteral("width")).toInt(), meta.value(QStringLiteral("height")).toInt()); }
    // A capture rendered for another layout is discarded and the press captures again after a rebuild: the layout
    // changed without a Qt signal, or KWin rendered a layout Qt hasn't reported yet (the image's shape doesn't fit).
    // A press's second shape mismatch fails instead, since the layout Qt reports is then most likely final.
    bool discardedForLayout(const QSize &imageSize) {
        const bool layoutStale = currentLayout() != screens;
        const bool shapeStale = !layoutStale && CaptureChecks::desktopScale(imageSize, boundingRect(screens), maxScale(screens)) <= 0;
        if (!layoutStale && !shapeStale) return false;
        const QString shape = QStringLiteral("image=%1x%2 desktop=%3x%4").arg(imageSize.width()).arg(imageSize.height()).arg(boundingRect(screens).width()).arg(boundingRect(screens).height());
        if (layoutStale) {
            logLine(QStringLiteral("capture-discarded-stale-layout"));
            stopPolling();
            flow.retryAfterRebuild();
            scheduleRebuild(0);
        } else if (flow.retryShape()) {
            logLine(QStringLiteral("capture-discarded-shape %1").arg(shape));
            stopPolling();
            scheduleRebuild(shapeRetryDelayMs);
        } else {
            captureFailed(QStringLiteral("shape %1").arg(shape));
        }
        return true;
    }
    void captureReplied(int fd, const QVariantMap &meta, quint64 serial) {
        stopPolling();
        pending.fd = fd;
        pending.serial = serial;
        pending.meta = meta;
        pending.replyMs = timer.elapsed();
        // Reject malformed metadata before checking its shape; an unusable size must be logged as such, not retried
        // as a layout change.
        if (CaptureChecks::firstReplyCheck(meta) == CaptureChecks::ReplyCheck::UnusableBuffer) {
            const char *problem = CaptureChecks::rawBufferProblem(meta);
            captureFailed(QStringLiteral("unusable-reply %1 type=%2 %3x%4 stride=%5 format=%6").arg(QLatin1String(problem), meta.value(QStringLiteral("type")).toString()).arg(metaSize(meta).width()).arg(metaSize(meta).height()).arg(meta.value(QStringLiteral("stride")).toInt()).arg(meta.value(QStringLiteral("format")).toUInt()));
            return;
        }
        // Check the layout before waiting for pixels that would be thrown away.
        if (discardedForLayout(metaSize(meta))) return;
        pending.needed = CaptureChecks::rawBufferSize(meta);
        pending.waited.start();
        pollCapture();
        if (pending.fd >= 0) pollTimer.start();
    }
    void stopPolling() {
        pollTimer.stop();
        if (pending.fd >= 0) ::close(pending.fd);
        pending.fd = -1;
    }
    // Polls from the event loop until KWin has written the whole image, so input and D-Bus keep flowing meanwhile.
    void pollCapture() {
        if (pending.fd < 0) { pollTimer.stop(); return; }
        if (!flow.isCurrent(pending.serial)) { stopPolling(); logLine(QStringLiteral("capture-discarded-stale")); return; }
        struct stat st;
        if (fstat(pending.fd, &st) != 0) { captureFailed(QStringLiteral("fstat %1").arg(strerror(errno))); return; }
        if (st.st_size < pending.needed) {
            if (pending.waited.elapsed() > captureWriteTimeoutMs) captureFailed(QStringLiteral("write-timeout size=%1 needed=%2").arg(qint64(st.st_size)).arg(pending.needed));
            return;
        }
        pollTimer.stop();
        if (discardedForLayout(metaSize(pending.meta))) return;
        const size_t size = size_t(pending.needed);
        void *data = mmap(nullptr, size, PROT_READ, MAP_SHARED, pending.fd, 0);
        if (data == MAP_FAILED) { captureFailed(QStringLiteral("mmap %1").arg(strerror(errno))); return; }
        stopPolling();
        // Read-only image over the mapping; dropping the last copy unmaps it. QImage skips the cleanup when it
        // rejects the data, so unmap it here then.
        const QVariantMap &meta = pending.meta;
        auto *mapping = new Mapping{data, size};
        QImage image(static_cast<const uchar *>(data), meta.value(QStringLiteral("width")).toInt(), meta.value(QStringLiteral("height")).toInt(),
                     meta.value(QStringLiteral("stride")).toInt(), QImage::Format(meta.value(QStringLiteral("format")).toUInt()), unmapCapture, mapping);
        if (image.isNull()) { unmapCapture(mapping); captureFailed(QStringLiteral("qimage-rejected format=%1").arg(meta.value(QStringLiteral("format")).toUInt())); return; }
        showCapture(std::move(image));
    }
    struct Mapping { void *data; size_t size; };
    static void unmapCapture(void *info) { auto *mapping = static_cast<Mapping *>(info); munmap(mapping->data, mapping->size); delete mapping; }
    void showCapture(QImage image) {
        flow.captured();
        desktop = std::move(image);
        virtualRect = boundingRect(screens);
        logLine(QStringLiteral("capture-complete reply_ms=%1 ms=%2 image=%3x%4 format=%5 scale=%6x%7").arg(pending.replyMs).arg(timer.elapsed()).arg(desktop.width()).arg(desktop.height()).arg(int(desktop.format()))
                    .arg(qreal(desktop.width()) / virtualRect.width(), 0, 'f', 3).arg(qreal(desktop.height()) / virtualRect.height(), 0, 'f', 3));
        for (int i = 0; i < windows.size(); ++i) windows[i]->setFrozen(&desktop, virtualRect, screens[i].screen);
        dragging = false; mouseDown = false; shiftHeld = shiftDown(); pointerKnown = false;
        for (auto *window : windows) window->showFrozen();
        logLine(QStringLiteral("overlay-shown ms=%1 windows=%2").arg(timer.elapsed()).arg(windows.size()));
    }
    static void offerImage(const QImage &image) {
        KSystemClipboard::instance()->setMimeData(ClipboardImage::mimeData(image), QClipboard::Clipboard);
    }
    void finishSelection() {
        const QRect clipped = selectionGlobal().intersected(virtualRect);
        const QRect imageRect = SelectionGeometry::selectionCrop(pressGlobal, currentGlobal, virtualRect, desktop.size());
        if (imageRect.isEmpty()) { logLine(QStringLiteral("selection-empty")); closeOverlay(); return; }
        const qint64 releaseMs = timer.elapsed();
        const QImage selected = desktop.copy(imageRect);
        const bool toStack = appendSelection;
        closeOverlay();
        if (toStack) stack.appendImage(selected);
        else offerImage(selected);
        const QString event = toStack ? QStringLiteral("stack-capture-queued") : QStringLiteral("clipboard-offer-direct");
        logLine(event + QStringLiteral(" rect=%1,%2 %3x%4 native=%5x%6 release_ms=%7 copy_and_queue_or_offer_ms=%8").arg(clipped.x()).arg(clipped.y()).arg(clipped.width()).arg(clipped.height()).arg(selected.width()).arg(selected.height()).arg(releaseMs).arg(timer.elapsed() - releaseMs));
    }
    void closeOverlay() {
        appendSelection = false;
        flow.closed(); mouseDown = false; dragging = false; shiftHeld = false; pointerKnown = false;
        for (auto *window : windows) window->closeFrozen();
        desktop = QImage();
        logLine(QStringLiteral("overlay-closed"));
    }
    static bool shiftDown() { return QGuiApplication::queryKeyboardModifiers().testFlag(Qt::ShiftModifier); }
    // Shift held since before the overlay got keyboard focus produces no key event.
    void syncShift() {
        const bool held = shiftDown();
        if (!flow.overlayShown() || held == shiftHeld) return;
        shiftHeld = held;
        if (pointerKnown) updateAround(currentGlobal);
    }
    void hover(const QPoint &point, Qt::KeyboardModifiers modifiers) {
        if (!flow.overlayShown()) return;
        const QPoint previous = currentGlobal;
        const bool wasKnown = pointerKnown, wasShift = shiftHeld;
        currentGlobal = point; pointerKnown = true;
        shiftHeld = modifiers.testFlag(Qt::ShiftModifier);
        if (wasShift || shiftHeld) { if (wasKnown) updateAround(previous); updateAround(point); }
    }
    void beginDrag(const QPoint &point) {
        if (!flow.overlayShown()) return;
        hover(point, QGuiApplication::queryKeyboardModifiers());
        mouseDown = true; pressGlobal = currentGlobal = point; dragging = false;
        dragIntervals.clear(); frameIntervals.clear(); dragTimer.restart(); frameTimer.invalidate();
        logLine(QStringLiteral("press-at %1,%2").arg(point.x()).arg(point.y()));
    }
    void moveDrag(const QPoint &point, Qt::KeyboardModifiers modifiers) {
        const QRect previousSelection = dragging ? selectionGlobal() : QRect();
        hover(point, modifiers);
        const qint64 interval = dragTimer.restart();
        if (interval > 0 && interval < 1000) dragIntervals.append(interval);
        dragging = dragging || (currentGlobal - pressGlobal).manhattanLength() >= 3;
        if (dragging) updateRect(previousSelection.united(selectionGlobal()).adjusted(-2, -2, 2, 2));
    }
    void endDrag(const QPoint &point) {
        if (!mouseDown) return;
        mouseDown = false;
        currentGlobal = point;
        if (!dragging) { logLine(QStringLiteral("click-no-capture")); return; }
        logLine(QStringLiteral("drag-stats input_samples=%1 input_median_ms=%2 input_p95_ms=%3 frame_samples=%4 frame_median_ms=%5 frame_p95_ms=%6 frame_max_ms=%7 magnifier=%8")
                    .arg(dragIntervals.size()).arg(percentile(dragIntervals, 0.50)).arg(percentile(dragIntervals, 0.95))
                    .arg(frameIntervals.size()).arg(percentile(frameIntervals, 0.50)).arg(percentile(frameIntervals, 0.95)).arg(percentile(frameIntervals, 1.0)).arg(shiftHeld));
        finishSelection();
    }
    static qint64 percentile(QVector<qint64> values, double p) {
        if (values.isEmpty()) return -1;
        std::sort(values.begin(), values.end());
        return values.at(qBound(0, int(std::ceil(p * values.size())) - 1, int(values.size()) - 1));
    }
    void framePainted(const OverlayWindow *window) {
        if (!dragging || !window->screenRect().contains(currentGlobal)) return;
        if (frameTimer.isValid()) frameIntervals.append(frameTimer.restart()); else frameTimer.start();
    }
    void updateRect(const QRect &global) {
        for (auto *window : windows) {
            const QRect local = global.intersected(window->screenRect()).translated(-window->screenRect().topLeft());
            if (!local.isEmpty()) window->update(local);
        }
    }
    void updateAround(const QPoint &point) {
        const int reach = magnifierOffset + magnifierSize + 4;
        updateRect(QRect(point - QPoint(reach, reach), QSize(2 * reach, 2 * reach)));
    }
    void cancel() { if (flow.overlayShown()) { closeOverlay(); logLine(QStringLiteral("escape-cancel")); } }
    void keyPress(QKeyEvent *event) {
        if (event->key() == Qt::Key_Escape) { cancel(); return; }
        if (event->key() == Qt::Key_Shift && !shiftHeld) { shiftHeld = true; if (pointerKnown) updateAround(currentGlobal); }
    }
    void keyRelease(QKeyEvent *event) {
        if (event->key() == Qt::Key_Shift && shiftHeld) { shiftHeld = false; if (pointerKnown) updateAround(currentGlobal); }
    }
    friend class OverlayWindow;
};

OverlayWindow::OverlayWindow(OverlayController *controller) : QWidget(nullptr), owner(controller) {
    setWindowFlags(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::CrossCursor);
    winId();
    auto *layer = LayerShellQt::Window::get(windowHandle());
    layer->setLayer(LayerShellQt::Window::LayerOverlay);
    layer->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom | LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight));
    layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityExclusive);
    layer->setExclusiveZone(-1);
    layer->setScope(QStringLiteral("quick-region-overlay"));
    layer->setActivateOnShow(true);
}
void OverlayWindow::setFrozen(const QImage *image, const QRect &desktopRect, QScreen *screen) {
    frozen = image; virtualRect = desktopRect; outputRect = screen->geometry();
    windowHandle()->setScreen(screen); setGeometry(outputRect);
}
void OverlayWindow::keyPressEvent(QKeyEvent *event) { owner->keyPress(event); }
void OverlayWindow::keyReleaseEvent(QKeyEvent *event) { owner->keyRelease(event); }
// QEnterEvent carries no modifiers, so ask for the keyboard state.
void OverlayWindow::enterEvent(QEnterEvent *event) { if (!owner->mouseDown) owner->hover(globalPoint(event), QGuiApplication::queryKeyboardModifiers()); }
void OverlayWindow::focusInEvent(QFocusEvent *event) { QWidget::focusInEvent(event); owner->syncShift(); }
void OverlayWindow::mousePressEvent(QMouseEvent *event) { if (event->button() == Qt::LeftButton) owner->beginDrag(globalPoint(event)); }
void OverlayWindow::mouseMoveEvent(QMouseEvent *event) {
    if (owner->mouseDown) owner->moveDrag(globalPoint(event), event->modifiers());
    else owner->hover(globalPoint(event), event->modifiers());
}
void OverlayWindow::mouseReleaseEvent(QMouseEvent *event) { if (event->button() == Qt::LeftButton) owner->endDrag(globalPoint(event)); }
QRect OverlayWindow::magnifierRect(const QPoint &local) const {
    QPoint topLeft = local + QPoint(magnifierOffset, magnifierOffset);
    if (topLeft.x() + magnifierSize > width()) topLeft.setX(local.x() - magnifierSize - magnifierOffset);
    if (topLeft.y() + magnifierSize > height()) topLeft.setY(local.y() - magnifierSize - magnifierOffset);
    topLeft.setX(qBound(4, topLeft.x(), qMax(4, width() - magnifierSize - 4)));
    topLeft.setY(qBound(4, topLeft.y(), qMax(4, height() - magnifierSize - 4)));
    return QRect(topLeft, QSize(magnifierSize, magnifierSize));
}
void OverlayWindow::paintEvent(QPaintEvent *event) {
    if (!frozen || frozen->isNull()) return;
    const auto &controller = *owner;
    QPainter painter(this);
    const QRect dirty = event->rect();
    painter.drawImage(QRectF(dirty), *frozen, SelectionGeometry::sourceRect(dirty, outputRect.topLeft(), virtualRect, frozen->size()));
    painter.fillRect(dirty, QColor(0, 0, 0, 100));
    if (controller.dragging) {
        const QRect selection = controller.selectionGlobal().translated(-outputRect.topLeft());
        if (selection.intersects(dirty.adjusted(-2, -2, 2, 2))) {
            // Undim the selection so it shows the exact frozen pixels that will be copied.
            const QRect inside = selection.intersected(dirty);
            painter.drawImage(QRectF(inside), *frozen, SelectionGeometry::sourceRect(inside, outputRect.topLeft(), virtualRect, frozen->size()));
            painter.setPen(QPen(Qt::white, 1));
            painter.drawRect(selection.adjusted(0, 0, -1, -1));
        }
    }
    if (controller.shiftHeld && controller.pointerKnown && outputRect.contains(controller.currentGlobal)) {
        const QRect box = magnifierRect(controller.currentGlobal - outputRect.topLeft());
        if (box.adjusted(-2, -2, 2, 2).intersects(dirty)) {
            const QPoint center = controller.imagePointFor(controller.currentGlobal);
            const QRect sample(center - QPoint(magnifierSample / 2, magnifierSample / 2), QSize(magnifierSample, magnifierSample));
            painter.fillRect(box, Qt::black);
            // Copy the sample with edge clipping, then scale it by an integer factor without smoothing.
            QImage zoomed(magnifierSample, magnifierSample, QImage::Format_ARGB32_Premultiplied);
            zoomed.fill(Qt::black);
            { QPainter samplePainter(&zoomed); samplePainter.drawImage(sample.intersected(frozen->rect()).topLeft() - sample.topLeft(), *frozen, sample.intersected(frozen->rect())); }
            painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
            painter.drawImage(box, zoomed);
            const QPoint mid = box.topLeft() + QPoint((magnifierSample / 2) * magnifierZoom, (magnifierSample / 2) * magnifierZoom);
            painter.setPen(QPen(Qt::red, 1));
            painter.drawRect(QRect(mid - QPoint(1, 1), QSize(magnifierZoom + 1, magnifierZoom + 1)));
            painter.setPen(QPen(Qt::white, 2));
            painter.drawRect(box.adjusted(-1, -1, 0, 0));
        }
    }
    owner->framePainted(this);
}

// Only quick-region-overlay.service runs the daemon (--daemon). --capture asks it for a capture over D-Bus, which
// D-Bus-activates the service when it isn't running; this process never registers anything. The call keeps D-Bus's
// default deadline: this short-lived client blocks nothing else, and an activation that starts the service can take
// longer than the daemon's 2 s deadlines.
static int requestCapture(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QDBusMessage reply = QDBusConnection::sessionBus().call(QDBusMessage::createMethodCall(QString::fromLatin1(serviceName), QStringLiteral("/"), QString::fromLatin1(serviceName), QStringLiteral("Capture")));
    if (reply.type() == QDBusMessage::ErrorMessage) { qCritical().noquote() << "capture-request-failed" << reply.errorMessage(); return 1; }
    return 0;
}

int main(int argc, char **argv) {
    const QStringList args = [&] { QStringList list; for (int i = 1; i < argc; ++i) list << QString::fromLocal8Bit(argv[i]); return list; }();
    switch (DaemonLogic::parseCommand(args)) {
    case DaemonLogic::Command::Capture: return requestCapture(argc, argv);
    case DaemonLogic::Command::HoldClipboard: return holdClipboard(argc, argv, args[1]);
    case DaemonLogic::Command::Usage:
        fprintf(stderr, "usage: %s --capture   capture a region (starts quick-region-overlay.service if needed)\n"
                        "       %s --daemon    run the daemon; only quick-region-overlay.service does this\n"
                        "       %s --hold-clipboard <file>    serve a shutdown clipboard snapshot\n", argv[0], argv[0], argv[0]);
        return DaemonLogic::usageStatus;
    case DaemonLogic::Command::Daemon: break;
    }
    // systemctl stop and restart send SIGTERM. Handle it in the event loop so the shortcut is marked inactive on the
    // way out; blocking it before Qt starts its threads keeps it off every thread.
    sigset_t stopSignals;
    sigemptyset(&stopSignals);
    for (int signal : {SIGTERM, SIGINT, SIGHUP}) sigaddset(&stopSignals, signal);
    pthread_sigmask(SIG_BLOCK, &stopSignals, nullptr);
    const int signalFd = signalfd(-1, &stopSignals, SFD_NONBLOCK | SFD_CLOEXEC);
    if (signalFd < 0) {
        const int error = errno;
        pthread_sigmask(SIG_UNBLOCK, &stopSignals, nullptr);
        fprintf(stderr, "signalfd-error %s: stop signals end the daemon without marking its shortcut inactive\n", strerror(error));
    }
    QCoreApplication::setAttribute(Qt::AA_DisableSessionManager);
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Quick Region Overlay"));
    app.setQuitOnLastWindowClosed(false);
    if (signalFd >= 0) {
        auto *notifier = new QSocketNotifier(signalFd, QSocketNotifier::Read, &app);
        QObject::connect(notifier, &QSocketNotifier::activated, &app, [signalFd] {
            signalfd_siginfo info;
            if (::read(signalFd, &info, sizeof info) == sizeof info) logLine(QStringLiteral("service-stopping signal=%1").arg(info.ssi_signo));
            QCoreApplication::quit();
        });
    }
    auto bus = QDBusConnection::sessionBus();
    if (const auto status = DaemonLogic::alreadyRunning(bus.interface()->isServiceRegistered(QString::fromLatin1(serviceName)))) { qCritical() << "service already running"; return *status; }
    OverlayController controller;
    if (!bus.registerObject(QStringLiteral("/"), &controller, QDBusConnection::ExportAllSlots)) { qCritical() << "object registration failed"; return 2; }
    // Own the name before registering the shortcut. KGlobalAccel deactivates an action when its QAction is destroyed,
    // even while the running daemon uses it, so a process that loses the name must never register it. Activation
    // callers' messages wait for app.exec(), after the registration.
    if (const auto status = DaemonLogic::alreadyRunning(!bus.registerService(QString::fromLatin1(serviceName)))) { qCritical() << "service already running"; return *status; }
    controller.registerShortcut();
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &controller, [&controller] { controller.shutdown(); });
    logLine(QStringLiteral("service-ready pid=%1 screens=%2").arg(QCoreApplication::applicationPid()).arg(QGuiApplication::screens().size()));
    const int status = app.exec();
    controller.shutdown(); // Also covers an event-loop exit that didn't emit aboutToQuit.
    controller.deactivateShortcut();
    return status;
}
#include "quick-region-overlay.moc"
