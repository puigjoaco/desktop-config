#include "history.h"
#include "panel.h"
#include "kwin-bridge.h"
#include <QAction>
#include <QApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QLockFile>
#include <QStandardPaths>
#include <QTimer>
#include <KGlobalAccel>
#include <KSystemClipboard>
#include <sys/stat.h>

class Controller final : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.local.ClipboardPanel")
public:
    Clip::History history;
    Clip::Panel panel;
    KWinBridge bridge;
    QTimer poll;
    QAction action;
    bool opening = false;
    Controller() : history(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+QStringLiteral("/clipboard-panel")),panel(&history),action(QStringLiteral("Portapapeles"),this) {
        action.setObjectName(QStringLiteral("show-history"));action.setProperty("componentName",QStringLiteral("clipboard-panel"));action.setProperty("componentDisplayName",QStringLiteral("Portapapeles"));action.setAutoRepeat(false);
        // The installer alone hands over Meta+V. Starting this daemon never steals a shortcut.
        KGlobalAccel::self()->setShortcut(&action,{});
        connect(&action,&QAction::triggered,this,&Controller::Open);
        connect(&panel,&Clip::Panel::chosen,this,[this](const QString &id){
            auto mime=history.choose(id);if(!mime)return;
            KSystemClipboard::instance()->setMimeData(mime.release(),QClipboard::Clipboard);
            close(true);
        });
        connect(&panel,&Clip::Panel::dismissRequested,this,&Controller::close);
        connect(&bridge,&KWinBridge::ready,this,[this](const QRect &area){if(!opening)return;panel.prepareOpen(area);panel.show();panel.raise();panel.activateWindow();});
        connect(&bridge,&KWinBridge::failed,this,[this]{opening=false;panel.hide();qWarning("No se pudo posicionar el panel. No se cambia la configuracion de pantalla.");});
        connect(&poll,&QTimer::timeout,this,&Controller::sync);poll.start(1000);sync();
        // Initialize data-control without reading or publishing clipboard content.
        KSystemClipboard::instance();
    }
    void sync() {
        QString error;
        const QString root=QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+QStringLiteral("/klipper/");
        auto incoming=Clip::readKlipper(root+QStringLiteral("history3.sqlite"),root+QStringLiteral("data"),&error,history.knownIds());
        if(!error.isEmpty())return; // A missing Klipper DB is normal before the first copy.
        if(!incoming.isEmpty()&&history.merge(incoming)&&panel.isVisible())panel.refresh();
    }
public slots:
    void Open() {
        if(panel.isVisible()||opening){close(true);return;}
        sync();opening=true;
        if(!bridge.prepare()){opening=false;qWarning("No se pudo preparar la ventana normal de Portapapeles.");}
    }
    QString Status() const { return history.isOpen()?QStringLiteral("ready"):QStringLiteral("error"); }
    void Quit() { close(true);qApp->quit(); }
private:
    void close(bool restore) {
        opening=false;
        // Restore while the panel is still active. KWin refuses to steal focus if a user has clicked elsewhere.
        if(restore)bridge.restore();else bridge.cancel();
        panel.hide();
    }
};
int main(int argc,char **argv) {
    ::umask(0077);
    QApplication app(argc,argv);app.setApplicationName(QStringLiteral("clipboard-panel"));app.setOrganizationName(QStringLiteral("local"));app.setDesktopFileName(QStringLiteral("org.local.clipboard-panel"));app.setQuitOnLastWindowClosed(false);
    const auto args=app.arguments();
    if(args.contains(QStringLiteral("--open"))||args.contains(QStringLiteral("--quit"))) {
        QDBusInterface target(QStringLiteral("org.local.ClipboardPanel"),QStringLiteral("/panel"),QStringLiteral("org.local.ClipboardPanel"));target.setTimeout(1500);
        const auto reply=target.call(args.contains(QStringLiteral("--quit"))?QStringLiteral("Quit"):QStringLiteral("Open"));return reply.type()==QDBusMessage::ErrorMessage?1:0;
    }
    if(!args.contains(QStringLiteral("--daemon"))) { qInfo("Use --daemon, --open o --quit. Las pruebas visuales estan en panel-test.");return 0; }
    auto bus=QDBusConnection::sessionBus();if(!bus.registerService(QStringLiteral("org.local.ClipboardPanel")))return 2;
    Controller controller;if(!controller.history.isOpen()) {qCritical("No se pudo abrir el historial privado.");return 3;}
    if(!bus.registerObject(QStringLiteral("/panel"),&controller,QDBusConnection::ExportAllSlots))return 2;
    return app.exec();
}
#include "main.moc"
