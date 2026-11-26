#pragma once
#include "stack-clipboard.h"
#include "stack-busy.h"
#include <QElapsedTimer>
#include <QProcess>
#include <QTemporaryFile>
#include <QTimer>
#include <QThreadPool>
#include <functional>

class StackController final : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.local.QuickRegionOverlay.Stack")
public:
    explicit StackController(QObject *parent = nullptr);
    ~StackController() override;
    bool busy() const { return operation != Operation::Idle; }
    void copy();
    void paste();
    void clear();
    void appendImage(const QImage &image);
    bool canAdd();
    void shutdown();
    static void osd(const QString &text);
    // Menu requests share the shortcut's capture guard without changing shortcut dispatch.
    std::function<bool()> captureBusy;
public slots:
    void Paste();
    void Clear();
    void ActiveWindow(const QString &id, const QString &windowClass);
    int Count() const { return items.size(); }
    QString State() const;
    QString Focus() const { return activeClass; }
    QString FocusId() const { return activeId; }
signals:
    void Changed(int count, const QString &state);
private:
    void notifyChanged();
    QString lastPublishedState;
    void appended(const ClipboardStack::Item &item, ClipboardStack::AddMode mode, const QString &error);
    enum class Operation { Idle, Modifiers, Copying, CopySnapshot, RestoreSnapshot, Offering, Pasting, Restoring, Stopping, Saving, PreparingImage };
    Operation operation = Operation::Idle;
    StackBusy busyWheel{this};
    QThreadPool storage;
    void setOperation(Operation next);
    StackClipboard clipboard;
    QList<ClipboardStack::Item> items, pasteItems;
    ClipboardStack::Item original, pendingImage;
    ClipboardStack::AddMode pendingImageMode = ClipboardStack::AddMode::Append;
    QElapsedTimer addTime;
    bool restorePending = false;
    bool pasted = false, pasteSucceeded = false;
    QString directory, loadError, activeId, activeClass, targetId;
    bool targetTerminal = false, targetLeft = false, copyChanged = false, keyRunning = false, readSeen = false;
    quint64 serial = 0;
    qsizetype pasteIndex = 0;
    QTimer deadline, grace, modifiers;
    QElapsedTimer modifierTime;
    QProcess keys;
    QProcess releaseKeys;
    std::function<void()> afterModifiers, afterKeys;
    QTemporaryFile script;
    QString scriptName;
    void load();
    void append(const ClipboardStack::Item &item);
    bool available();
    bool sameTarget() const;
    void waitModifiers(std::function<void()> next);
    void key(bool copy, bool image, std::function<void()> next);
    void releaseSyntheticKeys();
    void takeSnapshot(qint64 limit = ClipboardStack::maxBytes);
    void nextPaste();
    void restore(const QString &message);
    void finish(const QString &message);
    QString completion;
};
