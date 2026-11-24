#include "kwin-bridge.h"
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>

static QString jsString(const QString &s) {
    return QString::fromUtf8(QJsonDocument(QJsonArray{s}).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
}
KWinBridge::KWinBridge(QObject *parent) : QObject(parent) {
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/bridge"),this,QDBusConnection::ExportAllSlots);
}
KWinBridge::~KWinBridge() { const auto names=loaded;for(const auto &name:names)unload(name); }
bool KWinBridge::run(const QString &name,const QString &source) {
    if(!files.isValid())return false;
    QFile f(files.filePath(name+QStringLiteral(".js")));if(!f.open(QIODevice::WriteOnly))return false;
    // KWin also expires the script itself, even if this process exits mid-open.
    const QString watchdog=QStringLiteral(R"JS(
var expiry = new QTimer(); expiry.singleShot = true; expiry.interval = 2200;
expiry.timeout.connect(function() { callDBus('org.kde.KWin','/Scripting','org.kde.kwin.Scripting','unloadScript',%1); });
expiry.start();
)JS").arg(jsString(name));
    if(f.write((source+watchdog).toUtf8())<0)return false;f.close();
    QDBusInterface scripting(QStringLiteral("org.kde.KWin"),QStringLiteral("/Scripting"),QStringLiteral("org.kde.kwin.Scripting"));scripting.setTimeout(1500);
    QDBusReply<int> id=scripting.call(QStringLiteral("loadScript"),f.fileName(),name);if(!id.isValid()||id.value()<0)return false;
    loaded.insert(name);
    QDBusInterface runner(QStringLiteral("org.kde.KWin"),QStringLiteral("/Scripting/Script")+QString::number(id.value()),QStringLiteral("org.kde.kwin.Script"));runner.setTimeout(1500);
    const auto reply=runner.call(QStringLiteral("run"));
    if(reply.type()==QDBusMessage::ErrorMessage){unload(name);return false;}
    QTimer::singleShot(2500,this,[this,name]{
        if(pending&&name==openScript){pending=false;emit failed();}
        unload(name);
    });
    return true;
}
void KWinBridge::unload(const QString &name) {
    if(!loaded.remove(name))return;
    QDBusInterface scripting(QStringLiteral("org.kde.KWin"),QStringLiteral("/Scripting"),QStringLiteral("org.kde.kwin.Scripting"));scripting.setTimeout(1500);scripting.call(QStringLiteral("unloadScript"),name);
    QFile::remove(files.filePath(name+QStringLiteral(".js")));
}
bool KWinBridge::prepare() {
    if(pending)return false;
    pending=true;previous.clear();openScript=QStringLiteral("clipboard-panel-open-%1").arg(++serial);
    // Capture focus BEFORE showing the tool. KWin, rather than the Wayland client,
    // positions the normal window and excludes it from taskbar and switcher.
    const QString script=QStringLiteral(R"JS(
var previous = workspace.activeWindow;
function ours(w) { return String(w.resourceClass) === 'clipboard-panel' || String(w.desktopFileName) === 'org.local.clipboard-panel'; }
function place(w) {
    if (!ours(w)) return;
    workspace.windowAdded.disconnect(place);
    w.skipTaskbar = true; w.skipSwitcher = true; w.skipPager = true;
    var pointer = workspace.cursorPos;
    var output = workspace.screenAt(pointer);
    var area = output ? workspace.clientArea(KWin.WorkArea, output, workspace.currentDesktop) : workspace.clientArea(KWin.WorkArea, w);
    var g = w.frameGeometry;
    g.width = Math.min(g.width, area.width); g.height = Math.min(g.height, area.height);
    var x = pointer.x + 8 + g.width <= area.x + area.width ? pointer.x + 8 : pointer.x - g.width - 8;
    var y = pointer.y + 8 + g.height <= area.y + area.height ? pointer.y + 8 : pointer.y - g.height - 8;
    g.x = Math.max(area.x, Math.min(x, area.x + area.width - g.width));
    g.y = Math.max(area.y, Math.min(y, area.y + area.height - g.height));
    w.frameGeometry = g;
    workspace.activeWindow = w;
    callDBus('org.local.ClipboardPanel','/bridge','org.local.ClipboardPanel.Bridge','Positioned');
}
workspace.windowAdded.connect(place);
var output = workspace.screenAt(workspace.cursorPos);
var area = output ? workspace.clientArea(KWin.WorkArea, output, workspace.currentDesktop) : workspace.clientArea(KWin.WorkArea, previous);
callDBus('org.local.ClipboardPanel','/bridge','org.local.ClipboardPanel.Bridge','OpenReady',previous ? String(previous.internalId) : '', Math.round(area.x), Math.round(area.y), Math.round(area.width), Math.round(area.height));
)JS");
    if(run(openScript,script))return true;
    pending=false;return false;
}
void KWinBridge::OpenReady(const QString &id,int x,int y,int width,int height) { if(pending){previous=id;emit ready(QRect(x,y,width,height));} }
void KWinBridge::Positioned() { pending=false;const QString name=openScript;QTimer::singleShot(0,this,[this,name]{unload(name);}); }
void KWinBridge::cancel() { pending=false;unload(openScript); }
void KWinBridge::restore() {
    cancel();if(previous.isEmpty())return;
    const QString name=QStringLiteral("clipboard-panel-restore-%1").arg(++serial);
    const QString script=QStringLiteral(R"JS(
var active = workspace.activeWindow;
var own = active && (String(active.resourceClass) === 'clipboard-panel' || String(active.desktopFileName) === 'org.local.clipboard-panel');
// A click into another app must never be undone by a late restore.
if (!active || own) {
    var windows = workspace.windowList();
    for (var i=0;i<windows.length;i++) if (String(windows[i].internalId) === %1) { workspace.activeWindow = windows[i]; break; }
}
callDBus('org.local.ClipboardPanel','/bridge','org.local.ClipboardPanel.Bridge','Done',%2);
)JS").arg(jsString(previous),jsString(name));
    previous.clear();run(name,script);
}
void KWinBridge::Done(const QString &name) { if(loaded.contains(name))QTimer::singleShot(0,this,[this,name]{unload(name);}); }
