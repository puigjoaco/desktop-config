#pragma once
#include "history.h"
#include <QWidget>
class QListWidget;
class QLineEdit;
class QPushButton;
class QLabel;
class QKeyEvent;
class QProcess;
class QAction;
namespace Clip {
class Panel final : public QWidget {
    Q_OBJECT
public:
    explicit Panel(History *history, const QString &settingsFile = {}, const QString &authorizationProgram = QStringLiteral("pkcheck"));
    void refresh(bool selectNewest = false);
    void prepareOpen(const QRect &availableArea = {});
    QString currentId() const;
    QString filter() const;
signals:
    void chosen(const QString &id);
    void dismissRequested(bool restoreFocus);
protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;
    bool event(QEvent *) override;
private:
    bool handleKey(QKeyEvent *event);
    Qt::Edges resizeEdges(const QPoint &position) const;
    void applyStyle();
    void updateCardSizes();
    void queueCardSizes();
    void resizeInternally(const QSize &size);
    void requestClear();
    void finishAuthorization(bool authorized);
    History *history;
    QListWidget *list = nullptr;
    QWidget *header = nullptr;
    QLineEdit *search = nullptr;
    QAction *searchIcon = nullptr;
    QPushButton *clearButton = nullptr;
    QLabel *empty = nullptr;
    QString settingsFile;
    QString authorizationProgram;
    QProcess *authorization = nullptr;
    bool authorizationPending = false;
    QSize preferredSize;
    QSize pendingAutomaticSize;
    bool customSize = false;
    bool adjustingSize = false;
    bool cardSizesPending = false;
    bool resizing = false;
};
}
