#pragma once
#include <QMap>
#include <QImage>
#include <QSet>
#include <QVariant>
#include <QMimeData>
#include <QSqlDatabase>
#include <QString>
#include <QVector>
#include <memory>

namespace Clip {
struct Item {
    QString id;
    double added = 0;
    qint64 sequence = 0;
    QString text;
    QMap<QString, QByteArray> payloads;
    bool pinned = false;
    bool hidden = false;
    QImage image() const;
    std::unique_ptr<QMimeData> mimeData() const;
};
QString cleanText(const QString &text);
bool isLink(const QString &text);
struct ImagePreview { QImage thumbnail; QSize original; };
// Reads a coherent snapshot; never creates or modifies Klipper's DB or payloads.
QVector<Item> readKlipper(const QString &dbPath, const QString &dataPath, QString *error, const QSet<QString> &known = {});
class History {
public:
    explicit History(const QString &directory);
    ~History();
    bool isOpen() const { return db.isOpen(); }
    QString error() const { return lastError; }
    bool merge(QVector<Item> incoming);
    // Metadata only; use choose() to restore the full MIME payloads.
    QVector<Item> items(const QString &filter = {}) const;
    bool hasItems() const;
    ImagePreview imagePreview(const QString &id) const;
    std::unique_ptr<QMimeData> choose(const QString &id);
    bool pin(const QString &id);
    bool remove(const QString &id);
    bool clear();
    int maxUnpinned() const;
    // Atomically persist the limit and remove oldest unpinned cards; -1 on failure.
    int setMaxUnpinned(int limit);
    struct Usage { int unpinned = 0; int pinned = 0; qint64 bytes = 0; };
    Usage usage() const;
    QString lastChosen() const;
    QSet<QString> knownIds() const;
private:
    QSqlDatabase db;
    QString connection;
    mutable QString lastError;
    bool enforceRetention(int limit);
    int storedMaxUnpinned() const; // Zero means invalid/unreadable: do not trim automatically.
    bool execute(const QString &sql, const QVariantList &args = {});
    bool finishTransaction(bool ok);
};
}
