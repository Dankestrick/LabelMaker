// Label Maker prints a PDF or a picture on a D450 label.

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>

#include "backend.h"
#include "cli.h"
#include "labelimageprovider.h"
#include "portalfilepicker.h"
#include "theme.h"
#include "translator.h"

namespace {

bool wantsCli(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--render") == 0 || qstrcmp(argv[i], "--help") == 0 || qstrcmp(argv[i], "-h") == 0)
            return true;
    }
    return false;
}

QString fileFromArguments(const QStringList &arguments)
{
    if (arguments.size() < 2 || arguments.at(1).startsWith(QLatin1Char('-')))
        return {};
    if (arguments.at(1) == QLatin1String("open"))
        return arguments.size() >= 3 ? arguments.at(2) : QString();
    return arguments.at(1);
}

} // namespace

int main(int argc, char *argv[])
{
    const bool headless = wantsCli(argc, argv);
    if (headless)
        qputenv("QT_QPA_PLATFORM", "offscreen");

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("label-maker"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    app.setOrganizationName(QStringLiteral("label-maker"));

    // Matches the window to label-maker.desktop, so the compositor and the
    // launcher pick up the installed icon.
    app.setDesktopFileName(QStringLiteral("label-maker"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("label-maker")));

    QQuickStyle::setStyle(QStringLiteral("Material"));

    Translator translator;
    app.installTranslator(&translator);

    if (headless)
        return runCli();

    Theme theme;
    PortalFilePicker picker;
    Backend backend(&picker);

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.addImageProvider(QStringLiteral("label"), new LabelImageProvider(&backend));
    engine.rootContext()->setContextProperty(QStringLiteral("translator"), &translator);
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    const QString initial = fileFromArguments(app.arguments());
    if (!initial.isEmpty())
        backend.openPath(initial);

    return app.exec();
}
