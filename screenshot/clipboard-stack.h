// Clipboard stack decisions and bounded on-disk format; no desktop/session dependencies.
#pragma once
#include <algorithm>
#include <QByteArray>
#include <QDataStream>
#include <QMap>
#include <QString>
#include <QList>
#include <QIODevice>
#include <QStringList>
#include <QTextDocument>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QStandardPaths>
#include <limits>

namespace ClipboardStack {
using Item = QMap<QString, QByteArray>;
inline constexpr qsizetype maxItems = 100;
inline constexpr qint64 maxBytes = 64LL * 1024 * 1024;
enum class AddMode { Append, Replace, Full };
inline AddMode addMode(qsizetype count, bool pasted, bool newStackAfterPaste) {
    if (pasted && newStackAfterPaste) return AddMode::Replace;
    return count >= maxItems ? AddMode::Full : AddMode::Append;
}
inline const QString secretHint = QStringLiteral("x-kde-passwordManagerHint");
inline QString stateDirectory() { return QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + QStringLiteral("/quick-region-overlay/stack"); }
enum class Kind { Text, Image, Other };
inline Kind kind(const Item &item) {
    bool text = false, files = false;
    for (auto it = item.begin(); it != item.end(); ++it) {
        const auto format = it.key().toLower();
        if (format.startsWith(QLatin1String("image/")) || format == QLatin1String("application/x-qt-image")) return Kind::Image;
        if (format.section(QLatin1Char(';'), 0, 0) == QLatin1String("text/uri-list")) files = true;
        if (format.section(QLatin1Char(';'), 0, 0) == QLatin1String("text/plain") || format == QLatin1String("utf8_string")) text = true;
    }
    return text && !files ? Kind::Text : Kind::Other;
}
// Qt image offers list ~38 encodings (HEIF, AVIF, EXR, ...) produced on request, which takes seconds to read.
// PNG is lossless and universally accepted, so when it is offered the other image encodings are not read.
inline QStringList snapshotFormats(const QStringList &formats) {
    if (!formats.contains(QStringLiteral("image/png"))) return formats;
    QStringList kept;
    for (const QString &format : formats) {
        const bool otherImage = format != QLatin1String("image/png")
            && (format.startsWith(QLatin1String("image/")) || format == QLatin1String("application/x-qt-image"));
        if (!otherImage) kept << format;
    }
    return kept;
}
// A selection can offer formats with zero bytes; that is nothing to stack.
inline bool hasPayload(const Item &item) {
    return std::any_of(item.cbegin(), item.cend(), [](const QByteArray &bytes) { return !bytes.isEmpty(); });
}
inline qint64 size(const Item &item) {
    qint64 result = 0;
    for (auto it = item.begin(); it != item.end(); ++it) result += it.key().toUtf8().size() + it.value().size();
    return result;
}
inline QString decodeText(const QString &format, const QByteArray &bytes) {
    static const QRegularExpression charset(QStringLiteral("(?:^|;)\\s*charset\\s*=\\s*\"?([^;\"]+)"), QRegularExpression::CaseInsensitiveOption);
    const auto match = charset.match(format);
    if (match.hasMatch()) {
        QStringDecoder decoder(match.captured(1).trimmed().toLatin1());
        if (decoder.isValid()) return decoder.decode(bytes);
    }
    return QString::fromUtf8(bytes);
}
inline QString htmlFormat(const Item &item) {
    for (auto it = item.begin(); it != item.end(); ++it)
        if (it.key().section(QLatin1Char(';'), 0, 0).trimmed().compare(QLatin1String("text/html"), Qt::CaseInsensitive) == 0) return it.key();
    return {};
}
inline QString plain(const Item &item) {
    if (item.contains(QStringLiteral("text/plain;charset=utf-8"))) return QString::fromUtf8(item.value(QStringLiteral("text/plain;charset=utf-8")));
    if (item.contains(QStringLiteral("text/plain"))) return QString::fromUtf8(item.value(QStringLiteral("text/plain")));
    if (item.contains(QStringLiteral("UTF8_STRING"))) return QString::fromUtf8(item.value(QStringLiteral("UTF8_STRING")));
    for (auto it = item.begin(); it != item.end(); ++it) {
        if (it.key().section(QLatin1Char(';'), 0, 0).trimmed().compare(QLatin1String("text/plain"), Qt::CaseInsensitive) == 0 || it.key().compare(QLatin1String("UTF8_STRING"), Qt::CaseInsensitive) == 0)
            return decodeText(it.key(), it.value());
    }
    const QString html = htmlFormat(item);
    if (!html.isEmpty()) {
        QTextDocument document;
        document.setHtml(decodeText(html, item.value(html)));
        return document.toPlainText();
    }
    return {};
}
inline QString htmlFragment(const QByteArray &bytes) {
    // Content outside a StartFragment/EndFragment pair was not selected, and Qt receivers keep only the first pair.
    // Keep only the outside tags that enclose the selection (e.g. its paragraph), so wrappers stay balanced.
    static const QRegularExpression fragmentMarker(QStringLiteral("<!--(?:StartFragment|EndFragment)-->"));
    static const QRegularExpression tag(QStringLiteral("<(/?)([A-Za-z][A-Za-z0-9]*)\\b[^>]*?(/?)>"));
    static const QStringList voidTags{QStringLiteral("area"), QStringLiteral("br"), QStringLiteral("col"), QStringLiteral("embed"),
        QStringLiteral("hr"), QStringLiteral("img"), QStringLiteral("input"), QStringLiteral("meta"), QStringLiteral("source"), QStringLiteral("wbr")};
    // Before the selection, keep start tags left open; after it, keep end tags with no start tag (scanning backwards).
    const auto enclosing = [](const QString &outside, bool before) {
        QList<QRegularExpressionMatch> tags;
        for (auto it = tag.globalMatch(outside); it.hasNext();) tags << it.next();
        if (!before) std::reverse(tags.begin(), tags.end());
        QStringList kept, names;
        for (const auto &m : tags) {
            const QString name = m.captured(2).toLower();
            if (!m.captured(3).isEmpty() || voidTags.contains(name)) continue;
            if (m.captured(1).isEmpty() == before) { kept << m.captured(0); names << name; }
            else if (const auto at = names.lastIndexOf(name); at >= 0) { kept.resize(at); names.resize(at); }
        }
        if (!before) std::reverse(kept.begin(), kept.end());
        return kept.join(QString());
    };
    const auto selection = [&enclosing](QString html) {
        const qsizetype markStart = html.indexOf(QLatin1String("<!--StartFragment-->"));
        const qsizetype markEnd = html.lastIndexOf(QLatin1String("<!--EndFragment-->"));
        if (markStart >= 0 && markEnd > markStart)
            html = enclosing(html.left(markStart), true) + html.mid(markStart, markEnd - markStart) + enclosing(html.mid(markEnd), false);
        return html.remove(fragmentMarker);
    };
    const QString html = QString::fromUtf8(bytes);
    static const QRegularExpression body(QStringLiteral("<body\\b[^>]*>([\\s\\S]*?)</body\\s*>"), QRegularExpression::CaseInsensitiveOption);
    const auto match = body.match(html);
    if (!match.hasMatch()) return selection(html);
    // QTextEdit and browsers often copy a full HTML document; nested html/body tags would truncate a joined paste.
    static const QRegularExpression style(QStringLiteral("<style\\b[^>]*>[\\s\\S]*?</style\\s*>"), QRegularExpression::CaseInsensitiveOption);
    QString styles;
    auto it = style.globalMatch(html.left(match.capturedStart(0)));
    while (it.hasNext()) styles += it.next().captured(0);
    const QString content = selection(match.captured(1));
    // Qt copies complete paragraph wrappers even for a one-line selection. Leaving </p><br><p> at a run boundary
    // inserts a blank line in rich-text targets. Inline those wrappers while retaining their text-format styles.
    static const QRegularExpression paragraph(QStringLiteral("<p\\b([^>]*)>([\\s\\S]*?)</p\\s*>"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression emptyParagraph(QStringLiteral("^\\s*<br\\s*/?>\\s*$"), QRegularExpression::CaseInsensitiveOption);
    auto paragraphs = paragraph.globalMatch(content);
    QStringList inlineParagraphs;
    qsizetype end = 0;
    bool onlyParagraphs = true;
    while (paragraphs.hasNext()) {
        const auto part = paragraphs.next();
        if (!content.mid(end, part.capturedStart() - end).trimmed().isEmpty()) { onlyParagraphs = false; break; }
        QString inner = part.captured(2);
        if (emptyParagraph.match(inner).hasMatch()) inner.clear();
        inlineParagraphs << QStringLiteral("<span%1>%2</span>").arg(part.captured(1), inner);
        end = part.capturedEnd();
    }
    if (onlyParagraphs && !inlineParagraphs.isEmpty() && content.mid(end).trimmed().isEmpty()) return styles + inlineParagraphs.join(QStringLiteral("<br>"));
    return styles + content;
}
// A single item is offered unchanged. A run of text items gets plain text and HTML when present; auxiliary
// formats (ODT, browser metadata, markdown...) cannot be meaningfully joined and are omitted from merged runs.
inline QList<Item> plan(const QList<Item> &items) {
    QList<Item> result;
    for (qsizetype i = 0; i < items.size();) {
        if (kind(items[i]) != Kind::Text) { result << items[i++]; continue; }
        qsizetype end = i + 1;
        while (end < items.size() && kind(items[end]) == Kind::Text) ++end;
        if (end == i + 1) { result << items[i++]; continue; }
        QStringList texts, html;
        bool hasHtml = false;
        for (; i < end; ++i) {
            texts << plain(items[i]);
            const QString format = htmlFormat(items[i]);
            if (!format.isEmpty()) {
                hasHtml = true;
                html << htmlFragment(decodeText(format, items[i].value(format)).toUtf8());
            } else html << plain(items[i]).toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
        }
        Item joined{{QStringLiteral("text/plain"), texts.join(QLatin1Char('\n')).toUtf8()}};
        if (hasHtml) joined.insert(QStringLiteral("text/html"), html.join(QStringLiteral("<br>")).toUtf8());
        result << joined;
    }
    return result;
}
inline QByteArray encode(const Item &item) {
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_0);
    out << quint32(0x51525331) << quint32(item.size()); // QRS1
    for (auto it = item.begin(); it != item.end(); ++it) {
        out << it.key().toUtf8() << quint32(it.value().size());
        out.writeRawData(it.value().constData(), it.value().size());
    }
    return bytes;
}
// Read lengths before allocating; QDataStream's QByteArray operator alone trusts attacker-controlled lengths.
inline bool decode(const QByteArray &bytes, Item &item, qint64 limit = maxBytes, bool allowEmpty = false) {
    item.clear();
    if (bytes.size() < 8 || bytes.size() - qMin<qint64>(bytes.size(), 128 * 1024) > limit) return false;
    QDataStream in(bytes);
    in.setVersion(QDataStream::Qt_6_0);
    quint32 magic, count;
    in >> magic >> count;
    if (magic != 0x51525331 || (!allowEmpty && count == 0) || count > 4096) return false;
    auto read = [&](QByteArray &value, quint32 limit) {
        quint32 length;
        in >> length;
        if (in.status() != QDataStream::Ok || length > limit || length > quint64(in.device()->bytesAvailable())) return false;
        value.resize(length);
        return in.readRawData(value.data(), length) == qint64(length);
    };
    Item loaded;
    for (quint32 n = 0; n < count; ++n) {
        QByteArray name, value;
        if (!read(name, 16384) || name.isEmpty() || !read(value, quint32(qMin<quint64>(limit, std::numeric_limits<quint32>::max())))) return false;
        const QString format = QString::fromUtf8(name);
        if (format.toUtf8() != name || loaded.contains(format)) return false;
        loaded.insert(format, value);
        if (size(loaded) > limit) return false;
    }
    if (!in.atEnd() || in.status() != QDataStream::Ok) return false;
    item = loaded;
    return true;
}
inline bool terminal(QString windowClass) {
    windowClass = windowClass.toLower();
    for (const auto &name : {"konsole", "org.kde.konsole", "yakuake", "org.kde.yakuake", "kitty", "alacritty", "org.alacritty.alacritty", "foot", "footclient", "wezterm", "org.wezfurlong.wezterm", "gnome-terminal", "gnome-terminal-server", "org.gnome.terminal", "ptyxis", "org.gnome.ptyxis", "xterm", "uxterm", "tilix", "org.gnome.console", "kgx", "terminator", "terminology", "urxvt", "st", "rio", "ghostty", "com.mitchellh.ghostty"})
        if (windowClass == QLatin1String(name)) return true;
    return false;
}
}
