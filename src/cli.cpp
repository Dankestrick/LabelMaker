#include "cli.h"

#include "labelrenderer.h"
#include "labelsize.h"
#include "tspljob.h"
#include "printerlink.h"

#include <QEventLoop>

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextStream>

namespace {

void printHelp(QTextStream &out)
{
    out << "label-maker open [file]\n"
           "label-maker --render <file> --size 4x6 [--page N] [--fit shipping|page|fill]\n"
           "            [--turn auto|upright|sideways] [--photo] [--darkness N] [--copies N]\n"
           "            --out preview.png [--tspl job.bin] [--print] [--json]\n";
}

bool writeAtomic(const QString &path, const QByteArray &bytes, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = QCoreApplication::translate("Cli", "Couldn't write %1.").arg(path);
        return false;
    }
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        *error = QCoreApplication::translate("Cli", "Couldn't write %1.").arg(path);
        return false;
    }
    return true;
}

void fail(const QString &message, bool json)
{
    QTextStream err(stderr);
    err << message << "\n";
    if (json) {
        QJsonObject object;
        object.insert(QStringLiteral("ok"), false);
        object.insert(QStringLiteral("error"), message);
        QTextStream(stdout) << QJsonDocument(object).toJson(QJsonDocument::Compact) << "\n";
    }
}

} // namespace

int runCli()
{
    const QStringList args = QCoreApplication::arguments();
    QTextStream out(stdout);
    if (args.size() < 2 || args.contains(QStringLiteral("--help")) || args.contains(QStringLiteral("-h"))) {
        printHelp(out);
        return args.size() < 2 ? 1 : 0;
    }
    if (!args.contains(QStringLiteral("--render"))) {
        fail(QCoreApplication::translate("Cli", "Use label-maker open for the window, or --render for a file."), false);
        QTextStream err(stderr);
        printHelp(err);
        return 1;
    }

    QString input;
    QString sizeText = QStringLiteral("4x6");
    QString outPath;
    QString tsplPath;
    QString fitText = QStringLiteral("shipping");
    QString turnText = QStringLiteral("auto");
    int page = 1;
    int darkness = 10;
    int copies = 1;
    bool photo = false;
    bool json = false;

    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        auto need = [&](QString *value) {
            if (i + 1 >= args.size())
                return false;
            *value = args.at(++i);
            return true;
        };
        if (arg == QLatin1String("--render")) {
            if (!need(&input)) {
                fail(QCoreApplication::translate("Cli", "Missing the file after --render."), json);
                return 1;
            }
        } else if (arg == QLatin1String("--size")) {
            if (!need(&sizeText)) {
                fail(QCoreApplication::translate("Cli", "Missing the size after --size."), json);
                return 1;
            }
        } else if (arg == QLatin1String("--page")) {
            QString text;
            if (!need(&text)) {
                fail(QCoreApplication::translate("Cli", "Missing the page after --page."), json);
                return 1;
            }
            bool ok = false;
            page = text.toInt(&ok);
            if (!ok || page < 1) {
                fail(QCoreApplication::translate("Cli", "The page has to be 1 or more."), json);
                return 1;
            }
        } else if (arg == QLatin1String("--fit")) {
            if (!need(&fitText)) {
                fail(QCoreApplication::translate("Cli", "Missing the fit after --fit."), json);
                return 1;
            }
        } else if (arg == QLatin1String("--turn")) {
            if (!need(&turnText)) {
                fail(QCoreApplication::translate("Cli", "Missing the turn after --turn."), json);
                return 1;
            }
        } else if (arg == QLatin1String("--out")) {
            if (!need(&outPath)) {
                fail(QCoreApplication::translate("Cli", "Missing the path after --out."), json);
                return 1;
            }
        } else if (arg == QLatin1String("--tspl")) {
            if (!need(&tsplPath)) {
                fail(QCoreApplication::translate("Cli", "Missing the path after --tspl."), json);
                return 1;
            }
        } else if (arg == QLatin1String("--darkness")) {
            QString text;
            if (!need(&text)) {
                fail(QCoreApplication::translate("Cli", "Missing the darkness after --darkness."), json);
                return 1;
            }
            bool ok = false;
            darkness = text.toInt(&ok);
            if (!ok) {
                fail(QCoreApplication::translate("Cli", "Darkness has to be a number from 1 to 15."), json);
                return 1;
            }
        } else if (arg == QLatin1String("--copies")) {
            QString text;
            if (!need(&text)) {
                fail(QCoreApplication::translate("Cli", "Missing the copies after --copies."), json);
                return 1;
            }
            bool ok = false;
            copies = text.toInt(&ok);
            if (!ok || copies < 1) {
                fail(QCoreApplication::translate("Cli", "Copies has to be 1 or more."), json);
                return 1;
            }
        } else if (arg == QLatin1String("--photo")) {
            photo = true;
        } else if (arg == QLatin1String("--json")) {
            json = true;
        } else if (arg == QLatin1String("--print")) {
            // read below
        } else {
            fail(QCoreApplication::translate("Cli", "Unknown option %1.").arg(arg), json);
            return 1;
        }
    }

    const bool sendToPrinter = args.contains(QStringLiteral("--print"));
    if (input.isEmpty() || (outPath.isEmpty() && tsplPath.isEmpty() && !sendToPrinter)) {
        fail(QCoreApplication::translate("Cli", "Pass --render and at least one of --out, --tspl or --print."), json);
        return 1;
    }

    double widthMm = 0;
    double heightMm = 0;
    if (!parseLabelSize(sizeText, &widthMm, &heightMm)) {
        fail(QCoreApplication::translate("Cli", "Unknown label size %1.").arg(sizeText), json);
        return 1;
    }

    LabelRenderer::Request request;
    request.widthMm = widthMm;
    request.heightMm = heightMm;
    request.pageIndex = page - 1;
    request.photo = photo;
    if (fitText == QLatin1String("shipping"))
        request.fit = LabelRenderer::Fit::Shipping;
    else if (fitText == QLatin1String("page"))
        request.fit = LabelRenderer::Fit::Page;
    else if (fitText == QLatin1String("fill"))
        request.fit = LabelRenderer::Fit::Fill;
    else {
        fail(QCoreApplication::translate("Cli", "Fit has to be shipping, page, or fill."), json);
        return 1;
    }
    if (turnText == QLatin1String("auto"))
        request.turn = LabelRenderer::Turn::Auto;
    else if (turnText == QLatin1String("upright"))
        request.turn = LabelRenderer::Turn::Upright;
    else if (turnText == QLatin1String("sideways"))
        request.turn = LabelRenderer::Turn::Sideways;
    else {
        fail(QCoreApplication::translate("Cli", "Turn has to be auto, upright, or sideways."), json);
        return 1;
    }

    const LabelRenderer::Result result = LabelRenderer::renderFile(input, request);
    if (!result.ok()) {
        fail(result.error, json);
        return 1;
    }

    QString error;
    if (!outPath.isEmpty()) {
        QSaveFile file(outPath);
        if (!file.open(QIODevice::WriteOnly) || !result.image.save(&file, "PNG") || !file.commit()) {
            fail(QCoreApplication::translate("Cli", "Couldn't write %1.").arg(outPath), json);
            return 1;
        }
    }
    if (!tsplPath.isEmpty()) {
        TsplOptions options;
        options.widthMm = widthMm;
        options.heightMm = heightMm;
        options.density = darkness;
        options.copies = copies;
        const QByteArray job = TsplJob::encode(result.image, options);
        if (job.isEmpty() || !writeAtomic(tsplPath, job, &error)) {
            fail(error.isEmpty() ? QCoreApplication::translate("Cli", "Couldn't build the print job.") : error, json);
            return 1;
        }
    }

    if (sendToPrinter) {
        TsplOptions options;
        options.widthMm = widthMm;
        options.heightMm = heightMm;
        options.density = darkness;
        options.copies = copies;
        PrinterLink link;
        QEventLoop loop;
        bool printed = false;
        QObject::connect(&link, &PrinterLink::progress, [](const QString &message, double fraction) {
            QTextStream(stderr) << message << " " << int(fraction * 100) << "%\n";
        });
        QObject::connect(&link, &PrinterLink::finished, [&](bool ok, const QString &message) {
            printed = ok;
            QTextStream(stderr) << message << "\n";
            loop.quit();
        });
        link.print(TsplJob::encodeBody(result.image, options), copies);
        loop.exec();
        if (!printed)
            return 1;
    }

    if (json) {
        QJsonObject object;
        object.insert(QStringLiteral("ok"), true);
        object.insert(QStringLiteral("width"), result.image.width());
        object.insert(QStringLiteral("height"), result.image.height());
        object.insert(QStringLiteral("pages"), result.pageCount);
        object.insert(QStringLiteral("rotated"), result.rotated);
        object.insert(QStringLiteral("blank"), result.blank);
        QTextStream(stdout) << QJsonDocument(object).toJson(QJsonDocument::Compact) << "\n";
    }
    return 0;
}
