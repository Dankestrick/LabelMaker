#pragma once

#include <QImage>
#include <QString>

// Turns a PDF page or a picture into the 1-bit image the printer will burn.
// No UI. Width is a multiple of 8 dots at 203 DPI.
class LabelRenderer {
public:
    enum class Fit { Shipping, Page, Fill };

    struct Request {
        double widthMm = 101.6;
        double heightMm = 152.4;
        int pageIndex = 0;
        Fit fit = Fit::Shipping;
        bool photo = false;
        int threshold = 160;
        double marginMm = 2.0;
        // Pictures are not enlarged past this factor. PDFs are scaled by
        // asking Poppler to render at the matching resolution instead.
        double maxImageScale = 2.0;
    };

    struct Result {
        QImage image;
        int pageCount = 0;
        bool rotated = false;
        QString error;
        bool ok() const { return error.isEmpty() && !image.isNull(); }
    };

    static Result renderFile(const QString &path, const Request &request);
};
