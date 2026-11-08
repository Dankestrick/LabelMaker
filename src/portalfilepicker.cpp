#include "portalfilepicker.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QRandomGenerator>
#include <QStandardPaths>

namespace {

struct PortalFilterRule {
    uint type = 0;
    QString pattern;
};

using PortalFilterRules = QList<PortalFilterRule>;

struct PortalFileFilter {
    QString name;
    PortalFilterRules rules;
};

using PortalFileFilters = QList<PortalFileFilter>;

QDBusArgument &operator<<(QDBusArgument &argument, const PortalFilterRule &rule)
{
    argument.beginStructure();
    argument << rule.type << rule.pattern;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, PortalFilterRule &rule)
{
    argument.beginStructure();
    argument >> rule.type >> rule.pattern;
    argument.endStructure();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const PortalFileFilter &filter)
{
    argument.beginStructure();
    argument << filter.name << filter.rules;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, PortalFileFilter &filter)
{
    argument.beginStructure();
    argument >> filter.name >> filter.rules;
    argument.endStructure();
    return argument;
}

void registerPortalTypes()
{
    static const bool registered = [] {
        qDBusRegisterMetaType<PortalFilterRule>();
        qDBusRegisterMetaType<PortalFilterRules>();
        qDBusRegisterMetaType<PortalFileFilter>();
        qDBusRegisterMetaType<PortalFileFilters>();
        return true;
    }();
    Q_UNUSED(registered);
}

PortalFileFilter labelFilter()
{
    return {
        QStringLiteral("PDF and pictures"),
        {
            {0, QStringLiteral("*.pdf")},
            {0, QStringLiteral("*.png")},
            {0, QStringLiteral("*.jpg")},
            {0, QStringLiteral("*.jpeg")},
            {0, QStringLiteral("*.webp")},
            {0, QStringLiteral("*.bmp")},
        },
    };
}

QByteArray folderBytes(const QString &path)
{
    QByteArray bytes = path.toUtf8();
    bytes.append('\0');
    return bytes;
}

QString startFolder()
{
    const QString downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    return QDir(downloads).exists() ? downloads : QDir::homePath();
}

} // namespace

PortalFilePicker::PortalFilePicker(QObject *parent) : FilePicker(parent)
{
    registerPortalTypes();
}

void PortalFilePicker::clearPending()
{
    if (m_watcher) {
        m_watcher->deleteLater();
        m_watcher = nullptr;
    }
    if (!m_path.isEmpty()) {
        QDBusConnection::sessionBus().disconnect(
            QStringLiteral("org.freedesktop.portal.Desktop"), m_path,
            QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
            this, SLOT(handleResponse(uint,QVariantMap)));
    }
    m_path.clear();
    m_busy = false;
}

void PortalFilePicker::openLabel()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusInterface portal(QStringLiteral("org.freedesktop.portal.Desktop"),
                          QStringLiteral("/org/freedesktop/portal/desktop"),
                          QStringLiteral("org.freedesktop.portal.FileChooser"), bus);
    if (!portal.isValid()) {
        emit failed(tr("The file picker isn't available."));
        return;
    }

    if (m_busy)
        clearPending();

    const QString token = QStringLiteral("labelmaker_%1").arg(QRandomGenerator::global()->generate());
    QVariantMap options;
    options.insert(QStringLiteral("handle_token"), token);
    options.insert(QStringLiteral("accept_label"), tr("Open"));
    options.insert(QStringLiteral("modal"), true);
    options.insert(QStringLiteral("multiple"), false);
    options.insert(QStringLiteral("current_folder"), folderBytes(startFolder()));
    const PortalFileFilter filter = labelFilter();
    options.insert(QStringLiteral("filters"), QVariant::fromValue(PortalFileFilters{filter}));
    options.insert(QStringLiteral("current_filter"), QVariant::fromValue(filter));

    QString sender = bus.baseService().mid(1);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    const QString predicted = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
    m_path = predicted;
    m_busy = true;
    if (!bus.connect(QStringLiteral("org.freedesktop.portal.Desktop"), predicted,
                     QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
                     this, SLOT(handleResponse(uint,QVariantMap)))) {
        clearPending();
        emit failed(tr("Couldn't open the file picker."));
        return;
    }

    auto *watcher = new QDBusPendingCallWatcher(
        portal.asyncCall(QStringLiteral("OpenFile"), QString(), tr("Open"), options), this);
    m_watcher = watcher;
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, predicted] {
        QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        watcher->deleteLater();
        if (m_watcher == watcher)
            m_watcher = nullptr;
        if (!m_busy)
            return;
        if (reply.isError()) {
            clearPending();
            emit failed(tr("The file picker failed: %1").arg(reply.error().message()));
            return;
        }
        const QString actual = reply.value().path();
        if (actual == predicted)
            return;
        QDBusConnection::sessionBus().disconnect(
            QStringLiteral("org.freedesktop.portal.Desktop"), predicted,
            QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
            this, SLOT(handleResponse(uint,QVariantMap)));
        m_path = actual;
        if (!QDBusConnection::sessionBus().connect(
                QStringLiteral("org.freedesktop.portal.Desktop"), actual,
                QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
                this, SLOT(handleResponse(uint,QVariantMap)))) {
            clearPending();
            emit failed(tr("Couldn't open the file picker."));
        }
    });
}

void PortalFilePicker::handleResponse(uint response, const QVariantMap &results)
{
    clearPending();
    if (response != 0)
        return;
    const QStringList uris = results.value(QStringLiteral("uris")).toStringList();
    if (uris.isEmpty())
        return;
    emit openSelected(QUrl(uris.first()));
}
