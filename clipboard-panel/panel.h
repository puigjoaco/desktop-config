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
class QToolButton;
class QButtonGroup;
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
    void requestSave();
    void requestAuthorization(bool saving);
    void finishAuthorization(int result);
    void showSettings(bool show);
    void updateSettings();
    void growSettings();
    History *history;
    QListWidget *list = nullptr;
    QWidget *header = nullptr;
    QLineEdit *search = nullptr;
    QAction *searchIcon = nullptr;
    QPushButton *clearButton = nullptr;
    QLabel *empty = nullptr;
    QLabel *title = nullptr;
    QToolButton *settingsButton = nullptr;
    QToolButton *backButton = nullptr;
    QWidget *settingsPage = nullptr;
    QWidget *listPage = nullptr;
    QButtonGroup *presets = nullptr;
    QPushButton *saveButton = nullptr;
    QLabel *countLabel = nullptr;
    QLabel *warningLabel = nullptr;
    QLabel *saveStatus = nullptr;
    QLabel *clearStatus = nullptr;
    QLabel *divider = nullptr;
    bool settingsVisible = false;
    bool savingLimit = false;
    int selectedLimit = 100;
    QSize listSize;
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
