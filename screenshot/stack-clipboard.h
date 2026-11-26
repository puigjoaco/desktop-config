// Raw Wayland offers preserve original MIME bytes (KSystemClipboard re-encodes images).
#pragma once
#include "clipboard-stack.h"
#include <QObject>
#include <QThread>
#include <atomic>
#include <optional>

class StackClipboard final : public QObject {
    Q_OBJECT
public:
    explicit StackClipboard(QObject *parent = nullptr);
    ~StackClipboard() override;
    void offer(const ClipboardStack::Item &item, quint64 serial);
    void snapshot(quint64 serial, qint64 limit = ClipboardStack::maxBytes);
    // Validate selection state on the transport thread without requesting any payload on a hit.
    void prepareOriginal(quint64 serial);
    // Drain pending cancellation events before reporting a live source.
    std::optional<ClipboardStack::Item> ownedItem();
    bool ready() const { return initialized.load(); }
signals:
    void changed();
    void cancelled(quint64 serial);
    void unavailable();
    void snapshotReady(quint64 serial, const ClipboardStack::Item &item, const QString &error);
    void originalReady(quint64 serial, const ClipboardStack::Item &item, const QString &error, const QString &path, qint64 ms);
    void offered(quint64 serial, bool ok);
    // A non-hint MIME payload has been written completely to a consumer's pipe.
    void consumed(quint64 serial, const QString &format);
private:
    QThread thread;
    QObject *worker = nullptr;
    std::atomic<bool> initialized{false};
};
