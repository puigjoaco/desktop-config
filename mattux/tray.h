#pragma once
#include <KStatusNotifierItem>
#include <QAction>
#include <QMenu>
#include <QPointer>
#include <QDialog>

class Tray final : public QObject {
    Q_OBJECT
public:
    explicit Tray(QObject *parent = nullptr);
protected:
    bool eventFilter(QObject *object, QEvent *event) override;
private slots:
    void stackChanged(int count, const QString &state);
    void ownerChanged(const QString &name, const QString &oldOwner, const QString &newOwner);
private:
    KStatusNotifierItem item{QStringLiteral("mattux"), this};
    QMenu *menu = new QMenu;
    QMenu *history = nullptr;
    QAction *paste = nullptr, *clear = nullptr, *newStack = nullptr, *enabledAction = nullptr;
    QList<QAction *> shortcutActions;
    QPointer<QDialog> openDialog; // one shortcut dialog at a time
    bool enabled = false, running = false, busy = false, switching = false;
    int count = 0;
    quint64 generation = 0;
    void addScreenshotSection();
    void refresh();
    void refreshStack();
    void updateDisplay();
    void rebuildHistory();
    void invokeStack(const QString &method);
    void setEnabled(bool on);
};
