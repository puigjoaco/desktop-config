#pragma once

#include <QImage>
#include <QMimeData>

namespace ClipboardImage {

// Ownership passes to KSystemClipboard. Keep the decoded image so encoding stays deferred until a client asks.
inline QMimeData *mimeData(const QImage &image) {
    auto *mime = new QMimeData;
    mime->setImageData(image);
    // Like Spectacle, explicitly copied screenshots belong in Klipper even with its default IgnoreImages=true.
    mime->setData(QStringLiteral("x-kde-force-image-copy"), QByteArray());
    return mime;
}

} // namespace ClipboardImage
