#include "stack-clipboard.h"
#include "ext-data-control-v1-client-protocol.h"
#include <QSocketNotifier>
#include <QTimer>
#include <QHash>
#include <QElapsedTimer>
#include <QSet>
#include <QDebug>
#include <wayland-client.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <poll.h>
#include <limits>

namespace {
class Transport;
struct Source {
    Transport *owner;
    ext_data_control_source_v1 *source;
    ClipboardStack::Item item;
    quint64 serial;
};
class Transport final : public QObject {
public:
    StackClipboard *api;
    wl_display *display = nullptr;
    wl_registry *registry = nullptr;
    wl_seat *seat = nullptr;
    ext_data_control_manager_v1 *manager = nullptr;
    ext_data_control_device_v1 *device = nullptr;
    QList<Source *> sources;
    Source *current = nullptr;
    QHash<ext_data_control_offer_v1 *, QStringList> offers;
    ext_data_control_offer_v1 *selection = nullptr;
    quint64 generation = 0, cachedGeneration = 0;
    std::optional<ClipboardStack::Item> cached;
    bool initialized = false;
    bool observing = false;
    explicit Transport(StackClipboard *api) : api(api) {}
    bool sync() {
        bool done = false;
        auto *callback = wl_display_sync(display);
        static const wl_callback_listener listener{[](void *data, wl_callback *, uint32_t) { *static_cast<bool *>(data) = true; }};
        wl_callback_add_listener(callback, &listener, &done);
        QElapsedTimer timer;
        timer.start();
        bool ok = true;
        while (!done && timer.elapsed() < 1000) {
            if (wl_display_dispatch_pending(display) < 0) { ok = false; break; }
            if (done) break;
            if (wl_display_prepare_read(display) < 0) continue;
            wl_display_flush(display);
            pollfd fd{wl_display_get_fd(display), POLLIN, 0};
            const int result = poll(&fd, 1, qMax(0, 1000 - int(timer.elapsed())));
            if (result > 0 && (fd.revents & POLLIN)) {
                if (wl_display_read_events(display) < 0) { ok = false; break; }
            } else {
                wl_display_cancel_read(display);
                if (result < 0 && errno == EINTR) continue;
                ok = false; break;
            }
        }
        wl_callback_destroy(callback);
        return ok && done;
    }
    ~Transport() override {
        qDeleteAll(findChildren<QSocketNotifier *>());
        for (auto *offer : offers.keys()) ext_data_control_offer_v1_destroy(offer);
        for (auto *source : std::as_const(sources)) { ext_data_control_source_v1_destroy(source->source); delete source; }
        if (device) ext_data_control_device_v1_destroy(device);
        if (manager) ext_data_control_manager_v1_destroy(manager);
        if (seat) wl_seat_destroy(seat);
        if (registry) wl_registry_destroy(registry);
        if (display) wl_display_disconnect(display);
    }
    void init() {
        initialized = true;
        display = wl_display_connect(nullptr);
        if (!display) return;
        registry = wl_display_get_registry(display);
        static const wl_registry_listener listener{
            [](void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t) {
                auto *self = static_cast<Transport *>(data);
                if (strcmp(interface, wl_seat_interface.name) == 0 && !self->seat)
                {
                    self->seat = static_cast<wl_seat *>(wl_registry_bind(registry, name, &wl_seat_interface, 1));
                    static const wl_seat_listener seatListener{[](void *, wl_seat *, uint32_t) {}, [](void *, wl_seat *, const char *) {}};
                    wl_seat_add_listener(self->seat, &seatListener, nullptr);
                }
                if (strcmp(interface, ext_data_control_manager_v1_interface.name) == 0)
                    self->manager = static_cast<ext_data_control_manager_v1 *>(wl_registry_bind(registry, name, &ext_data_control_manager_v1_interface, 1));
            }, [](void *, wl_registry *, uint32_t) {}};
        wl_registry_add_listener(registry, &listener, this);
        if (!sync() || !manager || !seat) return;
        device = ext_data_control_manager_v1_get_data_device(manager, seat);
        static const ext_data_control_device_v1_listener deviceListener{
            [](void *data, ext_data_control_device_v1 *, ext_data_control_offer_v1 *offer) {
                auto *self = static_cast<Transport *>(data);
                self->offers.insert(offer, {});
                static const ext_data_control_offer_v1_listener offerListener{[](void *data, ext_data_control_offer_v1 *offer, const char *mime) {
                    auto &formats = static_cast<Transport *>(data)->offers[offer];
                    const auto format = QString::fromUtf8(mime);
                    if (!formats.contains(format)) formats << format;
                }};
                ext_data_control_offer_v1_add_listener(offer, &offerListener, self);
            },
            [](void *data, ext_data_control_device_v1 *, ext_data_control_offer_v1 *offer) {
                auto *self = static_cast<Transport *>(data);
                if (self->selection && self->selection != offer) { self->offers.remove(self->selection); ext_data_control_offer_v1_destroy(self->selection); }
                self->selection = offer;
                ++self->generation;
                if (self->observing) emit self->api->changed();
            },
            [](void *data, ext_data_control_device_v1 *device) {
                auto *self = static_cast<Transport *>(data);
                ext_data_control_device_v1_destroy(device);
                self->device = nullptr;
                emit self->api->unavailable();
            },
            [](void *data, ext_data_control_device_v1 *, ext_data_control_offer_v1 *offer) {
                if (offer) { static_cast<Transport *>(data)->offers.remove(offer); ext_data_control_offer_v1_destroy(offer); }
            }
        };
        ext_data_control_device_v1_add_listener(device, &deviceListener, this);
        auto *readable = new QSocketNotifier(wl_display_get_fd(display), QSocketNotifier::Read, this);
        connect(readable, &QSocketNotifier::activated, this, [this, readable] {
            // sync() may have drained an activation already queued by Qt. Never block dispatching a stale event.
            while (wl_display_prepare_read(display) < 0) {
                if (wl_display_dispatch_pending(display) < 0) { readable->setEnabled(false); device = nullptr; emit api->unavailable(); return; }
            }
            pollfd fd{wl_display_get_fd(display), POLLIN, 0};
            const int result = poll(&fd, 1, 0);
            if (result > 0 && (fd.revents & POLLIN)) {
                if (wl_display_read_events(display) < 0 || wl_display_dispatch_pending(display) < 0) { readable->setEnabled(false); device = nullptr; emit api->unavailable(); return; }
            } else {
                wl_display_cancel_read(display);
                if (result < 0 || (fd.revents & (POLLERR | POLLHUP))) { readable->setEnabled(false); device = nullptr; emit api->unavailable(); return; }
            }
            wl_display_flush(display);
        });
        if (!sync()) device = nullptr;
        observing = device != nullptr;
    }
    std::optional<ClipboardStack::Item> ownedItem() {
        if (!device || !sync()) return std::nullopt; // Never trust ownership after a failed round-trip.
        if (current) return current->item;
        return std::nullopt;
    }
    void prepareOriginal(quint64 serial) {
        QElapsedTimer timer;
        timer.start();
        // Dispatch selection and cancellation events before trusting either cache.
        if (!device || !sync()) {
            emit api->originalReady(serial, {}, QStringLiteral("Clipboard unavailable"), QStringLiteral("snapshot"), timer.elapsed());
            return;
        }
        if (current) {
            emit api->originalReady(serial, current->item, {}, QStringLiteral("owned"), timer.elapsed());
        } else if (cached && cachedGeneration == generation) {
            emit api->originalReady(serial, *cached, {}, QStringLiteral("cached"), timer.elapsed());
        } else {
            snapshot(serial, std::numeric_limits<qint64>::max(), true, timer);
        }
    }
    void snapshot(quint64 serial, qint64 limit, bool original = false, QElapsedTimer timer = {}) {
        if (!initialized) init();
        if (!timer.isValid()) timer.start();
        const auto deliver = [this, serial, original, timer](const ClipboardStack::Item &item, const QString &error) {
            if (original) emit api->originalReady(serial, item, error, QStringLiteral("snapshot"), timer.elapsed());
            else emit api->snapshotReady(serial, item, error);
        };
        if (!device || (!original && !sync())) { deliver({}, QStringLiteral("Clipboard unavailable")); return; }
        const quint64 capturedGeneration = generation;
        if (!selection) { cached = ClipboardStack::Item{}; cachedGeneration = generation; deliver({}, {}); return; }
        struct Read final : QObject {
            ClipboardStack::Item item;
            QList<int> fds;
            int remaining = 0;
            qint64 bytes = 0;
            bool finished = false;
            QSet<QString> pending;
            QTimer *idle = nullptr;
            using QObject::QObject;
            ~Read() override { for (int fd : fds) close(fd); }
        };
        auto *read = new Read(this);
        auto finish = [this, read, deliver, original, capturedGeneration](QString error) {
            if (read->finished) return;
            read->finished = true;
            if (read->idle) read->idle->stop();
            for (auto *notifier : read->findChildren<QSocketNotifier *>()) notifier->setEnabled(false);
            // An in-flight read belongs to its original offer, even if another selection arrives.
            // It can still be added, but must never be cached as the new selection.
            if (error.isEmpty() && capturedGeneration == generation) {
                cached = read->item;
                cachedGeneration = capturedGeneration;
            } else if (original && error.isEmpty()) {
                error = QStringLiteral("Clipboard changed while reading; try again");
            }
            deliver(error.isEmpty() ? read->item : ClipboardStack::Item{}, error);
            read->deleteLater();
        };
        const auto formats = ClipboardStack::snapshotFormats(offers.value(selection));
        if (formats.size() > 4096) { finish(QStringLiteral("Too many clipboard formats")); return; }
        read->remaining = formats.size();
        if (formats.isEmpty()) { finish({}); return; }
        read->idle = new QTimer(read);
        read->idle->setSingleShot(true);
        connect(read->idle, &QTimer::timeout, read, [read, finish] {
            qWarning() << "clipboard-read-timeout" << read->pending;
            finish(QStringLiteral("Clipboard read timed out"));
        });
        read->idle->start(10000);
        for (const QString &format : formats) {
            if (format.isEmpty() || format.toUtf8().size() > 16384) { finish(QStringLiteral("Invalid clipboard format")); return; }
            int fds[2];
            if (pipe2(fds, O_CLOEXEC) < 0) { finish(QStringLiteral("Clipboard read failed")); return; }
            fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK);
            read->fds << fds[0];
            read->item.insert(format, {});
            read->pending.insert(format);
            read->bytes += format.toUtf8().size();
            if (read->bytes > limit) { close(fds[1]); finish(QStringLiteral("Item exceeds 64 MB")); return; }
            ext_data_control_offer_v1_receive(selection, format.toUtf8().constData(), fds[1]);
            close(fds[1]);
            auto *notifier = new QSocketNotifier(fds[0], QSocketNotifier::Read, read);
            connect(notifier, &QSocketNotifier::activated, read, [read, notifier, fd = fds[0], format, finish, limit] {
                if (read->finished) return;
                char buffer[65536];
                for (;;) {
                    const auto n = ::read(fd, buffer, sizeof buffer);
                    if (n > 0) {
                        read->idle->start(10000);
                        read->bytes += n;
                        if (read->bytes > limit) { finish(QStringLiteral("Item exceeds 64 MB")); return; }
                        read->item[format].append(buffer, n);
                    } else if (n == 0) {
                        notifier->setEnabled(false);
                        read->pending.remove(format);
                        if (--read->remaining == 0) finish({});
                        return;
                    } else if (errno == EINTR) continue;
                    else if (errno == EAGAIN || errno == EWOULDBLOCK) return;
                    else { finish(QStringLiteral("Clipboard read failed")); return; }
                }
            });
        }
        wl_display_flush(display);
        // Snapshot every advertised format, including cold image encoders. Ten seconds without progress, or a
        // minute overall, fails explicitly. Copy-change and paste-consumer deadlines remain short.
        QTimer::singleShot(60000, read, [read, finish] {
            if (!read->finished) qWarning() << "clipboard-read-timeout" << read->pending;
            finish(QStringLiteral("Clipboard read timed out"));
        });
    }
    void write(Source *source, const QString &format, int fd) {
        const QByteArray bytes = source->item.value(format);
        const quint64 serial = source->serial;
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
        struct Pending final : QObject {
            int fd;
            QByteArray bytes;
            qsizetype sent = 0;
            Pending(int fd, QByteArray bytes, QObject *parent) : QObject(parent), fd(fd), bytes(std::move(bytes)) {}
            ~Pending() override { close(fd); }
        };
        auto *pending = new Pending(fd, bytes, this);
        auto *writable = new QSocketNotifier(fd, QSocketNotifier::Write, pending);
        auto *timeout = new QTimer(pending);
        timeout->setSingleShot(true);
        connect(timeout, &QTimer::timeout, pending, &QObject::deleteLater);
        timeout->start(10000);
        connect(writable, &QSocketNotifier::activated, pending, [this, pending, writable, timeout, serial, format] {
            while (pending->sent < pending->bytes.size()) {
                const auto n = ::write(pending->fd, pending->bytes.constData() + pending->sent, pending->bytes.size() - pending->sent);
                if (n > 0) { pending->sent += n; timeout->start(10000); }
                else if (n < 0 && errno == EINTR) continue;
                else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
                else { writable->setEnabled(false); pending->deleteLater(); return; }
            }
            writable->setEnabled(false);
            // KDE metadata such as x-kde-cutselection is not the pasted payload.
            if (!format.startsWith(QLatin1String("x-kde-")) && !format.startsWith(QLatin1String("application/x-kde-")))
                emit api->consumed(serial, format);
            pending->deleteLater();
        });
    }
    void offer(const ClipboardStack::Item &item, quint64 serial) {
        if (!initialized) init();
        if (!device) { emit api->offered(serial, false); return; }
        if (item.isEmpty()) {
            current = nullptr;
            ext_data_control_device_v1_set_selection(device, nullptr);
            emit api->offered(serial, sync());
            return;
        }
        auto *source = new Source{this, ext_data_control_manager_v1_create_data_source(manager), item, serial};
        sources << source;
        current = source;
        static const ext_data_control_source_v1_listener listener{
            [](void *data, ext_data_control_source_v1 *, const char *mime, int32_t fd) {
                auto *source = static_cast<Source *>(data);
                source->owner->write(source, QString::fromUtf8(mime), fd);
            },
            [](void *data, ext_data_control_source_v1 *) {
                auto *source = static_cast<Source *>(data);
                if (source->owner->current == source) source->owner->current = nullptr;
                emit source->owner->api->cancelled(source->serial);
                source->owner->sources.removeOne(source);
                ext_data_control_source_v1_destroy(source->source);
                delete source;
            }
        };
        ext_data_control_source_v1_add_listener(source->source, &listener, source);
        for (auto it = item.begin(); it != item.end(); ++it) ext_data_control_source_v1_offer(source->source, it.key().toUtf8().constData());
        ext_data_control_device_v1_set_selection(device, source->source);
        // Confirm the offer reached KWin before sending the paste key on another connection.
        const bool ok = sync();
        emit api->offered(serial, ok && sources.contains(source));
    }
};
}
StackClipboard::StackClipboard(QObject *parent) : QObject(parent) {
    signal(SIGPIPE, SIG_IGN); // A consumer cancelling a pipe is ordinary clipboard operation.
    worker = new Transport(this);
    worker->moveToThread(&thread);
    connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
    thread.start();
    QMetaObject::invokeMethod(worker, [this] {
        auto *transport = static_cast<Transport *>(worker);
        transport->init();
        initialized.store(transport->device != nullptr);
    }, Qt::QueuedConnection);
}
StackClipboard::~StackClipboard() { thread.quit(); thread.wait(); }
void StackClipboard::offer(const ClipboardStack::Item &item, quint64 serial) {
    QMetaObject::invokeMethod(worker, [this, item, serial] { static_cast<Transport *>(worker)->offer(item, serial); }, Qt::QueuedConnection);
}
void StackClipboard::snapshot(quint64 serial, qint64 limit) {
    QMetaObject::invokeMethod(worker, [this, serial, limit] { static_cast<Transport *>(worker)->snapshot(serial, limit); }, Qt::QueuedConnection);
}
void StackClipboard::prepareOriginal(quint64 serial) {
    QMetaObject::invokeMethod(worker, [this, serial] { static_cast<Transport *>(worker)->prepareOriginal(serial); }, Qt::QueuedConnection);
}
std::optional<ClipboardStack::Item> StackClipboard::ownedItem() {
    std::optional<ClipboardStack::Item> result;
    QMetaObject::invokeMethod(worker, [this, &result] { result = static_cast<Transport *>(worker)->ownedItem(); }, Qt::BlockingQueuedConnection);
    return result;
}
