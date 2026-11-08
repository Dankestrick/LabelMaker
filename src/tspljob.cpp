#include "tspljob.h"

#include <QString>

namespace {

bool isDark(const QImage &image, int x, int y)
{
    switch (image.format()) {
    case QImage::Format_Mono: {
        const bool bit = image.constScanLine(y)[x >> 3] & static_cast<uchar>(0x80 >> (x & 7));
        return qGray(image.color(bit ? 1 : 0)) < 128;
    }
    case QImage::Format_MonoLSB: {
        const bool bit = image.constScanLine(y)[x >> 3] & static_cast<uchar>(1 << (x & 7));
        return qGray(image.color(bit ? 1 : 0)) < 128;
    }
    case QImage::Format_Grayscale8:
        return image.constScanLine(y)[x] < 128;
    default:
        return qGray(image.pixel(x, y)) < 128;
    }
}

QString formatMm(double mm)
{
    return QString::number(mm, 'f', 1);
}

} // namespace

QByteArray TsplJob::bitmapBytes(const QImage &image, bool blackIsZero)
{
    if (image.isNull() || image.width() < 1 || image.height() < 1)
        return {};

    const int width = image.width();
    const int height = image.height();
    const int bytesPerRow = (width + 7) / 8;
    QByteArray bytes(bytesPerRow * height, 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < bytesPerRow * 8; ++x) {
            const bool dark = x < width && isDark(image, x, y);
            // blackIsZero: a printed dot is bit 0, so a white dot sets the bit.
            const bool bitOn = dark ? !blackIsZero : blackIsZero;
            if (!bitOn)
                continue;
            const int index = y * bytesPerRow + (x >> 3);
            bytes[index] = static_cast<char>(static_cast<uchar>(bytes.at(index)) | static_cast<uchar>(0x80 >> (x & 7)));
        }
    }
    return bytes;
}

QByteArray TsplJob::encode(const QImage &image, const TsplOptions &options)
{
    const QByteArray body = encodeBody(image, options);
    if (body.isEmpty())
        return {};
    return body + "\r\nPRINT " + QByteArray::number(qMax(1, options.copies)) + "\r\n";
}

QByteArray TsplJob::encodeBody(const QImage &image, const TsplOptions &options)
{
    const QByteArray pixels = bitmapBytes(image, options.blackIsZero);
    if (pixels.isEmpty())
        return {};

    const int bytesPerRow = (image.width() + 7) / 8;
    const int density = qBound(1, options.density, 15);
    const QByteArray header =
        "\r\nSIZE " + formatMm(options.widthMm).toLatin1() + " mm,"
        + formatMm(options.heightMm).toLatin1() + " mm\r\n"
        "GAP 3 mm,0 mm\r\n"
        "DIRECTION 1\r\n"
        "DENSITY " + QByteArray::number(density) + "\r\n"
        "CLS\r\n"
        "BITMAP 0,0," + QByteArray::number(bytesPerRow) + ","
        + QByteArray::number(image.height()) + ",0,";
    return header + pixels;
}
