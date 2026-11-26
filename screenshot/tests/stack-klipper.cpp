// Load the installed Klipper plugin in a private session without starting a second full Plasma desktop.
#include <QApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QDebug>
#include <memory>
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("klipper"));
    app.setQuitOnLastWindowClosed(false);
    app.setQuitLockEnabled(false);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [] { qInfo() << "klipper-test-about-to-quit"; });
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData("import org.kde.plasma.private.clipboard\nKlipperInterface {}", QUrl());
    std::unique_ptr<QObject> klipper(component.create());
    if (!klipper) { qWarning() << component.errors(); return 1; }
    return app.exec();
}
