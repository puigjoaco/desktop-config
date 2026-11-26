// Dedicated editor/MIME receiver for unattended clipboard-stack integration checks.
#include "../stack-clipboard.h"
#include <QApplication>
#include <QPlainTextEdit>
#include <QClipboard>
#include <QDBusConnection>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QBuffer>
#include <QImage>
#include <QMimeData>
#include <QTimer>
#include <QCryptographicHash>
#include <QSaveFile>
#include <QFile>
#include <QDBusMessage>
#include <QDBusArgument>
#include <QDBusUnixFileDescriptor>
#include <QDateTime>
#include <QMouseEvent>
#include <QWheelEvent>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits>

using namespace ClipboardStack;
static QJsonObject json(const Item &item) {
    QJsonObject result;
    for (auto it = item.begin(); it != item.end(); ++it) result[it.key()] = QString::fromLatin1(it.value().toBase64());
    return result;
}
class Probe final : public QPlainTextEdit {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.local.StackProbe")
public:
    StackClipboard raw;
    Item source;
    QJsonArray events;
    QJsonObject snapshot;
    Item saved, lastSnapshot;
    bool saveNext = false, savedReady = false;
    quint64 serial = 0;
    bool custom = false;
    bool suppressCopy = false;
    bool ignorePaste = false;
    int pasteDelay = 0;
    int clicks = 0, scrolls = 0;
    QJsonArray windows, activations;
    QString windowState;
    explicit Probe() {
        resize(800, 600);
        setWindowTitle(QStringLiteral("QRO dedicated stack test"));
        connect(&raw, &StackClipboard::snapshotReady, this, [this](quint64, const Item &item, const QString &error) {
            lastSnapshot = item;
            snapshot = json(item);
            snapshot[QStringLiteral("_error")] = error;
            if (saveNext) { savedReady = error.isEmpty(); saved = item; saveNext = false; }
        });
    }
    void receivePaste(bool shift) {
        Item item;
        const auto *mime = QApplication::clipboard()->mimeData();
        if (mime) for (const QString &format : mime->formats()) item.insert(format, mime->data(format));
        auto entry = json(item);
        entry[QStringLiteral("_key")] = shift ? QStringLiteral("Ctrl+Shift+V") : QStringLiteral("Ctrl+V");
        events << entry;
    }
    void keyPressEvent(QKeyEvent *event) override {
        if (suppressCopy && event->key() == Qt::Key_C && event->modifiers().testFlag(Qt::ControlModifier)) {
            events << QJsonObject{{QStringLiteral("_copy"), true}, {QStringLiteral("alt"), event->modifiers().testFlag(Qt::AltModifier)}, {QStringLiteral("shift"), event->modifiers().testFlag(Qt::ShiftModifier)}};
            event->accept(); return;
        }
        if (event->key() == Qt::Key_V && event->modifiers().testFlag(Qt::ControlModifier)) {
            if (!ignorePaste) {
                const bool shift = event->modifiers().testFlag(Qt::ShiftModifier);
                if (pasteDelay) QTimer::singleShot(pasteDelay, this, [this, shift] { receivePaste(shift); });
                else receivePaste(shift);
            }
            event->accept();
            return;
        }
        if (custom && event->key() == Qt::Key_C && event->modifiers() == Qt::ControlModifier) {
            raw.offer(source, ++serial);
            event->accept();
            return;
        }
        QPlainTextEdit::keyPressEvent(event);
    }
    void mousePressEvent(QMouseEvent *event) override { ++clicks; QPlainTextEdit::mousePressEvent(event); }
    void wheelEvent(QWheelEvent *event) override { ++scrolls; QPlainTextEdit::wheelEvent(event); }
public slots:
    bool Active() const { return isActiveWindow() && QApplication::focusWidget() && (QApplication::focusWidget() == this || isAncestorOf(QApplication::focusWidget())); }
    int Clicks() const { return clicks; }
    int Scrolls() const { return scrolls; }
    void Background(const QString &color) { setStyleSheet(QStringLiteral("QPlainTextEdit { background: %1; }").arg(color)); }
    void ObserveWindows(const QString &state) {
        windowState = state;
        windows << QJsonObject{{QStringLiteral("time"), QDateTime::currentMSecsSinceEpoch()}, {QStringLiteral("state"), QJsonDocument::fromJson(state.toUtf8()).array()}};
    }
    void ObserveActivation(const QString &id, const QString &caption) {
        activations << QJsonObject{{QStringLiteral("time"), QDateTime::currentMSecsSinceEpoch()}, {QStringLiteral("id"), id}, {QStringLiteral("caption"), caption}};
    }
    QString Windows() const { return windowState; }
    QString WindowEvents() const { return QString::fromUtf8(QJsonDocument(windows).toJson(QJsonDocument::Compact)); }
    QString Activations() const { return QString::fromUtf8(QJsonDocument(activations).toJson(QJsonDocument::Compact)); }
    bool ClipboardImageUniform(const QString &color) const {
        const auto image = QImage::fromData(lastSnapshot.value(QStringLiteral("image/png")));
        if (image.isNull()) return false;
        const QColor expected(color);
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (image.pixelColor(x, y) != expected) return false;
        return true;
    }
    bool Screenshot(const QString &path) {
        const int fd = memfd_create("stack-test-screenshot", MFD_CLOEXEC);
        if (fd < 0) return false;
        auto message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/org/kde/KWin/ScreenShot2"), QStringLiteral("org.kde.KWin.ScreenShot2"), QStringLiteral("CaptureWorkspace"));
        message << QVariantMap{{QStringLiteral("include-cursor"), true}, {QStringLiteral("native-resolution"), true}, {QStringLiteral("hide-caller-windows"), false}} << QVariant::fromValue(QDBusUnixFileDescriptor(fd));
        const auto reply = QDBusConnection::sessionBus().call(message, QDBus::Block, 2000);
        bool ok = false;
        if (reply.type() != QDBusMessage::ErrorMessage && reply.arguments().size() == 1) {
            const auto meta = qdbus_cast<QVariantMap>(reply.arguments()[0]);
            const int width = meta.value(QStringLiteral("width")).toInt(), height = meta.value(QStringLiteral("height")).toInt();
            const int stride = meta.value(QStringLiteral("stride")).toInt();
            const qint64 bytes = qint64(stride) * height;
            const auto format = QImage::Format(meta.value(QStringLiteral("format")).toInt());
            struct stat info {};
            for (int i = 0; i < 2000 && fstat(fd, &info) == 0 && info.st_size < bytes; ++i) usleep(1000);
            if (width > 0 && height > 0 && stride > 0 && bytes > 0 && info.st_size >= bytes) {
                void *data = mmap(nullptr, bytes, PROT_READ, MAP_SHARED, fd, 0);
                if (data != MAP_FAILED) {
                    ok = QImage(static_cast<const uchar *>(data), width, height, stride, format).save(path);
                    munmap(data, bytes);
                }
            }
        }
        ::close(fd);
        return ok;
    }
    void Prepare(const QString &text) { custom = false; setPlainText(text); selectAll(); setFocus(); }
    bool FocusReady() const { return isActiveWindow() && textCursor().hasSelection() && QApplication::focusWidget() && (QApplication::focusWidget() == this || isAncestorOf(QApplication::focusWidget())); }
    void SuppressCopy(bool suppress) { suppressCopy = suppress; }
    void IgnorePaste(bool ignore) { ignorePaste = ignore; }
    void PasteDelay(int milliseconds) { pasteDelay = milliseconds; }
    void Custom(const QString &mode, const QString &value) {
        source.clear(); custom = true;
        if (mode == QLatin1String("image")) {
            QImage image(32, 24, QImage::Format_ARGB32); image.fill(QColor(value));
            QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG");
            source.insert(QStringLiteral("image/png"), bytes);
        } else if (mode == QLatin1String("rich")) {
            source.insert(QStringLiteral("text/plain"), value.toUtf8());
            source.insert(QStringLiteral("text/html"), (QStringLiteral("<b>") + value.toHtmlEscaped() + QStringLiteral("</b>")).toUtf8());
        } else if (mode == QLatin1String("files")) {
            source.insert(QStringLiteral("text/uri-list"), value.toUtf8());
            source.insert(QStringLiteral("application/x-kde-cutselection"), QByteArrayLiteral("0"));
        } else if (mode == QLatin1String("secret")) {
            source.insert(QStringLiteral("text/plain"), value.toUtf8());
            source.insert(secretHint, QByteArrayLiteral("secret"));
        } else {
            source.insert(QStringLiteral("text/plain"), value.toUtf8());
            source.insert(QStringLiteral("application/x-qro-test"), QByteArray::fromHex("0001feff"));
        }
    }
    void Offer() { raw.offer(source, ++serial); }
    bool SourceIsSecret() const { return custom && source.value(secretHint) == QByteArrayLiteral("secret"); }
    void ReadClipboard() { snapshot = {}; raw.snapshot(++serial, std::numeric_limits<qint64>::max()); }
    void SaveClipboard() { savedReady = false; saveNext = true; raw.snapshot(++serial, std::numeric_limits<qint64>::max()); }
    QString SavedState() const { return savedReady ? QStringLiteral("ready:") + QString::fromLatin1(QCryptographicHash::hash(encode(saved), QCryptographicHash::Sha256).toHex()) : saveNext ? QStringLiteral("pending") : QStringLiteral("error:") + snapshot.value(QStringLiteral("_error")).toString(); }
    QString Digest() const { return snapshot.contains(QStringLiteral("_error")) ? QString::fromLatin1(QCryptographicHash::hash(encode(lastSnapshot), QCryptographicHash::Sha256).toHex()) : QString(); }
    void RestoreSaved() { if (savedReady) raw.offer(saved, ++serial); }
    bool WriteSaved(const QString &path) {
        if (!savedReady) return false;
        QSaveFile file(path);
        const auto bytes = encode(saved);
        return file.open(QIODevice::WriteOnly) && file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)
            && file.write(bytes) == bytes.size() && file.commit();
    }
    QString Clipboard() const { return QString::fromUtf8(QJsonDocument(snapshot).toJson(QJsonDocument::Compact)); }
    QString Events() const { return QString::fromUtf8(QJsonDocument(events).toJson(QJsonDocument::Compact)); }
    void ResetEvents() { events = {}; }
    void Quit() { qApp->quit(); }
};
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    app.setQuitLockEnabled(false);
    app.setDesktopFileName(app.arguments().contains(QStringLiteral("--live")) ? QStringLiteral("qro-stack-live-probe") : app.arguments().contains(QStringLiteral("--terminal")) ? QStringLiteral("org.kde.konsole") : QStringLiteral("qro-stack-probe"));
    Probe probe;
    const int serviceArg = app.arguments().indexOf(QStringLiteral("--service"));
    const QString service = serviceArg >= 0 ? app.arguments().value(serviceArg + 1) : QStringLiteral("org.local.StackProbe");
    if (!QDBusConnection::sessionBus().registerService(service)) return 3;
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/"), &probe, QDBusConnection::ExportAllSlots);
    if (!app.arguments().contains(QStringLiteral("--hidden"))) { probe.show(); probe.activateWindow(); }
    return app.exec();
}
#include "stack-probe.moc"
