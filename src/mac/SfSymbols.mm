#include "MacIntegration.hpp"

#ifdef __APPLE__
#import <AppKit/AppKit.h>
#include <QColor>
#include <QGuiApplication>
#include <QIconEngine>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#endif

#ifdef __APPLE__

namespace {

// Renders the configured SF Symbol into a freshly allocated bitmap of
// exactly `pixelW × pixelH`, with the glyph aspect-fit and centred. The
// canvas is square in points (pixelW/scale × pixelH/scale) so AppKit's
// drawing math runs at the natural symbol coordinate space, with the
// bitmap context providing high-DPI rasterisation.
QImage renderSymbolToImage(NSString *name, CGFloat pointSize,
                           NSColor *color, int pixelW, int pixelH) {
    if (!name) return QImage();
    NSImage *base = [NSImage imageWithSystemSymbolName:name
                              accessibilityDescription:nil];
    if (!base) return QImage();

    NSImageSymbolConfiguration *cfg =
        [NSImageSymbolConfiguration configurationWithPointSize:pointSize
                                                        weight:NSFontWeightMedium];
    NSImage *img = [base imageWithSymbolConfiguration:cfg] ?: base;

    if (@available(macOS 12.0, *)) {
        NSImageSymbolConfiguration *palette =
            [NSImageSymbolConfiguration configurationWithPaletteColors:@[color]];
        NSImage *colored = [img imageWithSymbolConfiguration:palette];
        if (colored) img = colored;
    } else {
        NSSize sz = img.size;
        NSImage *tinted = [[NSImage alloc] initWithSize:sz];
        [tinted lockFocus];
        [img drawInRect:NSMakeRect(0, 0, sz.width, sz.height)
               fromRect:NSZeroRect
              operation:NSCompositingOperationSourceOver
               fraction:1.0];
        [color set];
        NSRectFillUsingOperation(NSMakeRect(0, 0, sz.width, sz.height),
                                 NSCompositingOperationSourceAtop);
        [tinted unlockFocus];
        img = tinted;
    }

    NSSize intrinsic = img.size;
    if (intrinsic.width <= 0 || intrinsic.height <= 0) {
        intrinsic = NSMakeSize(pointSize, pointSize);
    }

    NSBitmapImageRep *rep = [[NSBitmapImageRep alloc]
        initWithBitmapDataPlanes:NULL
                      pixelsWide:pixelW
                      pixelsHigh:pixelH
                   bitsPerSample:8
                 samplesPerPixel:4
                        hasAlpha:YES
                        isPlanar:NO
                  colorSpaceName:NSDeviceRGBColorSpace
                    bitmapFormat:(NSBitmapFormat)0
                     bytesPerRow:0
                    bitsPerPixel:32];
    if (!rep) return QImage();
    // Map the bitmap's point space to (canvasPts × canvasPts) where the
    // longer axis equals pointSize. This guarantees the glyph fits without
    // top/bottom clipping regardless of intrinsic ascender height.
    const CGFloat canvasPts = pointSize;
    rep.size = NSMakeSize(canvasPts * pixelW / pixelH, canvasPts);

    // Aspect-preserving fit, centred. We deliberately don't use
    // -alignmentRect here because for many SF Symbols the alignment rect
    // sits above the bitmap origin, which pushed the draw rect off-canvas.
    const CGFloat canvasW = canvasPts * pixelW / pixelH;
    const CGFloat canvasH = canvasPts;
    const CGFloat targetMaxPts = canvasPts;
    const CGFloat s = targetMaxPts / MAX(intrinsic.width, intrinsic.height);
    const CGFloat drawW = intrinsic.width * s;
    const CGFloat drawH = intrinsic.height * s;
    const CGFloat drawX = (canvasW - drawW) / 2.0;
    const CGFloat drawY = (canvasH - drawH) / 2.0;

    [NSGraphicsContext saveGraphicsState];
    NSGraphicsContext *ctx = [NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
    [NSGraphicsContext setCurrentContext:ctx];
    ctx.imageInterpolation = NSImageInterpolationHigh;
    [img drawInRect:NSMakeRect(drawX, drawY, drawW, drawH)
           fromRect:NSZeroRect
          operation:NSCompositingOperationSourceOver
           fraction:1.0
     respectFlipped:YES
              hints:@{NSImageHintInterpolation: @(NSImageInterpolationHigh)}];
    [NSGraphicsContext restoreGraphicsState];

    NSData *png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    if (!png || png.length == 0) return QImage();
    QByteArray bytes((const char *)png.bytes, (int)png.length);
    QImage out;
    if (!out.loadFromData(bytes, "PNG")) return QImage();

    out = out.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage tinted(out.size(), QImage::Format_ARGB32_Premultiplied);
    tinted.fill(Qt::transparent);
    QPainter tintPainter(&tinted);
    tintPainter.drawImage(0, 0, out);
    tintPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    tintPainter.fillRect(tinted.rect(), QColor::fromRgbF(color.redComponent,
                                                         color.greenComponent,
                                                         color.blueComponent,
                                                         color.alphaComponent));
    tintPainter.end();
    return tinted;
}

class SfSymbolIconEngine : public QIconEngine {
public:
    SfSymbolIconEngine(QString name, double pointSize, QColor color)
        : m_name(std::move(name)), m_pointSize(pointSize), m_color(std::move(color)) {}

    QIconEngine *clone() const override {
        return new SfSymbolIconEngine(m_name, m_pointSize, m_color);
    }
    QString key() const override { return QStringLiteral("sfsymbol"); }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override {
        return scaledPixmap(size, mode, state, devicePixelRatio());
    }

    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State /*state*/,
                         qreal scale) override {
        if (size.isEmpty() || scale <= 0) return QPixmap();
        const int pxW = qMax(1, qRound(size.width() * scale));
        const int pxH = qMax(1, qRound(size.height() * scale));

        @autoreleasepool {
            NSColor *nsColor = [NSColor colorWithSRGBRed:m_color.redF()
                                                   green:m_color.greenF()
                                                    blue:m_color.blueF()
                                                   alpha:m_color.alphaF()];
            QImage img = renderSymbolToImage(m_name.toNSString(), m_pointSize,
                                             nsColor, pxW, pxH);
            if (img.isNull()) return QPixmap();
            QPixmap pm = QPixmap::fromImage(img);
            pm.setDevicePixelRatio(scale);
            if (mode == QIcon::Disabled) {
                QImage faded = pm.toImage();
                QPainter p(&faded);
                p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                p.fillRect(faded.rect(), QColor(0, 0, 0, 110));
                p.end();
                pm = QPixmap::fromImage(faded);
                pm.setDevicePixelRatio(scale);
            }
            return pm;
        }
    }

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode,
               QIcon::State state) override {
        const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
        QPixmap pm = scaledPixmap(rect.size(), mode, state, dpr);
        painter->drawPixmap(rect, pm);
    }

private:
    static qreal devicePixelRatio() {
        if (QGuiApplication *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
            if (QScreen *s = app->primaryScreen()) return s->devicePixelRatio();
        }
        return 2.0;
    }

    QString m_name;
    double m_pointSize;
    QColor m_color;
};

}  // namespace

#endif  // __APPLE__

namespace mac {

QIcon sfSymbolIcon(const QString &name, double pointSize, const QColor &color) {
#ifdef __APPLE__
    if (@available(macOS 11.0, *)) {
        return QIcon(new SfSymbolIconEngine(name, pointSize, color));
    }
    return QIcon();
#else
    (void)name; (void)pointSize; (void)color;
    return QIcon();
#endif
}

}  // namespace mac
