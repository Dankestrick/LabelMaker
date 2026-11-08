#pragma once

#include <QImage>
#include <QString>

// Turns a PDF page or a picture into the 1-bit image the printer will burn.
// No UI. Width is a multiple of 8 dots at 203 DPI.
class LabelRenderer {
public:
    enum class Fit { Shipping, Page, Fill };
    // Auto uses the same "fills the label better" check for every fit.
    enum class Turn { Auto, Upright, Sideways };

    struct Request {
        double widthMm = 101.6;
        double heightMm = 152.4;
        int pageIndex = 0;
        Fit fit = Fit::Shipping;
        Turn turn = Turn::Auto;
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
        // The 1-bit label is white, or only a speck of ink. White art on a
        // clear background lands here.
        bool blank = false;
        QString error;
        bool ok() const { return error.isEmpty() && !image.isNull(); }
    };

    static Result renderFile(const QString &path, const Request &request);
};
