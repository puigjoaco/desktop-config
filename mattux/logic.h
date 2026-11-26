#pragma once
#include <QKeySequence>
#include <QStringList>
#include <algorithm>

namespace Mattux {
inline QString pasteLabel(int count, bool running) {
    if (!running || count == 0) return QStringLiteral("Paste stack");
    return count == 1 ? QStringLiteral("Paste 1 item") : QStringLiteral("Paste %1 items").arg(count);
}
inline bool working(const QString &state) { return state.section(QLatin1Char(':'), 0, 0) != QLatin1String("0"); }
inline QString subtitle(int count, bool running, bool busy) {
    if (!running) return QStringLiteral("Not running");
    if (busy) return QStringLiteral("Working…");
    if (count == 0) return QStringLiteral("Empty");
    if (count == 1) return QStringLiteral("1 item");
    if (count == 100) return QStringLiteral("100 items (full)");
    return QStringLiteral("%1 items").arg(count);
}
inline QString keyText(const QList<QKeySequence> &keys) {
    QStringList text;
    for (const auto &key : keys) if (!key.isEmpty()) text << key.toString(QKeySequence::NativeText);
    return text.isEmpty() ? QStringLiteral("None") : text.join(QStringLiteral(", "));
}
// Main-menu shortcut column: the first key only, blank when none.
inline QString firstKeyText(const QList<QKeySequence> &keys) {
    for (const auto &key : keys) if (!key.isEmpty()) return key.toString(QKeySequence::NativeText);
    return {};
}
inline QList<int> historySizes(int current) {
    QList<int> sizes{10, 20, 40, 100, 200};
    if (current > 0 && !sizes.contains(current)) sizes << current;
    std::sort(sizes.begin(), sizes.end());
    return sizes;
}
struct Shortcut {
    const char *id;
    const char *label;
    const char *title;
    QList<QKeySequence> defaults;
};
inline const QList<Shortcut> &shortcuts() {
    static const QList<Shortcut> items{
        {"capture-region", "Region screenshot…", "Region Screenshot", {QKeySequence(Qt::META | Qt::SHIFT | Qt::Key_S), QKeySequence(Qt::Key_Print)}},
        {"stack-copy", "Copy to stack…", "Copy to Stack", {QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_C)}},
        {"stack-capture", "Screenshot to stack…", "Screenshot to Stack", {QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S)}},
        {"stack-paste", "Paste stack…", "Paste Stack", {QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_V)}},
        {"stack-clear", "Clear stack…", "Clear Stack", {QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_X)}}};
    return items;
}
}
