#include <algorithm>
#include <cstring>
#include <functional>

#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QPageLayout>
#include <QPageSize>
#include <QPdfWriter>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "labelimageprovider.h"
#include "labelrenderer.h"
#include "labelsize.h"
#include "theme.h"
#include "translator.h"
#include "tspljob.h"

namespace {

struct Coverage {
    int black = 0;
    int other = 0;
    int total = 0;
    QRect box;
};

Coverage cover(const QImage &image)
{
    Coverage result;
    result.total = image.width() * image.height();
    int minX = image.width();
    int minY = image.height();
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = image.pixel(x, y);
            if (pixel == qRgb(0, 0, 0)) {
                ++result.black;
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            } else if (pixel != qRgb(255, 255, 255)) {
                ++result.other;
            }
        }
    }
    if (maxX >= 0)
        result.box = QRect(minX, minY, maxX - minX + 1, maxY - minY + 1);
    return result;
}

struct Dot {
    int count = 0;
    double x = 0;
    double y = 0;
};

Dot whiteDot(const QImage &image, const QRect &bounds)
{
    Dot dot;
    long long sx = 0;
    long long sy = 0;
    const QRect area = bounds.intersected(QRect(0, 0, image.width(), image.height()));
    for (int y = area.top(); y <= area.bottom(); ++y) {
        for (int x = area.left(); x <= area.right(); ++x) {
            if (image.pixel(x, y) != qRgb(255, 255, 255))
                continue;
            ++dot.count;
            sx += x;
            sy += y;
        }
    }
    if (dot.count > 0) {
        dot.x = double(sx) / dot.count;
        dot.y = double(sy) / dot.count;
    }
    return dot;
}

bool samePixels(const QImage &a, const QImage &b)
{
    if (a.size() != b.size())
        return false;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            if (a.pixel(x, y) != b.pixel(x, y))
                return false;
        }
    }
    return true;
}

} // namespace

class LabelMakerTests : public QObject {
    Q_OBJECT

private slots:
    void sizeRoundsWidthDownToBytes();
    void shippingFindsHalfPage_data();
    void shippingFindsHalfPage();
    void shippingIgnoresTextBesideTheLabel();
    void shippingIgnoresADottedCutLine();
    void shippingTurnsALandscapeLabel();
    void shippingKeepsANativeLabel();
    void blankPageIsAnError();
    void secondPageCanBeBlank();
    void thresholdIsOnlyBlackOrWhite();
    void pictureDoesNotUpscalePastTwo();
    void photoModeDithers();
    void widePictureTurnsInFitAndFill();
    void turnOverride();
    void fillStaysCenteredOnTheInk();
    void transparentPrintsWhite();
    void whiteArtRaisesTheBlankHint();
    void turnUndoesAndResetsOnANewFile();
    void quarterTurnsOfAWideImage();
    void portraitLabelAtZeroStaysPut();
    void rotateFourTimesReturnsToAuto();
    void encoderHeaderAndByteCount();
    void polaritySwitchInvertsBytes();
    void themeFallsBack();
    void themeReadsAccent();
    void themeFollowsHyprlandRounding();
    void translatorFallsBackToEnglish();
    void translatorFollowsTheLanguageFile();
    void translatorPicksPluralForms();
    void windowLoads();

private:
    static void writeFile(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(contents), qint64(contents.size()));
    }

    static void writeLetter(const QString &path, const QRectF &black, bool secondBlank = false)
    {
        QPdfWriter writer(path);
        writer.setPageLayout(QPageLayout(QPageSize(QPageSize::Letter), QPageLayout::Portrait,
                                         QMarginsF(0, 0, 0, 0)));
        writer.setResolution(72);
        QPainter painter(&writer);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::black);
        // A null rect still leaves a mark with some painters. A blank page
        // is one where nothing was drawn.
        if (black.isValid() && !black.isEmpty())
            painter.drawRect(black);
        if (secondBlank)
            writer.newPage();
        painter.end();
    }

    static void paintPdf(const QString &path, const QPageSize &pageSize,
                         const std::function<void(QPainter &)> &draw)
    {
        QPdfWriter writer(path);
        writer.setPageLayout(QPageLayout(pageSize, QPageLayout::Portrait, QMarginsF(0, 0, 0, 0)));
        writer.setResolution(72);
        QPainter painter(&writer);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::black);
        draw(painter);
        painter.end();
    }
};

void LabelMakerTests::sizeRoundsWidthDownToBytes()
{
    QCOMPARE(labelDotsFromMm(101.6), 812);
    QCOMPARE(labelHeightDots(152.4), 1218);
    QCOMPARE(labelWidthDots(101.6), 808);
    QCOMPARE(labelWidthDots(101.6) % 8, 0);
    QCOMPARE(labelWidthDots(76.2), 608);
    QCOMPARE(labelHeightDots(50.8), 406);
    QCOMPARE(labelWidthDots(50.8), 400);
    QCOMPARE(labelHeightDots(25.4), 203);
    QCOMPARE(labelWidthDots(50.0), 400);
    QCOMPARE(labelHeightDots(30.0), 240);

    double width = 0;
    double height = 0;
    QVERIFY(parseLabelSize(QStringLiteral("4x6"), &width, &height));
    QCOMPARE(width, 101.6);
    QCOMPARE(height, 152.4);
    QVERIFY(parseLabelSize(QStringLiteral("50x30mm"), &width, &height));
    QCOMPARE(width, 50.0);
    QCOMPARE(height, 30.0);
    QVERIFY(!parseLabelSize(QStringLiteral("nope"), &width, &height));
}

void LabelMakerTests::shippingFindsHalfPage_data()
{
    QTest::addColumn<QRectF>("rect");
    QTest::addColumn<bool>("rotated");
    QTest::newRow("left") << QRectF(36, 72, 288, 432) << false;
    QTest::newRow("right") << QRectF(612 - 36 - 288, 72, 288, 432) << false;
    QTest::newRow("top") << QRectF(36, 36, 432, 288) << true;
}

void LabelMakerTests::shippingFindsHalfPage()
{
    QFETCH(QRectF, rect);
    QFETCH(bool, rotated);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("half.pdf");
    writeLetter(path, rect);

    LabelRenderer::Request request;
    const LabelRenderer::Result result = LabelRenderer::renderFile(path, request);
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.image.format(), QImage::Format_Mono);
    QCOMPARE(result.image.width(), 808);
    QCOMPARE(result.image.height(), 1218);
    QCOMPARE(result.rotated, rotated);

    const Coverage pixels = cover(result.image);
    QCOMPARE(pixels.other, 0);
    QVERIFY2(double(pixels.black) / pixels.total > 0.75, qPrintable(QString::number(pixels.black)));
}

void LabelMakerTests::shippingIgnoresTextBesideTheLabel()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("beside.pdf");
    // 4x6 block on the left, a few instruction lines on the right, half an inch away.
    paintPdf(path, QPageSize(QPageSize::Letter), [](QPainter &painter) {
        painter.drawRect(QRectF(36, 72, 288, 432));
        painter.drawRect(QRectF(360, 180, 160, 8));
        painter.drawRect(QRectF(360, 196, 140, 8));
        painter.drawRect(QRectF(360, 212, 120, 8));
    });

    const LabelRenderer::Result result = LabelRenderer::renderFile(path, {});
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.rotated, false);
    const Coverage pixels = cover(result.image);
    QCOMPARE(pixels.other, 0);
    QVERIFY2(double(pixels.black) / pixels.total > 0.75, qPrintable(QString::number(pixels.black)));
}

void LabelMakerTests::shippingIgnoresADottedCutLine()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("dotted.pdf");
    paintPdf(path, QPageSize(QPageSize::Letter), [](QPainter &painter) {
        painter.drawRect(QRectF(36, 36, 288, 432));
        for (int x = 36; x < 560; x += 8)
            painter.drawRect(QRectF(x, 500, 2, 2));
        painter.drawRect(QRectF(36, 540, 200, 8));
        painter.drawRect(QRectF(36, 556, 180, 8));
        painter.drawRect(QRectF(36, 572, 160, 8));
    });

    const LabelRenderer::Result result = LabelRenderer::renderFile(path, {});
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.rotated, false);
    const Coverage pixels = cover(result.image);
    QCOMPARE(pixels.other, 0);
    QVERIFY2(double(pixels.black) / pixels.total > 0.75, qPrintable(QString::number(pixels.black)));
}

void LabelMakerTests::shippingTurnsALandscapeLabel()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("landscape.pdf");
    paintPdf(path, QPageSize(QPageSize::Letter), [](QPainter &painter) {
        painter.drawRect(QRectF(36, 72, 432, 288));
        painter.drawRect(QRectF(504, 120, 70, 8));
        painter.drawRect(QRectF(504, 136, 60, 8));
    });

    const LabelRenderer::Result result = LabelRenderer::renderFile(path, {});
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.rotated, true);
    const Coverage pixels = cover(result.image);
    QCOMPARE(pixels.other, 0);
    QVERIFY2(double(pixels.black) / pixels.total > 0.75, qPrintable(QString::number(pixels.black)));
}

void LabelMakerTests::shippingKeepsANativeLabel()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("native.pdf");
    const QPageSize page(QSizeF(4, 6), QPageSize::Inch, QString(), QPageSize::ExactMatch);
    paintPdf(path, page, [](QPainter &painter) {
        painter.drawRect(QRectF(108, 180, 72, 72));
    });

    const LabelRenderer::Result result = LabelRenderer::renderFile(path, {});
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.rotated, false);
    const Coverage pixels = cover(result.image);
    QCOMPARE(pixels.other, 0);
    QVERIFY2(double(pixels.black) / pixels.total < 0.2, qPrintable(QString::number(pixels.black)));
    QVERIFY(pixels.box.width() < result.image.width() / 2);
    QVERIFY(std::abs(pixels.box.width() - pixels.box.height()) < pixels.box.width() / 5);
}

void LabelMakerTests::blankPageIsAnError()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("blank.pdf");
    writeLetter(path, QRectF());
    const LabelRenderer::Result result = LabelRenderer::renderFile(path, {});
    QVERIFY(!result.ok());
    QCOMPARE(result.error, QStringLiteral("This page looks blank."));
}

void LabelMakerTests::secondPageCanBeBlank()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("two.pdf");
    writeLetter(path, QRectF(36, 72, 288, 432), true);

    LabelRenderer::Request request;
    const LabelRenderer::Result first = LabelRenderer::renderFile(path, request);
    QVERIFY(first.ok());
    QCOMPARE(first.pageCount, 2);

    request.pageIndex = 1;
    const LabelRenderer::Result second = LabelRenderer::renderFile(path, request);
    QVERIFY(!second.ok());
    QCOMPARE(second.pageCount, 2);

    request.pageIndex = 4;
    const LabelRenderer::Result missing = LabelRenderer::renderFile(path, request);
    QVERIFY(!missing.ok());
    QCOMPARE(missing.pageCount, 2);
}

void LabelMakerTests::thresholdIsOnlyBlackOrWhite()
{
    QTemporaryDir dir;
    QImage source(80, 40, QImage::Format_RGB32);
    for (int y = 0; y < source.height(); ++y) {
        for (int x = 0; x < source.width(); ++x) {
            int gray = 200;
            if (y < 20)
                gray = x < 40 ? 100 : 159;
            else
                gray = x < 40 ? 160 : 220;
            source.setPixel(x, y, qRgb(gray, gray, gray));
        }
    }
    const QString path = dir.filePath("grays.png");
    QVERIFY(source.save(path));

    LabelRenderer::Request request;
    request.fit = LabelRenderer::Fit::Page;
    // Pinned so this checks the threshold, not the turn.
    request.turn = LabelRenderer::Turn::Upright;
    const LabelRenderer::Result result = LabelRenderer::renderFile(path, request);
    QVERIFY2(result.ok(), qPrintable(result.error));
    const Coverage pixels = cover(result.image);
    QCOMPARE(pixels.other, 0);
    QCOMPARE(pixels.box.size(), QSize(160, 40));
    QVERIFY(pixels.black > 1000);

    int blackUnder = 0;
    const int y = pixels.box.bottom() + 1;
    for (int x = pixels.box.left(); x <= pixels.box.right(); ++x) {
        if (result.image.pixel(x, y) == qRgb(0, 0, 0))
            ++blackUnder;
    }
    QCOMPARE(blackUnder, 0);
}

void LabelMakerTests::pictureDoesNotUpscalePastTwo()
{
    QTemporaryDir dir;
    QImage source(100, 80, QImage::Format_RGB32);
    source.fill(Qt::black);
    const QString path = dir.filePath("small.png");
    QVERIFY(source.save(path));

    LabelRenderer::Request request;
    request.fit = LabelRenderer::Fit::Page;
    // Pinned so this checks the 2x cap, not the turn.
    request.turn = LabelRenderer::Turn::Upright;
    const LabelRenderer::Result result = LabelRenderer::renderFile(path, request);
    QVERIFY(result.ok());
    const Coverage pixels = cover(result.image);
    QCOMPARE(pixels.other, 0);
    QCOMPARE(pixels.box.size(), QSize(200, 160));
}

void LabelMakerTests::widePictureTurnsInFitAndFill()
{
    QTemporaryDir dir;
    QImage wide(360, 90, QImage::Format_RGB32);
    wide.fill(Qt::black);
    const QString widePath = dir.filePath("wide.png");
    QVERIFY(wide.save(widePath));

    // Square canvas, wide mark, clear padding stored as black. Same shape
    // as a wordmark exported on a transparent square.
    QImage square(360, 360, QImage::Format_ARGB32);
    square.fill(qRgba(0, 0, 0, 0));
    for (int y = 150; y < 210; ++y) {
        for (int x = 30; x < 330; ++x)
            square.setPixel(x, y, qRgba(0, 0, 0, 255));
    }
    const QString squarePath = dir.filePath("square.png");
    QVERIFY(square.save(squarePath));

    const LabelRenderer::Fit fits[] = {LabelRenderer::Fit::Page, LabelRenderer::Fit::Fill};
    const QString paths[] = {widePath, squarePath};
    for (const QString &path : paths) {
        for (LabelRenderer::Fit fit : fits) {
            LabelRenderer::Request request;
            request.fit = fit;
            const LabelRenderer::Result result = LabelRenderer::renderFile(path, request);
            QVERIFY2(result.ok(), qPrintable(result.error));
            QCOMPARE(result.image.size(), QSize(808, 1218));
            QVERIFY2(result.rotated, qPrintable(path));
            QVERIFY(!result.blank);
            const Coverage pixels = cover(result.image);
            QCOMPARE(pixels.other, 0);
            QVERIFY(pixels.black > 1000);
            QCOMPARE(result.image.pixel(0, 0), qRgb(255, 255, 255));
        }
    }
}

void LabelMakerTests::turnOverride()
{
    QTemporaryDir dir;
    QImage wide(360, 90, QImage::Format_RGB32);
    wide.fill(Qt::black);
    const QString widePath = dir.filePath("wide.png");
    QVERIFY(wide.save(widePath));
    QImage tall(90, 360, QImage::Format_RGB32);
    tall.fill(Qt::black);
    const QString tallPath = dir.filePath("tall.png");
    QVERIFY(tall.save(tallPath));

    LabelRenderer::Request upright;
    upright.fit = LabelRenderer::Fit::Page;
    upright.turn = LabelRenderer::Turn::Upright;
    const LabelRenderer::Result stayed = LabelRenderer::renderFile(widePath, upright);
    QVERIFY(stayed.ok());
    QVERIFY(!stayed.rotated);

    upright.fit = LabelRenderer::Fit::Fill;
    const LabelRenderer::Result stayedFill = LabelRenderer::renderFile(widePath, upright);
    QVERIFY(stayedFill.ok());
    QVERIFY(!stayedFill.rotated);

    LabelRenderer::Request sideways;
    sideways.fit = LabelRenderer::Fit::Page;
    sideways.turn = LabelRenderer::Turn::Sideways;
    const LabelRenderer::Result forced = LabelRenderer::renderFile(tallPath, sideways);
    QVERIFY(forced.ok());
    QVERIFY(forced.rotated);

    LabelRenderer::Request automatic;
    automatic.fit = LabelRenderer::Fit::Page;
    const LabelRenderer::Result natural = LabelRenderer::renderFile(tallPath, automatic);
    QVERIFY(natural.ok());
    QVERIFY(!natural.rotated);
}

void LabelMakerTests::fillStaysCenteredOnTheInk()
{
    QTemporaryDir dir;
    QImage source(420, 220, QImage::Format_RGB32);
    source.fill(Qt::white);
    for (int y = 30; y < 70; ++y) {
        for (int x = 15; x < 215; ++x)
            source.setPixel(x, y, qRgb(0, 0, 0));
    }
    const QString path = dir.filePath("offcenter.png");
    QVERIFY(source.save(path));

    const LabelRenderer::Turn turns[] = {LabelRenderer::Turn::Auto, LabelRenderer::Turn::Upright};
    for (LabelRenderer::Turn turn : turns) {
        LabelRenderer::Request request;
        request.fit = LabelRenderer::Fit::Fill;
        request.turn = turn;
        const LabelRenderer::Result result = LabelRenderer::renderFile(path, request);
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(result.rotated, turn == LabelRenderer::Turn::Auto);
        const Coverage pixels = cover(result.image);
        QCOMPARE(pixels.other, 0);
        QVERIFY(pixels.black > 1000);
        const int left = pixels.box.left();
        const int right = result.image.width() - pixels.box.right() - 1;
        const int top = pixels.box.top();
        const int bottom = result.image.height() - pixels.box.bottom() - 1;
        QVERIFY2(std::abs(left - right) <= 4, qPrintable(QString("%1 vs %2").arg(left).arg(right)));
        QVERIFY2(std::abs(top - bottom) <= 4, qPrintable(QString("%1 vs %2").arg(top).arg(bottom)));
    }
}

void LabelMakerTests::transparentPrintsWhite()
{
    QTemporaryDir dir;
    QImage source(80, 80, QImage::Format_ARGB32);
    source.fill(qRgba(0, 0, 0, 0));
    for (int y = 30; y < 50; ++y) {
        for (int x = 30; x < 50; ++x)
            source.setPixel(x, y, qRgba(0, 0, 0, 255));
    }
    const QString path = dir.filePath("clear.png");
    QVERIFY(source.save(path));

    LabelRenderer::Request request;
    request.fit = LabelRenderer::Fit::Page;
    request.turn = LabelRenderer::Turn::Upright;
    const LabelRenderer::Result result = LabelRenderer::renderFile(path, request);
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.image.pixel(0, 0), qRgb(255, 255, 255));
    QCOMPARE(result.image.pixel(result.image.width() - 1, result.image.height() - 1), qRgb(255, 255, 255));
    const Coverage pixels = cover(result.image);
    QCOMPARE(pixels.other, 0);
    QVERIFY(pixels.black > 0);
    QVERIFY(pixels.black * 2 < pixels.total);
    QVERIFY(!result.blank);
}

void LabelMakerTests::whiteArtRaisesTheBlankHint()
{
    QTemporaryDir dir;
    QImage source(200, 80, QImage::Format_ARGB32);
    source.fill(qRgba(0, 0, 0, 0));
    for (int y = 20; y < 60; ++y) {
        for (int x = 20; x < 180; ++x)
            source.setPixel(x, y, qRgba(255, 255, 255, 255));
    }
    const QString path = dir.filePath("white.png");
    QVERIFY(source.save(path));

    const LabelRenderer::Fit fits[] = {LabelRenderer::Fit::Page, LabelRenderer::Fit::Fill};
    for (LabelRenderer::Fit fit : fits) {
        LabelRenderer::Request request;
        request.fit = fit;
        const LabelRenderer::Result result = LabelRenderer::renderFile(path, request);
        QVERIFY2(result.ok(), qPrintable(result.error));
        QVERIFY(result.blank);
        QCOMPARE(cover(result.image).black, 0);
    }

    LabelRenderer::Request shipping;
    shipping.fit = LabelRenderer::Fit::Shipping;
    const LabelRenderer::Result blankPage = LabelRenderer::renderFile(path, shipping);
    QVERIFY(!blankPage.ok());
}

void LabelMakerTests::turnUndoesAndResetsOnANewFile()
{
    QTemporaryDir dir;
    QImage black(40, 20, QImage::Format_RGB32);
    black.fill(Qt::black);
    QImage white(40, 20, QImage::Format_ARGB32);
    white.fill(qRgba(255, 255, 255, 255));
    const QString blackPath = dir.filePath("black.png");
    const QString whitePath = dir.filePath("white.png");
    QVERIFY(black.save(blackPath));
    QVERIFY(white.save(whitePath));

    Backend backend(nullptr);
    backend.openPath(blackPath);
    QTRY_VERIFY_WITH_TIMEOUT(backend.hasLabel(), 5000);
    QCOMPARE(backend.turn(), 0);
    QVERIFY(!backend.blank());

    backend.setTurn(2);
    QCOMPARE(backend.turn(), 2);
    backend.undo();
    QCOMPARE(backend.turn(), 0);
    backend.redo();
    QCOMPARE(backend.turn(), 2);

    backend.openPath(whitePath);
    QCOMPARE(backend.turn(), 0);
    QTRY_VERIFY_WITH_TIMEOUT(backend.blank(), 5000);
    QVERIFY(backend.hasLabel());

    backend.undo();
    QCOMPARE(backend.turn(), 2);
    QCOMPARE(backend.fileName(), QStringLiteral("black.png"));
    QTRY_VERIFY_WITH_TIMEOUT(!backend.blank(), 5000);
}

void LabelMakerTests::quarterTurnsOfAWideImage()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QImage source(960, 240, QImage::Format_RGB32);
    source.fill(Qt::black);
    for (int y = 20; y < 68; ++y) {
        for (int x = 20; x < 68; ++x)
            source.setPixel(x, y, qRgb(255, 255, 255));
    }
    const QString path = dir.filePath(QStringLiteral("wide.png"));
    QVERIFY(source.save(path));

    const LabelRenderer::Fit fits[] = {LabelRenderer::Fit::Shipping, LabelRenderer::Fit::Page,
                                       LabelRenderer::Fit::Fill};
    const int degrees[] = {0, 90, 180, 270};
    for (LabelRenderer::Fit fit : fits) {
        QSize uprightBox;
        QSize sidewaysBox;
        for (int deg : degrees) {
            LabelRenderer::Request request;
            request.fit = fit;
            request.absoluteDegrees = deg;
            const LabelRenderer::Result result = LabelRenderer::renderFile(path, request);
            QVERIFY2(result.ok(), qPrintable(result.error));
            QCOMPARE(result.image.size(), QSize(808, 1218));
            QCOMPARE(result.turnDegrees, deg);
            QCOMPARE(result.rotated, deg == 90 || deg == 270);

            const Coverage pixels = cover(result.image);
            QCOMPARE(pixels.other, 0);
            QVERIFY(pixels.black > 1000);
            const int left = pixels.box.left();
            const int right = result.image.width() - pixels.box.right() - 1;
            const int top = pixels.box.top();
            const int bottom = result.image.height() - pixels.box.bottom() - 1;
            QVERIFY2(std::abs(left - right) <= 2, qPrintable(QString("%1 vs %2").arg(left).arg(right)));
            QVERIFY2(std::abs(top - bottom) <= 2, qPrintable(QString("%1 vs %2").arg(top).arg(bottom)));

            const bool contain = fit != LabelRenderer::Fit::Fill;
            if (contain && (deg == 90 || deg == 270))
                QVERIFY2(pixels.box.height() >= 1216, qPrintable(QString::number(pixels.box.height())));
            if (contain && (deg == 0 || deg == 180))
                QVERIFY2(pixels.box.height() < 400, qPrintable(QString::number(pixels.box.height())));

            if (deg == 0)
                uprightBox = pixels.box.size();
            if (deg == 180)
                QCOMPARE(pixels.box.size(), uprightBox);
            if (deg == 90)
                sidewaysBox = pixels.box.size();
            if (deg == 270)
                QCOMPARE(pixels.box.size(), sidewaysBox);

            if (!contain)
                continue;

            const QImage turned = source.transformed(QTransform().rotate(deg), Qt::FastTransformation);
            const Dot from = whiteDot(turned, turned.rect());
            const Dot hole = whiteDot(result.image, pixels.box.adjusted(1, 1, -1, -1));
            QVERIFY2(from.count > 10, qPrintable(QString::number(deg)));
            QVERIFY2(hole.count > 10, qPrintable(QString::number(deg)));
            const double sx = from.x / turned.width();
            const double sy = from.y / turned.height();
            const double ox = (hole.x - pixels.box.left()) / pixels.box.width();
            const double oy = (hole.y - pixels.box.top()) / pixels.box.height();
            QVERIFY2(std::abs(sx - ox) < 0.06, qPrintable(QString("%1 vs %2 at %3").arg(sx).arg(ox).arg(deg)));
            QVERIFY2(std::abs(sy - oy) < 0.06, qPrintable(QString("%1 vs %2 at %3").arg(sy).arg(oy).arg(deg)));

            const double srcAspect = double(turned.width()) / turned.height();
            const double outAspect = double(pixels.box.width()) / pixels.box.height();
            QVERIFY2(std::abs(srcAspect - outAspect) / srcAspect < 0.04,
                     qPrintable(QString("%1 vs %2").arg(srcAspect).arg(outAspect)));
        }
    }

    LabelRenderer::Request extra;
    extra.fit = LabelRenderer::Fit::Page;
    extra.extraQuarters = 1;
    const LabelRenderer::Result stepped = LabelRenderer::renderFile(path, extra);
    QVERIFY(stepped.ok());
    QCOMPARE(stepped.turnDegrees, 180);

    extra.extraQuarters = 4;
    const LabelRenderer::Result wrapped = LabelRenderer::renderFile(path, extra);
    QVERIFY(wrapped.ok());
    QCOMPARE(wrapped.turnDegrees, 90);
}

void LabelMakerTests::portraitLabelAtZeroStaysPut()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QImage source(808, 1218, QImage::Format_RGB32);
    source.fill(Qt::white);
    for (int y = 0; y < source.height(); ++y) {
        for (int x = 0; x < source.width(); ++x) {
            if (x < 24 || y < 24 || x >= source.width() - 24 || y >= source.height() - 24)
                source.setPixel(x, y, qRgb(0, 0, 0));
        }
    }
    for (int y = 40; y < 110; ++y) {
        for (int x = 40; x < 180; ++x)
            source.setPixel(x, y, qRgb(0, 0, 0));
    }
    const QString path = dir.filePath(QStringLiteral("label.png"));
    QVERIFY(source.save(path));
    const QImage upside = source.transformed(QTransform().rotate(180), Qt::FastTransformation);

    const LabelRenderer::Fit fits[] = {LabelRenderer::Fit::Shipping, LabelRenderer::Fit::Page,
                                       LabelRenderer::Fit::Fill};
    for (LabelRenderer::Fit fit : fits) {
        LabelRenderer::Request request;
        request.fit = fit;
        request.absoluteDegrees = 0;
        const LabelRenderer::Result same = LabelRenderer::renderFile(path, request);
        QVERIFY2(same.ok(), qPrintable(same.error));
        QCOMPARE(same.turnDegrees, 0);
        QVERIFY2(!same.rotated, qPrintable(QString::number(int(fit))));
        QVERIFY2(samePixels(same.image, source), qPrintable(QString::number(int(fit))));

        request.absoluteDegrees = 180;
        const LabelRenderer::Result flipped = LabelRenderer::renderFile(path, request);
        QVERIFY2(flipped.ok(), qPrintable(flipped.error));
        QCOMPARE(flipped.turnDegrees, 180);
        QVERIFY(!flipped.rotated);
        QCOMPARE(flipped.image.size(), source.size());
        QVERIFY2(samePixels(flipped.image, upside), qPrintable(QString::number(int(fit))));
    }
}

void LabelMakerTests::rotateFourTimesReturnsToAuto()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QImage wide(960, 240, QImage::Format_RGB32);
    wide.fill(Qt::black);
    const QString widePath = dir.filePath(QStringLiteral("wide.png"));
    QVERIFY(wide.save(widePath));

    Backend backend(nullptr);
    backend.openPath(widePath);
    QTRY_VERIFY_WITH_TIMEOUT(backend.hasLabel(), 5000);
    QCOMPARE(backend.turn(), 0);
    const QImage start = backend.preview();

    for (int i = 0; i < 4; ++i) {
        const int revision = backend.revision();
        backend.rotate();
        QTRY_VERIFY_WITH_TIMEOUT(backend.revision() > revision, 5000);
        QCOMPARE(backend.turn(), (i + 1) % 4);
        if (i == 0)
            QVERIFY(backend.preview() != start);
    }
    QCOMPARE(backend.turn(), 0);
    QVERIFY(samePixels(backend.preview(), start));

    const int revision = backend.revision();
    backend.rotate();
    QTRY_VERIFY_WITH_TIMEOUT(backend.revision() > revision, 5000);
    QCOMPARE(backend.turn(), 1);
    backend.undo();
    QCOMPARE(backend.turn(), 0);

    const QString pdf = dir.filePath(QStringLiteral("two.pdf"));
    writeLetter(pdf, QRectF(36, 72, 288, 432), true);
    backend.openPath(pdf);
    QCOMPARE(backend.turn(), 0);
    backend.rotate();
    QCOMPARE(backend.turn(), 1);
    backend.setPage(2);
    QCOMPARE(backend.turn(), 0);
}

void LabelMakerTests::photoModeDithers()
{
    QTemporaryDir dir;
    QImage source(64, 64, QImage::Format_RGB32);
    source.fill(qRgb(128, 128, 128));
    const QString path = dir.filePath("gray.png");
    QVERIFY(source.save(path));

    LabelRenderer::Request hard;
    hard.fit = LabelRenderer::Fit::Page;
    const LabelRenderer::Result solid = LabelRenderer::renderFile(path, hard);
    QVERIFY(solid.ok());
    const Coverage solidPixels = cover(solid.image);
    QCOMPARE(solidPixels.other, 0);
    QCOMPARE(solidPixels.box.width(), 128);
    QCOMPARE(solidPixels.black, solidPixels.box.width() * solidPixels.box.height());

    LabelRenderer::Request dithered = hard;
    dithered.photo = true;
    const LabelRenderer::Result mixed = LabelRenderer::renderFile(path, dithered);
    QVERIFY(mixed.ok());
    const Coverage mixedPixels = cover(mixed.image);
    QCOMPARE(mixedPixels.other, 0);
    QVERIFY(mixedPixels.black > 0);
    const int whiteInside = mixedPixels.box.width() * mixedPixels.box.height() - mixedPixels.black;
    QVERIFY2(whiteInside > 0, "dither should leave some white dots inside the photo");
}

void LabelMakerTests::encoderHeaderAndByteCount()
{
    QImage image(8, 2, QImage::Format_RGB32);
    image.fill(Qt::white);
    image.setPixel(0, 0, qRgb(0, 0, 0));
    image.setPixel(1, 0, qRgb(0, 0, 0));

    TsplOptions options;
    options.widthMm = 101.6;
    options.heightMm = 152.4;
    options.density = 10;
    options.copies = 2;
    const QByteArray job = TsplJob::encode(image, options);
    const QByteArray header = QByteArrayLiteral(
        "\r\nSIZE 101.6 mm,152.4 mm\r\nGAP 3 mm,0 mm\r\nDIRECTION 1\r\nDENSITY 10\r\nCLS\r\nBITMAP 0,0,1,2,0,");
    QVERIFY(job.startsWith(header));
    QCOMPARE(job.mid(header.size(), 2), QByteArray::fromHex("3fff"));
    QVERIFY(job.endsWith(QByteArrayLiteral("\r\nPRINT 2\r\n")));
    QCOMPARE(job.size(), header.size() + 2 + int(strlen("\r\nPRINT 2\r\n")));

    QImage wide(16, 4, QImage::Format_RGB32);
    wide.fill(Qt::black);
    const QByteArray wideJob = TsplJob::encode(wide, options);
    const int payloadAt = wideJob.indexOf(QByteArrayLiteral("BITMAP 0,0,2,4,0,"));
    QVERIFY(payloadAt > 0);
    const int dataAt = payloadAt + int(strlen("BITMAP 0,0,2,4,0,"));
    QCOMPARE(wideJob.mid(dataAt, 8).size(), 8);
    QVERIFY(wideJob.mid(dataAt + 8).startsWith(QByteArrayLiteral("\r\nPRINT ")));
}

void LabelMakerTests::polaritySwitchInvertsBytes()
{
    QImage image(16, 3, QImage::Format_RGB32);
    image.fill(Qt::white);
    for (int x = 0; x < 10; ++x)
        image.setPixel(x, 1, qRgb(0, 0, 0));

    TsplOptions options;
    options.blackIsZero = true;
    const QByteArray normal = TsplJob::bitmapBytes(image, true);
    const QByteArray flipped = TsplJob::bitmapBytes(image, false);
    QCOMPARE(normal.size(), 2 * 3);
    QCOMPARE(flipped.size(), normal.size());
    for (int i = 0; i < normal.size(); ++i)
        QCOMPARE(static_cast<uchar>(normal.at(i)) ^ static_cast<uchar>(flipped.at(i)), uchar(0xff));

    options.blackIsZero = false;
    options.density = 15;
    const QByteArray job = TsplJob::encode(image, options);
    QVERIFY(job.contains(QByteArrayLiteral("DENSITY 15\r\n")));
    QVERIFY(job.contains(flipped));
}

void LabelMakerTests::themeFallsBack()
{
    QTemporaryDir dir;
    Theme theme(dir.path());
    QCOMPARE(theme.accent(), QString("#FFD60A"));
    QCOMPARE(theme.accentForeground(), QString("black"));
}

void LabelMakerTests::themeReadsAccent()
{
    QTemporaryDir dir;
    QDir(dir.path()).mkpath(QStringLiteral("theme"));
    writeFile(dir.path() + QStringLiteral("/theme/colors.toml"), "accent = \"#112233\"\n");

    Theme theme(dir.path());
    QCOMPARE(theme.accent(), QString("#112233"));
    QCOMPARE(theme.accentForeground(), QString("white"));
}

void LabelMakerTests::themeFollowsHyprlandRounding()
{
    QTemporaryDir dir;
    const QString response = dir.filePath(QStringLiteral("response.json"));
    const QString command = dir.filePath(QStringLiteral("hyprctl"));
    writeFile(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n");
    QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    writeFile(response, "{\"int\":0}");

    Theme theme(dir.path(), nullptr, command);
    QCOMPARE(theme.radius(), 0);
    writeFile(response, "{\"int\":6}");
    QTRY_COMPARE(theme.radius(), 6);
}

void LabelMakerTests::translatorFallsBackToEnglish()
{
    QTemporaryDir dir;
    Translator translator(dir.path() + QStringLiteral("/language"), dir.path());
    QCOMPARE(translator.language(), QString("en"));
    QVERIFY(translator.translate("Main", "Save").isNull());
}

void LabelMakerTests::translatorFollowsTheLanguageFile()
{
    QTemporaryDir dir;
    writeFile(dir.path() + QStringLiteral("/language"), "pt-BR\n");
    writeFile(dir.path() + QStringLiteral("/pt.json"), R"({"Save": "Guardar", "Open": "Abrir"})");
    writeFile(dir.path() + QStringLiteral("/pt-BR.json"), R"({"Save": "Salvar"})");

    Translator translator(dir.path() + QStringLiteral("/language"), dir.path());
    QCOMPARE(translator.language(), QString("pt-BR"));
    QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
    QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
    QVERIFY(translator.translate("Main", "Quit").isNull());
}

void LabelMakerTests::translatorPicksPluralForms()
{
    QTemporaryDir dir;
    writeFile(dir.path() + QStringLiteral("/language"), "ru\n");
    writeFile(dir.path() + QStringLiteral("/en.json"),
              R"({"%n clips": {"one": "%n clip", "other": "%n clips"}})");
    writeFile(dir.path() + QStringLiteral("/ru.json"),
              R"({"%n clips": {"one": "%n клип", "few": "%n клипа", "many": "%n клипов", "other": "%n клипа"}})");

    Translator translator(dir.path() + QStringLiteral("/language"), dir.path());
    QCOMPARE(translator.translate("Main", "%n clips", nullptr, 21), QString("%n клип"));
    QCOMPARE(translator.translate("Main", "%n clips", nullptr, 22), QString("%n клипа"));
    QCOMPARE(translator.translate("Main", "%n clips", nullptr, 25), QString("%n клипов"));
    QCOMPARE(Translator::pluralCategory("ar", 11), QString("many"));
    QCOMPARE(Translator::pluralCategory("ja", 1), QString("other"));
}

void LabelMakerTests::windowLoads()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QDir(dir.path()).mkpath(QStringLiteral("theme"));
    writeFile(dir.path() + QStringLiteral("/theme/colors.toml"), "accent = \"#FFD60A\"\n");

    const QString pdf = dir.filePath(QStringLiteral("sample.pdf"));
    {
        QPdfWriter writer(pdf);
        writer.setPageLayout(QPageLayout(QPageSize(QPageSize::Letter), QPageLayout::Portrait,
                                         QMarginsF(0, 0, 0, 0)));
        writer.setResolution(72);
        QPainter painter(&writer);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::black);
        const int widths[] = {2, 1, 4, 1, 2, 6, 1, 3, 1, 5, 2, 1, 4};
        int x = 50;
        for (int width : widths) {
            painter.drawRect(QRectF(x, 110, width, 280));
            x += width + 3;
        }
        painter.drawRect(QRectF(50, 410, 140, 16));
        painter.end();
    }

    Theme theme(dir.path());
    Translator translator(dir.filePath(QStringLiteral("language")), QStringLiteral(":/i18n"));
    Backend backend(nullptr);
    backend.openPath(pdf);

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("label"), new LabelImageProvider(&backend));
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
    engine.rootContext()->setContextProperty(QStringLiteral("translator"), &translator);
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    QVERIFY2(!engine.rootObjects().isEmpty(), "Main.qml failed to load");

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    QTRY_VERIFY_WITH_TIMEOUT(backend.hasLabel(), 15000);

    const QString shot = qEnvironmentVariable("LABELMAKER_SHOT");
    if (shot.isEmpty())
        return;
    window->show();
    QTest::qWait(500);
    const QImage image = window->grabWindow();
    QVERIFY(!image.isNull());
    QVERIFY(image.width() > 100);
    QVERIFY(image.save(shot));
}

QTEST_MAIN(LabelMakerTests)
#include "labelmaker_tests.moc"
