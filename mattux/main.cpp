#include "tray.h"
#include <QApplication>
#include <QDBusConnection>
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("mattux"));
    app.setApplicationDisplayName(QStringLiteral("Mattux"));
    app.setOrganizationDomain(QStringLiteral("local"));
    app.setDesktopFileName(QStringLiteral("mattux"));
    app.setQuitOnLastWindowClosed(false);
    if (!QDBusConnection::sessionBus().registerService(QStringLiteral("org.local.Mattux"))) return 0;
    Tray tray;
    return app.exec();
}
