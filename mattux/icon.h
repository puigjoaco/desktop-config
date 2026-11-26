#pragma once
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>

namespace Mattux {
inline QIcon stackIcon(int count, bool running, const QPalette &palette) {
    QIcon icon;
    for (int size : {16, 22, 32, 44, 48, 64}) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool smallGlyph = size == 16 && (!running || count == 0);
        painter.scale(size / (smallGlyph ? 16.0 : 22.0), size / (smallGlyph ? 16.0 : 22.0));
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette.color(QPalette::WindowText));
        painter.setOpacity(running ? 1.0 : 0.4);
        QPainterPath glyph;
        if (smallGlyph) {
            glyph.addRect(2, 6, 8, 8);
            glyph.addRect(3, 7, 6, 6);
            glyph.moveTo(4, 4); glyph.lineTo(12, 4); glyph.lineTo(12, 12); glyph.lineTo(11, 12); glyph.lineTo(11, 5); glyph.lineTo(4, 5); glyph.closeSubpath();
            glyph.moveTo(6, 2); glyph.lineTo(14, 2); glyph.lineTo(14, 10); glyph.lineTo(13, 10); glyph.lineTo(13, 3); glyph.lineTo(6, 3); glyph.closeSubpath();
        } else {
            glyph.addRect(3, 7, 12, 12);
            glyph.addRect(4, 8, 10, 10); // Odd-even fill cuts out the front sheet.
            glyph.moveTo(5, 5); glyph.lineTo(17, 5); glyph.lineTo(17, 17); glyph.lineTo(16, 17); glyph.lineTo(16, 6); glyph.lineTo(5, 6); glyph.closeSubpath();
            glyph.moveTo(7, 3); glyph.lineTo(19, 3); glyph.lineTo(19, 15); glyph.lineTo(18, 15); glyph.lineTo(18, 4); glyph.lineTo(7, 4); glyph.closeSubpath();
        }
        painter.drawPath(glyph);
        if (running && count > 0) {
            const int width = count < 10 ? 12 : count < 100 ? 15 : 19;
            const QRectF pill(22 - width, 10, width, 12);
            painter.setCompositionMode(QPainter::CompositionMode_Clear);
            painter.drawRoundedRect(pill.adjusted(-1.5, -1.5, 1.5, 1.5), 7.5, 7.5);
            painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
            painter.setBrush(palette.color(QPalette::Highlight));
            painter.drawRoundedRect(pill, 6, 6);
            QFont font(QStringLiteral("Noto Sans"));
            font.setPixelSize(9); font.setBold(true);
            painter.setFont(font); painter.setPen(Qt::white);
            painter.drawText(pill, Qt::AlignCenter, QString::number(count));
        }
        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}
}
