#include "labelrenderer.h"

#include "labelsize.h"

#include <poppler-qt6.h>

#include <QCoreApplication>
#include <QFile>
#include <QImageReader>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("LabelRenderer", text);
}

QImage monoCanvas(int width, int height)
{
    QImage image(width, height, QImage::Format_Mono);
    // Bit 0 is white and bit 1 is black, so a filled 0 is a blank label.
    image.setColor(0, qRgb(255, 255, 255));
    image.setColor(1, qRgb(0, 0, 0));
    image.fill(0);
    return image;
}

void setBlack(QImage &image, int x, int y)
{
    if (x < 0 || y < 0 || x >= image.width() || y >= image.height())
        return;
    image.scanLine(y)[x >> 3] |= static_cast<uchar>(0x80 >> (x & 7));
}

QRect contentBounds(const QImage &image, int whiteLevel)
{
    const QImage gray = image.format() == QImage::Format_Grayscale8
                            ? image
                            : image.convertToFormat(QImage::Format_Grayscale8);
    int minX = gray.width();
    int minY = gray.height();
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < gray.height(); ++y) {
        const uchar *row = gray.constScanLine(y);
        for (int x = 0; x < gray.width(); ++x) {
            if (row[x] >= whiteLevel)
                continue;
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
    }
    if (maxX < 0)
        return {};
    return QRect(minX, minY, maxX - minX + 1, maxY - minY + 1);
}

// Rotate only when the label can hold the artwork clearly better that way.
bool shouldRotate(double srcW, double srcH, double dstW, double dstH)
{
    if (srcW <= 0.0 || srcH <= 0.0 || dstW <= 0.0 || dstH <= 0.0)
        return false;
    const double upright = std::min(dstW / srcW, dstH / srcH);
    const double turned = std::min(dstW / srcH, dstH / srcW);
    return turned > upright * 1.02;
}

void paintGray(QImage &dest, const QImage &gray, int originX, int originY, bool photo, int threshold)
{
    const int width = gray.width();
    const int height = gray.height();
    if (!photo) {
        for (int y = 0; y < height; ++y) {
            const uchar *row = gray.constScanLine(y);
            for (int x = 0; x < width; ++x) {
                if (row[x] < threshold)
                    setBlack(dest, originX + x, originY + y);
            }
        }
        return;
    }

    // Floyd–Steinberg. Photos want the grays; barcodes do not.
    std::vector<double> pixels(static_cast<size_t>(width * height));
    for (int y = 0; y < height; ++y) {
        const uchar *row = gray.constScanLine(y);
        for (int x = 0; x < width; ++x)
            pixels[static_cast<size_t>(y * width + x)] = row[x];
    }
    auto at = [&](int x, int y) -> double & {
        return pixels[static_cast<size_t>(y * width + x)];
    };
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const double oldValue = std::clamp(at(x, y), 0.0, 255.0);
            const bool black = oldValue < threshold;
            const double neu = black ? 0.0 : 255.0;
            const double error = oldValue - neu;
            if (black)
                setBlack(dest, originX + x, originY + y);
            auto spread = [&](int xx, int yy, double factor) {
                if (xx >= 0 && xx < width && yy >= 0 && yy < height)
                    at(xx, yy) += error * factor;
            };
            spread(x + 1, y, 7.0 / 16.0);
            spread(x - 1, y + 1, 3.0 / 16.0);
            spread(x, y + 1, 5.0 / 16.0);
            spread(x + 1, y + 1, 1.0 / 16.0);
        }
    }
}

QImage toGray(const QImage &source, int width, int height, bool photo)
{
    const auto mode = photo ? Qt::SmoothTransformation : Qt::FastTransformation;
    if (source.width() == width && source.height() == height)
        return source.convertToFormat(QImage::Format_Grayscale8);
    return source.scaled(width, height, Qt::IgnoreAspectRatio, mode)
        .convertToFormat(QImage::Format_Grayscale8);
}

QImage flatten(const QImage &source)
{
    QImage base(source.size(), QImage::Format_RGB32);
    base.fill(Qt::white);
    QPainter painter(&base);
    painter.drawImage(0, 0, source);
    return base;
}

bool looksLikePdf(const QString &path)
{
    if (path.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive))
        return true;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    return file.read(5) == "%PDF-";
}

LabelRenderer::Result fail(const QString &error, int pageCount = 0)
{
    LabelRenderer::Result result;
    result.error = error;
    result.pageCount = pageCount;
    return result;
}

QImage renderPdfRegion(Poppler::Page *page, const QRectF &points, double dpi)
{
    dpi = std::clamp(dpi, 36.0, 2400.0);
    const int x = std::max(0, int(std::floor(points.x() / 72.0 * dpi)));
    const int y = std::max(0, int(std::floor(points.y() / 72.0 * dpi)));
    const int w = std::max(1, int(std::lround(points.width() / 72.0 * dpi)));
    const int h = std::max(1, int(std::lround(points.height() / 72.0 * dpi)));
    const QImage part = page->renderToImage(dpi, dpi, x, y, w, h);
    if (!part.isNull())
        return part;

    // Poppler documents the region arguments as lightly tested. Fall back
    // to the whole page only when that bitmap stays modest.
    if (double(w) * double(h) > 4000.0 * 4000.0)
        return {};
    const QImage full = page->renderToImage(dpi, dpi);
    if (full.isNull())
        return {};
    return full.copy(x, y, std::min(w, full.width() - x), std::min(h, full.height() - y));
}

void cropFill(QImage &gray, int labelW, int labelH, int *originX, int *originY)
{
    if (gray.width() > labelW || gray.height() > labelH) {
        const int cx = std::max(0, (gray.width() - labelW) / 2);
        const int cy = std::max(0, (gray.height() - labelH) / 2);
        gray = gray.copy(cx, cy, std::min(labelW, gray.width() - cx), std::min(labelH, gray.height() - cy));
    }
    *originX = (labelW - gray.width()) / 2;
    *originY = (labelH - gray.height()) / 2;
}

LabelRenderer::Result renderPdf(const QString &path, const LabelRenderer::Request &request,
                                int labelW, int labelH, int margin)
{
    std::unique_ptr<Poppler::Document> document = Poppler::Document::load(path);
    if (!document)
        return fail(tr("Couldn't open that file."));
    if (document->isLocked())
        return fail(tr("That PDF is locked."));

    // Barcodes stay crisp when the vectors are not antialiased before the
    // hard threshold.
    document->setRenderHint(Poppler::Document::Antialiasing, false);
    document->setRenderHint(Poppler::Document::TextAntialiasing, false);
    document->setRenderHint(Poppler::Document::TextHinting, false);

    const int pageCount = document->numPages();
    if (request.pageIndex < 0 || request.pageIndex >= pageCount)
        return fail(tr("That page isn't in this PDF."), pageCount);

    std::unique_ptr<Poppler::Page> page = document->page(request.pageIndex);
    if (!page)
        return fail(tr("Couldn't draw that page."), pageCount);

    const QSizeF pagePt = page->pageSizeF();
    QRectF sourcePt(0, 0, pagePt.width(), pagePt.height());
    if (request.fit == LabelRenderer::Fit::Shipping) {
        constexpr double detectDpi = 72.0;
        const QImage detected = page->renderToImage(detectDpi, detectDpi);
        if (detected.isNull())
            return fail(tr("Couldn't draw that page."), pageCount);
        const QRect bounds = contentBounds(detected, 250);
        if (bounds.isEmpty())
            return fail(tr("This page looks blank."), pageCount);
        const double ptPerPx = 72.0 / detectDpi;
        sourcePt = QRectF(bounds.x() * ptPerPx, bounds.y() * ptPerPx,
                          bounds.width() * ptPerPx, bounds.height() * ptPerPx);
    }

    const bool fill = request.fit == LabelRenderer::Fit::Fill;
    const int slotW = fill ? labelW : std::max(8, labelW - 2 * margin);
    const int slotH = fill ? labelH : std::max(8, labelH - 2 * margin);
    const double srcW = sourcePt.width() / 72.0 * 203.0;
    const double srcH = sourcePt.height() / 72.0 * 203.0;
    // Only shipping turns the artwork. Fit whole page keeps the page's
    // orientation and letterboxes. Fill crops that same orientation.
    const bool rotated = request.fit == LabelRenderer::Fit::Shipping
                         && shouldRotate(srcW, srcH, slotW, slotH);
    const double placedW = rotated ? srcH : srcW;
    const double placedH = rotated ? srcW : srcH;
    const double scale = fill ? std::max(slotW / placedW, slotH / placedH)
                              : std::min(slotW / placedW, slotH / placedH);
    const int outW = std::max(1, int(std::lround(placedW * scale)));
    const int outH = std::max(1, int(std::lround(placedH * scale)));

    // Ask Poppler for the dots we will actually print. The cap stops a tiny
    // speck from requesting a huge render; pixels are not stretched past it.
    const double placedInches = std::max(0.01, (rotated ? sourcePt.height() : sourcePt.width()) / 72.0);
    const double dpi = std::min(2400.0, outW / placedInches);
    QImage drawn = renderPdfRegion(page.get(), sourcePt, dpi);
    if (drawn.isNull())
        return fail(tr("Couldn't draw that page."), pageCount);
    if (rotated)
        drawn = drawn.transformed(QTransform().rotate(90), Qt::FastTransformation);

    const int targetW = fill ? outW : std::min(outW, slotW);
    const int targetH = fill ? outH : std::min(outH, slotH);
    QImage gray = toGray(drawn, targetW, targetH, request.photo);

    int originX = 0;
    int originY = 0;
    cropFill(gray, labelW, labelH, &originX, &originY);

    LabelRenderer::Result result;
    result.pageCount = pageCount;
    result.rotated = rotated;
    result.image = monoCanvas(labelW, labelH);
    paintGray(result.image, gray, originX, originY, request.photo, request.threshold);
    return result;
}

LabelRenderer::Result renderPicture(const QString &path, const LabelRenderer::Request &request,
                                   int labelW, int labelH, int margin)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage raw = reader.read();
    if (raw.isNull())
        return fail(tr("That isn't a PDF or a picture this app can open."));

    const QImage flat = flatten(raw);
    QRect region(0, 0, flat.width(), flat.height());
    if (request.fit == LabelRenderer::Fit::Shipping) {
        region = contentBounds(flat, 250);
        if (region.isEmpty())
            return fail(tr("This page looks blank."));
    }

    const bool fill = request.fit == LabelRenderer::Fit::Fill;
    const int slotW = fill ? labelW : std::max(8, labelW - 2 * margin);
    const int slotH = fill ? labelH : std::max(8, labelH - 2 * margin);
    const double srcW = region.width();
    const double srcH = region.height();
    const bool rotated = request.fit == LabelRenderer::Fit::Shipping
                         && shouldRotate(srcW, srcH, slotW, slotH);
    const double placedW = rotated ? srcH : srcW;
    const double placedH = rotated ? srcW : srcH;
    double scale = fill ? std::max(slotW / placedW, slotH / placedH)
                        : std::min(slotW / placedW, slotH / placedH);
    // A picture has a fixed pixel grid. Past 2x it just gets soft, so stop.
    if (scale > request.maxImageScale)
        scale = request.maxImageScale;

    const int outW = std::max(1, int(std::lround(placedW * scale)));
    const int outH = std::max(1, int(std::lround(placedH * scale)));
    QImage crop = flat.copy(region);
    if (rotated)
        crop = crop.transformed(QTransform().rotate(90), Qt::FastTransformation);
    QImage gray = toGray(crop, outW, outH, request.photo);

    int originX = 0;
    int originY = 0;
    cropFill(gray, labelW, labelH, &originX, &originY);

    LabelRenderer::Result result;
    result.pageCount = 1;
    result.rotated = rotated;
    result.image = monoCanvas(labelW, labelH);
    paintGray(result.image, gray, originX, originY, request.photo, request.threshold);
    return result;
}

} // namespace

LabelRenderer::Result LabelRenderer::renderFile(const QString &path, const Request &request)
{
    if (request.widthMm <= 0.0 || request.heightMm <= 0.0)
        return fail(tr("Enter a width and a height."));
    // The D450 head is 4 inches. A little past that is rejected so a typo
    // cannot allocate a huge bitmap.
    if (request.widthMm > 120.0 || request.heightMm > 400.0)
        return fail(tr("That label is too big."));

    const int labelW = labelWidthDots(request.widthMm);
    const int labelH = labelHeightDots(request.heightMm);
    if (labelW < 8 || labelH < 8)
        return fail(tr("Enter a width and a height."));

    int margin = request.fit == Fit::Fill ? 0 : qRound(request.marginMm * 203.0 / 25.4);
    if (request.fit != Fit::Fill && (labelW - 2 * margin < 8 || labelH - 2 * margin < 8))
        margin = 0;

    if (looksLikePdf(path))
        return renderPdf(path, request, labelW, labelH, margin);
    return renderPicture(path, request, labelW, labelH, margin);
}
