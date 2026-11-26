// Stack persistence, independent of the desktop/session. Directory exchange keeps replacement crash-atomic.
#pragma once
#include "clipboard-stack.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QUuid>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace ClipboardStack {
inline bool newStackAfterPaste() {
    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                       + QStringLiteral("/quick-region-overlayrc"), QSettings::IniFormat);
    return settings.value(QStringLiteral("Stack/NewStackAfterPaste"), true).toBool();
}
inline bool privateStackDirectory(const QString &directory) {
    return QDir().mkpath(directory) && QFile::setPermissions(directory, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}
inline bool saveStackBytes(const QString &path, const QByteArray &bytes) {
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)
        && file.write(bytes) == bytes.size() && file.commit();
}
inline bool saveStackItem(const QString &directory, qsizetype index, const Item &item) {
    return privateStackDirectory(directory)
        && saveStackBytes(directory + QStringLiteral("/%1.qrs").arg(index, 3, 10, QLatin1Char('0')), encode(item));
}
// A count makes a stale marker harmless if an OFF-mode append commits just before a crash. Appends never
// remove items, so this cannot match another generation; clear/replacement atomically retire the whole directory.
inline bool savePasteMarker(const QString &directory, qsizetype count) {
    return privateStackDirectory(directory) && saveStackBytes(directory + QStringLiteral("/pasted"), QByteArray::number(count) + '\n');
}
inline bool hasPasteMarker(const QString &directory, qsizetype count) {
    QFile file(directory + QStringLiteral("/pasted"));
    return count > 0 && file.open(QIODevice::ReadOnly) && file.size() <= 4
        && file.readAll() == QByteArray::number(count) + '\n';
}
inline QString stageStackReplacement(const QString &directory, const Item &item) {
    const QString staged = directory + QStringLiteral("-replaced-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!saveStackItem(staged, 0, item)) { QDir(staged).removeRecursively(); return {}; }
    return staged;
}
inline bool exchangeStackDirectories(const QString &directory, const QString &staged) {
    const auto currentPath = QFile::encodeName(directory), stagedPath = QFile::encodeName(staged);
    // No clear/rename fallback: if the filesystem cannot exchange directories, refuse without touching the old stack.
    return syscall(SYS_renameat2, AT_FDCWD, currentPath.constData(), AT_FDCWD, stagedPath.constData(), RENAME_EXCHANGE) == 0;
}
inline void cleanupRetiredStacks(const QString &directory) {
    const QDir parent(QFileInfo(directory).absolutePath());
    for (const auto &prefix : {QStringLiteral("stack-cleared-"), QStringLiteral("stack-replaced-")}) {
        for (const auto &name : parent.entryList({prefix + QLatin1Char('*')}, QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (!QUuid(name.mid(prefix.size())).isNull() && !QFileInfo(parent.filePath(name)).isSymLink())
                QDir(parent.filePath(name)).removeRecursively();
        }
    }
}
}
