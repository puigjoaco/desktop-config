#include "stack-holder.h"
#include "stack-clipboard.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace ClipboardStack;
namespace {
QString holderDirectory() { return QFileInfo(stateDirectory()).absolutePath(); }
}

void cleanupClipboardHolders() {
    const QDir dir(holderDirectory());
    const auto yesterday = QDateTime::currentDateTimeUtc().addDays(-1);
    for (const auto &file : dir.entryInfoList({QStringLiteral("clipboard-holder-*.qrs")}, QDir::Files)) {
        if (!file.isSymLink() && file.ownerId() == getuid() && file.lastModified() < yesterday)
            QFile::remove(file.absoluteFilePath());
    }
}

void handoffClipboard(const Item &item) {
    const QString directory = holderDirectory();
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString path = directory + QStringLiteral("/clipboard-holder-") + id + QStringLiteral(".qrs");
    if (!QDir().mkpath(directory) || !QFile::setPermissions(directory, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) {
        qCritical() << "clipboard-handoff-directory-failed" << directory;
        return;
    }
    QSaveFile file(path);
    const auto bytes = encode(item);
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)
        || file.write(bytes) != bytes.size() || !file.commit()) {
        qCritical() << "clipboard-handoff-save-failed" << path << file.errorString();
        return;
    }

    // A detached child still belongs to our service cgroup and dies on restart. Let the user manager spawn a
    // separate service instead. Its bus/runtime may differ from this Wayland session (e.g. a nested compositor).
    const auto session = QProcessEnvironment::systemEnvironment();
    auto manager = session;
    const QString runtime = QStringLiteral("/run/user/%1").arg(getuid());
    manager.insert(QStringLiteral("XDG_RUNTIME_DIR"), runtime);
    manager.insert(QStringLiteral("DBUS_SESSION_BUS_ADDRESS"), QStringLiteral("unix:path=") + runtime + QStringLiteral("/bus"));
    const QString unit = QStringLiteral("qro-clipboard-holder-") + id;
    QStringList args{QStringLiteral("--user"), QStringLiteral("--collect"), QStringLiteral("--quiet"), QStringLiteral("--unit=") + unit};
    for (const auto &name : {"WAYLAND_DISPLAY", "XDG_RUNTIME_DIR", "DBUS_SESSION_BUS_ADDRESS", "XDG_STATE_HOME"}) {
        if (session.contains(QLatin1String(name))) args << QStringLiteral("--setenv=") + QLatin1String(name) + QLatin1Char('=') + session.value(QLatin1String(name));
    }
    QString executable = QCoreApplication::applicationFilePath();
    // install.sh atomically replaces the binary before restarting us. /proc/self/exe can name the deleted inode;
    // argv[0] still names the installed successor, which implements this holder mode too.
    if (!QFileInfo(executable).isExecutable()) executable = QFileInfo(QCoreApplication::arguments().first()).absoluteFilePath();
    args << executable << QStringLiteral("--hold-clipboard") << path;
    QProcess launcher;
    launcher.setProcessEnvironment(manager);
    QElapsedTimer timer;
    timer.start();
    launcher.start(QStringLiteral("systemd-run"), args);
    bool launched = launcher.waitForFinished(3000) && launcher.exitStatus() == QProcess::NormalExit && launcher.exitCode() == 0;
    while (launched && QFile::exists(path) && timer.elapsed() < 3000) QThread::msleep(20);
    if (launched && !QFile::exists(path)) {
        qInfo().noquote() << "clipboard-handoff-ready" << unit;
    } else {
        // Never delete the only durable copy when launch/ownership confirmation failed.
        qCritical().noquote() << "clipboard-handoff-failed snapshot retained:" << path << launcher.readAllStandardError().trimmed();
    }
    if (launcher.state() != QProcess::NotRunning) { launcher.kill(); launcher.waitForFinished(100); }
}

int holdClipboard(int argc, char **argv, const QString &path) {
    QCoreApplication app(argc, argv); // No GUI, daemon D-Bus name, KWin script or shortcuts in holder mode.
    const int fd = open(QFile::encodeName(path).constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    struct stat stat{};
    if (fd < 0) { qCritical() << "clipboard-holder-open-failed" << path; return 1; }
    const bool privateFile = fstat(fd, &stat) == 0 && S_ISREG(stat.st_mode) && stat.st_uid == getuid() && (stat.st_mode & 0777) == 0600;
    QFile file;
    if (!file.open(fd, QIODevice::ReadOnly, QFile::AutoCloseHandle)) { close(fd); return 1; }
    Item item;
    // Original clipboards have no stack-item size cap. The same QRS1 length validation still applies.
    if (!privateFile || !decode(file.readAll(), item, std::numeric_limits<qint64>::max(), true)) {
        qCritical() << "clipboard-holder-invalid-snapshot" << path;
        return 1;
    }
    file.close();
    StackClipboard clipboard;
    bool confirmed = false;
    QObject::connect(&clipboard, &StackClipboard::offered, &app, [&](quint64, bool ok) {
        if (!ok) { qCritical() << "clipboard-holder-offer-failed"; app.exit(1); return; }
        // offer() round-trips with KWin and checks that the source wasn't cancelled before acknowledging.
        if (!QFile::remove(path)) { qCritical() << "clipboard-holder-ack-failed" << path; app.exit(1); return; }
        confirmed = true;
        qInfo() << "clipboard-holder-ready pid=" << QCoreApplication::applicationPid();
        if (item.isEmpty()) app.quit();
    });
    QObject::connect(&clipboard, &StackClipboard::cancelled, &app, [&] { app.quit(); });
    QObject::connect(&clipboard, &StackClipboard::unavailable, &app, [&] { app.quit(); });
    QTimer::singleShot(2500, &app, [&] { if (!confirmed) { qCritical() << "clipboard-holder-timeout"; app.exit(1); } });
    clipboard.offer(item, 1);
    return app.exec();
}
