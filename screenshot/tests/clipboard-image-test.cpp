#include "../clipboard-image.h"
#include <QTest>
#include <memory>

class ClipboardImageTest final : public QObject {
    Q_OBJECT
private slots:
    void explicitlyCopiedImageIsKeptInHistory() {
        QImage image(3, 2, QImage::Format_ARGB32);
        image.fill(qRgba(12, 34, 56, 78));
        image.setPixel(1, 1, qRgba(90, 80, 70, 60));
        const std::unique_ptr<QMimeData> mime(ClipboardImage::mimeData(image));

        // Klipper tests the presence of this format, including when its payload is empty.
        QVERIFY(mime->hasFormat(QStringLiteral("x-kde-force-image-copy")));
        QVERIFY(mime->data(QStringLiteral("x-kde-force-image-copy")).isEmpty());
        QVERIFY(mime->hasImage());
        QCOMPARE(qvariant_cast<QImage>(mime->imageData()), image);
        // The fast offer keeps implicitly shared pixels, without encoding a PNG on release.
        QCOMPARE(qvariant_cast<QImage>(mime->imageData()).constBits(), image.constBits());
        QVERIFY(!mime->hasFormat(QStringLiteral("image/png")));
        QVERIFY(!mime->hasText());
        QVERIFY(!mime->hasUrls());
    }
};

QTEST_GUILESS_MAIN(ClipboardImageTest)
#include "clipboard-image-test.moc"
