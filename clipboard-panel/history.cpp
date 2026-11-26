#include "history.h"
#include <QDir>
#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <algorithm>
#include <sys/stat.h>

namespace Clip {
QString cleanText(const QString &text) {
    QString s = text;
    s.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    s.replace(QChar('\r'), QChar('\n'));
    s.replace(QRegularExpression(QStringLiteral("[^\\S\\n]+")), QStringLiteral(" "));
    auto lines = s.split('\n');
    for (auto &line : lines) line = line.trimmed();
    s = lines.join('\n').trimmed();
    s.replace(QRegularExpression(QStringLiteral("\n{3,}")), QStringLiteral("\n\n"));
    return s;
}
bool isLink(const QString &text) {
    return QRegularExpression(QStringLiteral("^(https?://|www\\.)\\S+$"), QRegularExpression::CaseInsensitiveOption).match(text).hasMatch();
}
QImage Item::image() const {
    for (auto it = payloads.cbegin(); it != payloads.cend(); ++it) {
        if (it.key().startsWith(QStringLiteral("image/"))) {
            auto image = QImage::fromData(it.value());
            if (!image.isNull()) return image;
        }
    }
    return {};
}
std::unique_ptr<QMimeData> Item::mimeData() const {
    auto mime = std::make_unique<QMimeData>();
    for (auto it = payloads.cbegin(); it != payloads.cend(); ++it) mime->setData(it.key(), it.value());
    // Klipper also supplies Qt's decoded image format, for native image consumers.
    const QImage decoded = image();
    if (!decoded.isNull()) mime->setImageData(decoded);
    return mime;
}
static bool safeId(const QString &id) {
    return QRegularExpression(QStringLiteral("^[a-zA-Z0-9_-]{1,80}$")).match(id).hasMatch();
}
QVector<Item> readKlipper(const QString &dbPath, const QString &dataPath, QString *error, const QSet<QString> &known) {
    if (error) error->clear();
    const QString connection = QUuid::createUuid().toString();
    QVector<Item> result;
    {
        auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(dbPath);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=150"));
        if (!db.open()) {
            if (error) *error = QStringLiteral("No se puede leer el historial de Klipper.");
        } else {
            // Read-only connection + transaction prevent mixed main/aux snapshots.
            db.transaction();
            QSqlQuery q(db);
            if (!q.exec(QStringLiteral("SELECT uuid, added_time, text, starred, mimetypes FROM main ORDER BY added_time ASC, uuid ASC"))) {
                if (error) *error = QStringLiteral("El formato del historial de Klipper no es compatible.");
            } else while (q.next()) {
                Item item;
                item.id = q.value(0).toString();
                item.added = q.value(1).toDouble();
                item.text = q.value(2).toString();
                item.pinned = q.value(3).toBool();
                if (!safeId(item.id) || known.contains(item.id)) continue;
                QSqlQuery aux(db);
                aux.prepare(QStringLiteral("SELECT mimetype, data_uuid FROM aux WHERE uuid=? ORDER BY mimetype"));
                aux.addBindValue(item.id);
                if (!aux.exec()) { if (error) *error = QStringLiteral("No se pueden leer los formatos de Klipper."); break; }
                bool complete = true;
                qint64 totalBytes = 0;
                while (aux.next()) {
                    const QString dataId = aux.value(1).toString();
                    if (!safeId(dataId)) { complete = false; break; }
                    QFile f(QDir(dataPath).filePath(item.id + '/' + dataId));
                    // A disappearing/unfinished payload is retried at the next poll, never partly cached.
                    if (!f.open(QIODevice::ReadOnly) || f.size() > 128 * 1024 * 1024) { complete = false; break; }
                    totalBytes += f.size();
                    if(totalBytes > 128 * 1024 * 1024) { complete = false; break; }
                    const auto bytes = f.readAll();
                    if (f.error() != QFileDevice::NoError) { complete = false; break; }
                    item.payloads.insert(aux.value(0).toString(), bytes);
                }
                for (const auto &mime : q.value(4).toString().split(',', Qt::SkipEmptyParts)) {
                    if (!item.payloads.contains(mime)) complete = false;
                }
                if (complete && !item.payloads.isEmpty()) result.append(item);
            }
            db.commit();
        }
    }
    QSqlDatabase::removeDatabase(connection);
    if (error && !error->isEmpty()) result.clear();
    return result;
}

History::History(const QString &directory) : connection(QUuid::createUuid().toString()) {
    // Parent permissions protect DB, journal and cached MIME bytes, including secrets.
    if (!QDir().mkpath(directory) || ::chmod(QFile::encodeName(directory).constData(), 0700) != 0) {
        lastError = QStringLiteral("No se puede proteger el historial local."); return;
    }
    db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    db.setDatabaseName(QDir(directory).filePath(QStringLiteral("history.sqlite")));
    db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=150"));
    if (!db.open()) { lastError = QStringLiteral("No se puede abrir el historial local."); return; }
    ::chmod(QFile::encodeName(db.databaseName()).constData(), 0600);
    if (!execute(QStringLiteral("PRAGMA foreign_keys=ON")) ||
        !execute(QStringLiteral("CREATE TABLE IF NOT EXISTS items(id TEXT PRIMARY KEY, sequence INTEGER UNIQUE NOT NULL, added REAL, text TEXT, pinned INTEGER NOT NULL, hidden INTEGER NOT NULL DEFAULT 0)")) ||
        !execute(QStringLiteral("CREATE TABLE IF NOT EXISTS payloads(id TEXT REFERENCES items(id) ON DELETE CASCADE, mime TEXT, bytes BLOB, PRIMARY KEY(id,mime))")) ||
        !execute(QStringLiteral("CREATE TABLE IF NOT EXISTS settings(key TEXT PRIMARY KEY, value TEXT)"))) db.close();
}
History::~History() {
    db.close(); db = {}; QSqlDatabase::removeDatabase(connection);
}
bool History::execute(const QString &sql, const QVariantList &args) {
    QSqlQuery q(db); q.prepare(sql);
    for (const auto &arg : args) q.addBindValue(arg);
    if (q.exec()) return true;
    lastError = QStringLiteral("No se puede guardar el historial local."); return false;
}
bool History::finishTransaction(bool ok) {
    if (ok && db.commit()) return true;
    lastError = QStringLiteral("No se puede guardar el historial local.");
    // SQLite leaves a failed COMMIT transaction open (for example, SQLITE_BUSY).
    db.rollback();
    return false;
}
bool History::merge(QVector<Item> incoming) {
    if (!db.isOpen() || !db.transaction()) return false;
    std::sort(incoming.begin(), incoming.end(), [](const auto &a, const auto &b) {
        return a.added != b.added ? a.added < b.added : a.id < b.id;
    });
    QSqlQuery max(db); max.exec(QStringLiteral("SELECT COALESCE(MAX(sequence),0) FROM items")); max.next();
    qint64 sequence = max.value(0).toLongLong();
    bool ok = true;
    for (const auto &item : incoming) {
        QSqlQuery exists(db); exists.prepare(QStringLiteral("SELECT 1 FROM items WHERE id=?")); exists.addBindValue(item.id);
        if (!exists.exec()) { ok = false; break; }
        if (exists.next() || item.payloads.isEmpty()) continue;
        ok = execute(QStringLiteral("INSERT INTO items(id,sequence,added,text,pinned) VALUES(?,?,?,?,?)"),
                     {item.id, ++sequence, item.added, item.text.isNull() ? QStringLiteral("") : item.text, item.pinned});
        if (!ok) break;
        for (auto it = item.payloads.cbegin(); it != item.payloads.cend(); ++it) {
            if (!(ok = execute(QStringLiteral("INSERT INTO payloads VALUES(?,?,?)"), {item.id,it.key(),it.value()}))) break;
        }
        if (!ok) break;
    }
    if (ok) ok = enforceRetention(storedMaxUnpinned());
    return finishTransaction(ok);
}
bool History::enforceRetention(int limit) {
    if(limit<=0)return true;
    // Pinned items never count toward the limit. Keep tombstones only.
    return execute(QStringLiteral("UPDATE items SET hidden=1 WHERE hidden=0 AND pinned=0 AND id NOT IN (SELECT id FROM items WHERE hidden=0 AND pinned=0 ORDER BY added DESC, id DESC LIMIT ?)"), {limit}) &&
        execute(QStringLiteral("UPDATE items SET text='' WHERE hidden=1")) &&
        execute(QStringLiteral("DELETE FROM payloads WHERE id IN (SELECT id FROM items WHERE hidden=1)"));
}

int History::maxUnpinned() const {
    const int value=storedMaxUnpinned();return value?value:100;
}
int History::storedMaxUnpinned() const {
    QSqlQuery q(db);
    if(!q.exec(QStringLiteral("SELECT value FROM settings WHERE key='maxUnpinned'")))return 0;
    if(!q.next())return q.lastError().isValid()?0:100;
    bool ok=false;const int value=q.value(0).toString().toInt(&ok);
    return ok&&QList<int>{50,100,200,500}.contains(value)?value:0;
}
History::Usage History::usage() const {
    Usage result;QSqlQuery q(db);
    if(q.exec(QStringLiteral("SELECT COALESCE(SUM(pinned=0),0),COALESCE(SUM(pinned<>0),0) FROM items WHERE hidden=0"))&&q.next()) {
        result.unpinned=q.value(0).toInt();result.pinned=q.value(1).toInt();
    }
    if(q.exec(QStringLiteral("SELECT COALESCE(SUM(LENGTH(bytes)),0) FROM payloads JOIN items USING(id) WHERE hidden=0"))&&q.next())result.bytes=q.value(0).toLongLong();
    return result;
}
int History::setMaxUnpinned(int limit) {
    if(!QList<int>{50,100,200,500}.contains(limit)||!db.transaction())return -1;
    const int before=usage().unpinned;
    const bool ok=execute(QStringLiteral("INSERT OR REPLACE INTO settings VALUES('maxUnpinned',?)"),{limit})&&enforceRetention(limit);
    const int removed=ok?before-usage().unpinned:0;
    return finishTransaction(ok)?removed:-1;
}

QVector<Item> History::items(const QString &filter) const {
    QVector<Item> result;
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT id,sequence,added,text,pinned,EXISTS(SELECT 1 FROM payloads p WHERE p.id=items.id AND p.mime LIKE 'image/%') FROM items WHERE hidden=0 ORDER BY added DESC, id DESC"))) return result;
    while (q.next()) {
        Item item; item.id=q.value(0).toString(); item.sequence=q.value(1).toLongLong(); item.added=q.value(2).toDouble(); item.text=q.value(3).toString(); item.pinned=q.value(4).toBool();
        if (filter.isEmpty() || item.text.contains(filter, Qt::CaseInsensitive)) { result.append(item); continue; }
        if (q.value(5).toBool() && QStringLiteral("imagen captura").contains(filter, Qt::CaseInsensitive)) result.append(item);
    }
    return result;
}
bool History::hasItems() const {
    QSqlQuery q(db);
    return q.exec(QStringLiteral("SELECT 1 FROM items WHERE hidden=0 LIMIT 1")) && q.next();
}
ImagePreview History::imagePreview(const QString &id) const {
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT bytes FROM payloads JOIN items USING(id) WHERE id=? AND hidden=0 AND mime LIKE 'image/%' ORDER BY mime"));
    q.addBindValue(id);
    if (!q.exec()) return {};
    // One image at a time, decoded directly to a bounded thumbnail. The panel caches
    // only this preview, never the source bytes or full-resolution decoded image.
    while (q.next()) {
        QByteArray bytes=q.value(0).toByteArray();
        QBuffer buffer(&bytes); buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);
        const QSize original=reader.size();
        if (!original.isValid()) continue;
        reader.setScaledSize(original.scaled(QSize(1024,200),Qt::KeepAspectRatio));
        const QImage thumbnail=reader.read();
        if (!thumbnail.isNull()) return {thumbnail,original};
    }
    return {};
}
std::unique_ptr<QMimeData> History::choose(const QString &id) {
    Item item;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT mime,bytes FROM payloads JOIN items USING(id) WHERE id=? AND hidden=0"));
    q.addBindValue(id);
    if (!q.exec()) return {};
    while (q.next()) item.payloads.insert(q.value(0).toString(),q.value(1).toByteArray());
    q.finish();
    if (item.payloads.isEmpty()) return {};
    // Never touch sequence, added, hidden, pin or the order when choosing.
    if (!execute(QStringLiteral("INSERT OR REPLACE INTO settings VALUES('lastChosen',?)"), {id})) return {};
    return item.mimeData();
}
bool History::pin(const QString &id) {
    if (!db.transaction()) return false;
    return finishTransaction(execute(QStringLiteral("UPDATE items SET pinned=NOT pinned WHERE id=? AND hidden=0"), {id}) && enforceRetention(storedMaxUnpinned()));
}
bool History::remove(const QString &id) {
    if (!db.transaction()) return false;
    return finishTransaction(execute(QStringLiteral("UPDATE items SET hidden=1, text='' WHERE id=?"), {id}) && execute(QStringLiteral("DELETE FROM payloads WHERE id=?"), {id}));
}
bool History::clear() {
    if (!db.transaction()) return false;
    return finishTransaction(execute(QStringLiteral("UPDATE items SET hidden=1, text='' WHERE pinned=0")) && execute(QStringLiteral("DELETE FROM payloads WHERE id IN (SELECT id FROM items WHERE hidden=1)")));
}
QSet<QString> History::knownIds() const {
    QSet<QString> ids; QSqlQuery q(db); q.exec(QStringLiteral("SELECT id FROM items"));while(q.next())ids.insert(q.value(0).toString());return ids;
}
QString History::lastChosen() const {
    QSqlQuery q(db); q.exec(QStringLiteral("SELECT value FROM settings WHERE key='lastChosen'")); return q.next() ? q.value(0).toString() : QString();
}
}
