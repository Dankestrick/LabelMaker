#include "labelrenderer.h"

#include "labelsize.h"

#include <poppler-qt6.h>

#include <QCoreApplication>
#include <QFile>
#include <QImageReader>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <cstdlib>
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

// A 4x6 label, or the same label turned sideways, within 20%.
bool nearLabelShape(double widthIn, double heightIn)
{
    auto within = [](double value, double target) {
        return value >= target * 0.8 && value <= target * 1.2;
    };
    return (within(widthIn, 4.0) && within(heightIn, 6.0))
           || (within(widthIn, 6.0) && within(heightIn, 4.0));
}

struct InkMask {
    int width = 0;
    int height = 0;
    std::vector<uchar> on;
};

InkMask inkMaskFrom(const QImage &image, int whiteLevel)
{
    const QImage gray = image.format() == QImage::Format_Grayscale8
                            ? image
                            : image.convertToFormat(QImage::Format_Grayscale8);
    InkMask mask;
    mask.width = gray.width();
    mask.height = gray.height();
    mask.on.assign(static_cast<size_t>(mask.width * mask.height), 0);
    for (int y = 0; y < mask.height; ++y) {
        const uchar *row = gray.constScanLine(y);
        for (int x = 0; x < mask.width; ++x)
            mask.on[static_cast<size_t>(y * mask.width + x)] = row[x] < whiteLevel ? 1 : 0;
    }
    return mask;
}

// Dotted cut lines are just specks once each dot is its own blob.
void removeSpecks(InkMask *mask, int speckPx)
{
    const int width = mask->width;
    const int height = mask->height;
    std::vector<uchar> seen(mask->on.size(), 0);
    std::vector<int> stack;
    std::vector<int> blob;
    const int step[4] = {1, -1, width, -width};
    for (int start = 0; start < width * height; ++start) {
        if (!mask->on[static_cast<size_t>(start)] || seen[static_cast<size_t>(start)])
            continue;
        stack.clear();
        blob.clear();
        stack.push_back(start);
        seen[static_cast<size_t>(start)] = 1;
        int minX = width;
        int minY = height;
        int maxX = 0;
        int maxY = 0;
        while (!stack.empty()) {
            const int index = stack.back();
            stack.pop_back();
            blob.push_back(index);
            const int x = index % width;
            const int y = index / width;
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
            for (int delta : step) {
                const int next = index + delta;
                if (next < 0 || next >= width * height || seen[static_cast<size_t>(next)]
                    || !mask->on[static_cast<size_t>(next)])
                    continue;
                const int nx = next % width;
                const int ny = next / width;
                if (std::abs(nx - x) + std::abs(ny - y) != 1)
                    continue;
                seen[static_cast<size_t>(next)] = 1;
                stack.push_back(next);
            }
        }
        if (maxX - minX + 1 < speckPx && maxY - minY + 1 < speckPx) {
            for (int index : blob)
                mask->on[static_cast<size_t>(index)] = 0;
        }
    }
}

QRect trimToInk(const InkMask &mask, QRect rect)
{
    rect = rect.intersected(QRect(0, 0, mask.width, mask.height));
    if (rect.isEmpty())
        return {};
    int minX = rect.right();
    int minY = rect.bottom();
    int maxX = rect.left();
    int maxY = rect.top();
    bool any = false;
    for (int y = rect.top(); y <= rect.bottom(); ++y) {
        const uchar *row = mask.on.data() + static_cast<size_t>(y * mask.width);
        for (int x = rect.left(); x <= rect.right(); ++x) {
            if (!row[x])
                continue;
            any = true;
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
    }
    if (!any)
        return {};
    return QRect(minX, minY, maxX - minX + 1, maxY - minY + 1);
}

int countInk(const InkMask &mask, const QRect &rect)
{
    int count = 0;
    for (int y = rect.top(); y <= rect.bottom(); ++y) {
        const uchar *row = mask.on.data() + static_cast<size_t>(y * mask.width);
        for (int x = rect.left(); x <= rect.right(); ++x)
            count += row[x];
    }
    return count;
}

// White rows, plus a ruled line thin enough to be a cut mark rather than text.
std::vector<char> separatorRuns(const InkMask &mask, const QRect &rect, bool horizontal, int thinPx)
{
    const int span = horizontal ? rect.height() : rect.width();
    std::vector<char> white(static_cast<size_t>(span), 0);
    for (int i = 0; i < span; ++i) {
        bool ink = false;
        if (horizontal) {
            const int y = rect.top() + i;
            const uchar *row = mask.on.data() + static_cast<size_t>(y * mask.width);
            for (int x = rect.left(); x <= rect.right(); ++x) {
                if (row[x]) {
                    ink = true;
                    break;
                }
            }
        } else {
            const int x = rect.left() + i;
            for (int y = rect.top(); y <= rect.bottom(); ++y) {
                if (mask.on[static_cast<size_t>(y * mask.width + x)]) {
                    ink = true;
                    break;
                }
            }
        }
        white[static_cast<size_t>(i)] = ink ? 0 : 1;
    }
    for (int i = 0; i < span;) {
        if (white[static_cast<size_t>(i)]) {
            ++i;
            continue;
        }
        int end = i;
        while (end < span && !white[static_cast<size_t>(end)])
            ++end;
        const bool sandwiched = (i == 0 || white[static_cast<size_t>(i - 1)])
                                && (end == span || white[static_cast<size_t>(end)]);
        if (sandwiched && end - i <= thinPx) {
            for (int k = i; k < end; ++k)
                white[static_cast<size_t>(k)] = 1;
        }
        i = end;
    }
    return white;
}

std::vector<QRect> splitOnGaps(const InkMask &mask, const QRect &rect, bool horizontal, int gapPx, int thinPx)
{
    const std::vector<char> white = separatorRuns(mask, rect, horizontal, thinPx);
    const int span = static_cast<int>(white.size());
    std::vector<QRect> parts;
    int cursor = 0;
    bool split = false;
    for (int i = 0; i < span;) {
        if (!white[static_cast<size_t>(i)]) {
            ++i;
            continue;
        }
        int end = i;
        while (end < span && white[static_cast<size_t>(end)])
            ++end;
        if (end - i >= gapPx) {
            split = true;
            if (i > cursor) {
                parts.push_back(horizontal ? QRect(rect.left(), rect.top() + cursor, rect.width(), i - cursor)
                                           : QRect(rect.left() + cursor, rect.top(), i - cursor, rect.height()));
            }
            cursor = end;
        }
        i = end;
    }
    if (!split)
        return {rect};
    if (cursor < span) {
        parts.push_back(horizontal ? QRect(rect.left(), rect.top() + cursor, rect.width(), span - cursor)
                                   : QRect(rect.left() + cursor, rect.top(), span - cursor, rect.height()));
    }
    return parts;
}

struct InkBlock {
    QRect bounds;
    int ink = 0;
};

void collectBlocks(const InkMask &mask, QRect rect, int gapPx, int thinPx, std::vector<InkBlock> *blocks)
{
    rect = trimToInk(mask, rect);
    if (rect.isEmpty())
        return;

    const auto descend = [&](const std::vector<QRect> &parts) {
        const bool sliced = parts.size() >= 2
                            || (parts.size() == 1
                                && (parts[0].width() < rect.width() || parts[0].height() < rect.height()));
        if (!sliced)
            return false;
        for (const QRect &part : parts)
            collectBlocks(mask, part, gapPx, thinPx, blocks);
        return true;
    };
    if (descend(splitOnGaps(mask, rect, true, gapPx, thinPx)))
        return;
    if (descend(splitOnGaps(mask, rect, false, gapPx, thinPx)))
        return;

    InkBlock block;
    block.bounds = rect;
    block.ink = countInk(mask, rect);
    if (block.ink > 0)
        blocks->push_back(block);
}

// The label is the 4x6-shaped block with the most ink. Anything else falls
// back to the biggest block. A block that already covers the page stays whole.
QRect shippingBlock(const QImage &detected, double dpi)
{
    if (detected.isNull() || dpi <= 0.0)
        return {};
    InkMask mask = inkMaskFrom(detected, 250);
    const int speckPx = std::max(2, int(std::lround(2.0 / 25.4 * dpi)));
    const int gapPx = std::max(4, int(std::lround(0.25 * dpi)));
    const int thinPx = std::max(1, int(std::lround(1.0 / 25.4 * dpi)));
    removeSpecks(&mask, speckPx);

    std::vector<InkBlock> blocks;
    collectBlocks(mask, QRect(0, 0, mask.width, mask.height), gapPx, thinPx, &blocks);
    if (blocks.empty())
        return {};

    const InkBlock *shaped = nullptr;
    const InkBlock *largest = nullptr;
    for (const InkBlock &block : blocks) {
        if (!largest || block.bounds.width() * block.bounds.height()
                          > largest->bounds.width() * largest->bounds.height())
            largest = &block;
        if (!nearLabelShape(block.bounds.width() / dpi, block.bounds.height() / dpi))
            continue;
        if (!shaped || block.ink > shaped->ink)
            shaped = &block;
    }
    const InkBlock *chosen = shaped ? shaped : largest;
    if (chosen->bounds.width() >= mask.width * 0.9 && chosen->bounds.height() >= mask.height * 0.9)
        return QRect(0, 0, mask.width, mask.height);
    return chosen->bounds;
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
    // Clear pixels are stored as black with alpha 0. Composite them onto
    // white before any grayscale test, or the padding prints as ink.
    QImage base(source.size(), QImage::Format_RGB32);
    base.fill(Qt::white);
    {
        QPainter painter(&base);
        painter.drawImage(0, 0, source);
    }
    return base;
}

bool usesTransparency(const QImage &image)
{
    if (!image.hasAlphaChannel())
        return false;
    const QImage argb = image.format() == QImage::Format_ARGB32
                            ? image
                            : image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < argb.height(); ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(argb.constScanLine(y));
        for (int x = 0; x < argb.width(); ++x) {
            if (qAlpha(row[x]) < 16)
                return true;
        }
    }
    return false;
}

// Shipping always frames the ink. Fill does too, so the cover-crop stays on
// the artwork instead of the middle of a loose canvas. Fit keeps an opaque
// picture whole, and treats a clear background as padding.
QRect artworkRect(const QImage &flat, const QImage &raw, LabelRenderer::Fit fit)
{
    const QRect full(0, 0, flat.width(), flat.height());
    const QRect ink = contentBounds(flat, 250);
    if (fit == LabelRenderer::Fit::Shipping)
        return ink;
    if (!ink.isEmpty() && (fit == LabelRenderer::Fit::Fill || usesTransparency(raw)))
        return ink;
    return full;
}

bool wantsRotate(LabelRenderer::Turn turn, double srcW, double srcH, double dstW, double dstH)
{
    if (turn == LabelRenderer::Turn::Sideways)
        return true;
    if (turn == LabelRenderer::Turn::Upright)
        return false;
    return shouldRotate(srcW, srcH, dstW, dstH);
}

// Less than one dot in a thousand is ink. A real mark is well above this.
bool almostNoInk(const QImage &image)
{
    const int total = image.width() * image.height();
    if (total <= 0)
        return true;
    int black = 0;
    for (int y = 0; y < image.height(); ++y) {
        const uchar *row = image.constScanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            if (row[x >> 3] & static_cast<uchar>(0x80 >> (x & 7)))
                ++black;
        }
    }
    return black * 1000 < total;
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
    if (request.fit == LabelRenderer::Fit::Shipping
        && !nearLabelShape(pagePt.width() / 72.0, pagePt.height() / 72.0)) {
        // A letter page keeps the label and drops the instructions beside it.
        // A page that is already 4x6 is left whole, above.
        constexpr double detectDpi = 72.0;
        const QImage detected = page->renderToImage(detectDpi, detectDpi);
        if (detected.isNull())
            return fail(tr("Couldn't draw that page."), pageCount);
        const QRect bounds = shippingBlock(detected, detectDpi);
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
    const bool rotated = wantsRotate(request.turn, srcW, srcH, slotW, slotH);
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
    result.blank = almostNoInk(result.image);
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
    const QRect region = artworkRect(flat, raw, request.fit);
    if (request.fit == LabelRenderer::Fit::Shipping && region.isEmpty())
        return fail(tr("This page looks blank."));

    const bool fill = request.fit == LabelRenderer::Fit::Fill;
    const int slotW = fill ? labelW : std::max(8, labelW - 2 * margin);
    const int slotH = fill ? labelH : std::max(8, labelH - 2 * margin);
    const double srcW = region.width();
    const double srcH = region.height();
    const bool rotated = wantsRotate(request.turn, srcW, srcH, slotW, slotH);
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
    result.blank = almostNoInk(result.image);
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
