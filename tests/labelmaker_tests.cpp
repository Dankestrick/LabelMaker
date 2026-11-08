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
    const LabelRenderer::Result result = LabelRenderer::renderFile(path, request);
    QVERIFY(result.ok());
    const Coverage pixels = cover(result.image);
    QCOMPARE(pixels.other, 0);
    QCOMPARE(pixels.box.size(), QSize(200, 160));
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
