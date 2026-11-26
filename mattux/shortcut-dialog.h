#pragma once
#include "logic.h"
#include <QDialog>

QList<QKeySequence> readShortcut(const QString &id);
class ShortcutDialog final : public QDialog {
public:
    explicit ShortcutDialog(const Mattux::Shortcut &shortcut, QWidget *parent = nullptr);
    ~ShortcutDialog() override;
    void done(int result) override;
protected:
    void hideEvent(QHideEvent *event) override;
private:
    void stopRecording();
};
