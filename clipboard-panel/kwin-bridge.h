#pragma once
#include <QObject>
#include <QRect>
#include <QSet>
#include <QTemporaryDir>

// Normal xdg-toplevel windows only. No layer-shell, portal or input injection.
class KWinBridge final : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.local.ClipboardPanel.Bridge")
public:
    explicit KWinBridge(QObject *parent = nullptr);
    ~KWinBridge();
    bool prepare();
    void restore();
    void cancel();
signals:
    void ready(const QRect &availableArea);
    void failed();
public slots:
    void OpenReady(const QString &previous, int x, int y, int width, int height);
    void Positioned();
    void Done(const QString &name);
private:
    bool run(const QString &name, const QString &source);
    void unload(const QString &name);
    QTemporaryDir files;
    QSet<QString> loaded;
    QString previous;
    QString openScript;
    int serial = 0;
    bool pending = false;
};
