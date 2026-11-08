#pragma once

#include <QByteArray>
#include <QImage>

// One TSPL print job. The bytes are ready for the BLE link; this class
// does not open the printer.
struct TsplOptions {
    double widthMm = 101.6;
    double heightMm = 152.4;
    int density = 10;
    int copies = 1;
    // BITMAP bit 0 is a printed (black) dot. Checked on the D450's paper.
    bool blackIsZero = true;
};

class TsplJob {
public:
    static QByteArray encode(const QImage &image, const TsplOptions &options);
    // Everything before PRINT, so the link can check the transfer first.
    static QByteArray encodeBody(const QImage &image, const TsplOptions &options);
    static QByteArray bitmapBytes(const QImage &image, bool blackIsZero);
};
