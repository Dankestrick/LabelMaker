#include "printerlink.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QMap>

namespace {

const QString Bluez = QStringLiteral("org.bluez");
const QString Adapter = QStringLiteral("/org/bluez/hci0");
const QString DeviceIface = QStringLiteral("org.bluez.Device1");
const QString CharIface = QStringLiteral("org.bluez.GattCharacteristic1");
const QString PropsIface = QStringLiteral("org.freedesktop.DBus.Properties");

// The D450 by name; or by address.
const QString PrinterName = QStringLiteral("D450");
const QString PrinterAddress = QStringLiteral("00:00:00:00:00:00");
const QString TxUuid = QStringLiteral("49535343-8841-43f4-a8d4-ecbe34729bb3");
const QString RxUuid = QStringLiteral("49535343-1e4d-4bd9-ba61-23c647249616");

const int TickMs = 500;
const int FindTicks = 30;       // 15 s of scanning
const int ServiceTicks = 30;    // 15 s for the services to show up
const int CheckMs = 1000;       // time for an error report after the data
const int PrintTicks = 40;      // 20 s for the label to come out

using InterfaceMap = QMap<QString, QVariantMap>;
using ObjectMap = QMap<QDBusObjectPath, InterfaceMap>;

QDBusConnection bus() { return QDBusConnection::systemBus(); }

ObjectMap managedObjects()
{
    QDBusMessage call = QDBusMessage::createMethodCall(Bluez, QStringLiteral("/"),
        QStringLiteral("org.freedesktop.DBus.ObjectManager"), QStringLiteral("GetManagedObjects"));
    QDBusReply<ObjectMap> reply = bus().call(call, QDBus::Block, 5000);
    return reply.isValid() ? reply.value() : ObjectMap();
}

void adapterCall(const QString &method, const QVariantList &args = {})
{
    QDBusMessage call = QDBusMessage::createMethodCall(Bluez, Adapter, QStringLiteral("org.bluez.Adapter1"), method);
    call.setArguments(args);
    bus().call(call, QDBus::Block, 5000);
}

} // namespace

Q_DECLARE_METATYPE(InterfaceMap)
Q_DECLARE_METATYPE(ObjectMap)

PrinterLink::PrinterLink(QObject *parent)
    : QObject(parent)
{
    qDBusRegisterMetaType<InterfaceMap>();
    qDBusRegisterMetaType<ObjectMap>();
    m_timer.setSingleShot(true);
}

void PrinterLink::print(const QByteArray &body, int copies)
{
    if (busy() || body.isEmpty())
        return;
    m_body = body;
    m_copies = qMax(1, copies);
    m_attempt = 0;
    m_waits = 0;
    m_step = Finding;
    emit progress(tr("Looking for the printer..."), 0);
    findDevice();
}

// Finds the printer and its two characteristics. Returns true when the
// device is known; fills m_tx/m_rx once its services are resolved.
bool PrinterLink::scanObjects()
{
    const ObjectMap objects = managedObjects();
    m_device.clear();
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        const QVariantMap device = it.value().value(DeviceIface);
        if (device.isEmpty())
            continue;
        if (device.value(QStringLiteral("Address")).toString() == PrinterAddress
            || device.value(QStringLiteral("Name")).toString() == PrinterName) {
            m_device = it.key().path();
            break;
        }
    }
    if (m_device.isEmpty())
        return false;
    m_tx.clear();
    m_rx.clear();
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        if (!it.key().path().startsWith(m_device + QLatin1Char('/')))
            continue;
        const QVariantMap ch = it.value().value(CharIface);
        const QString uuid = ch.value(QStringLiteral("UUID")).toString().toLower();
        if (uuid == TxUuid) {
            m_tx = it.key().path();
            const int mtu = ch.value(QStringLiteral("MTU")).toInt();
            m_chunk = mtu > 23 ? mtu - 3 : 20;
        } else if (uuid == RxUuid) {
            m_rx = it.key().path();
        }
    }
    return true;
}

void PrinterLink::findDevice()
{
    if (scanObjects()) {
        if (m_discovering) {
            adapterCall(QStringLiteral("StopDiscovery"));
            m_discovering = false;
        }
        connectDevice();
        return;
    }
    if (!m_discovering) {
        // An unpaired LE printer drops out of BlueZ after a while; scan for it.
        QVariantMap filter { { QStringLiteral("Transport"), QStringLiteral("le") } };
        adapterCall(QStringLiteral("SetDiscoveryFilter"), { filter });
        adapterCall(QStringLiteral("StartDiscovery"));
        m_discovering = true;
        m_waits = 0;
    }
    if (++m_waits > FindTicks) {
        adapterCall(QStringLiteral("StopDiscovery"));
        m_discovering = false;
        done(false, tr("Couldn't find the printer. Is it on and close by?"));
        return;
    }
    m_timer.callOnTimeout(this, [this] { findDevice(); }, Qt::SingleShotConnection);
    m_timer.start(TickMs);
}

void PrinterLink::connectDevice()
{
    m_step = Connecting;
    emit progress(tr("Connecting to the printer..."), 0);
    QDBusMessage call = QDBusMessage::createMethodCall(Bluez, m_device, DeviceIface, QStringLiteral("Connect"));
    auto *watcher = new QDBusPendingCallWatcher(bus().asyncCall(call, 25000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (w->isError() && w->error().name() != QLatin1String("org.bluez.Error.AlreadyConnected")) {
            done(false, tr("Couldn't connect to the printer: %1").arg(w->error().message()));
            return;
        }
        m_waits = 0;
        waitForServices();
    });
}

void PrinterLink::waitForServices()
{
    scanObjects();
    if (!m_tx.isEmpty() && !m_rx.isEmpty()) {
        startSending();
        return;
    }
    if (++m_waits > ServiceTicks) {
        done(false, tr("The printer connected but didn't offer its print service."));
        return;
    }
    m_timer.callOnTimeout(this, [this] { waitForServices(); }, Qt::SingleShotConnection);
    m_timer.start(TickMs);
}

void PrinterLink::startSending()
{
    if (!m_notifying) {
        bus().connect(Bluez, m_rx, PropsIface, QStringLiteral("PropertiesChanged"), this,
            SLOT(onPropertiesChanged(QString, QVariantMap, QStringList)));
        QDBusMessage call = QDBusMessage::createMethodCall(Bluez, m_rx, CharIface, QStringLiteral("StartNotify"));
        bus().call(call, QDBus::Block, 5000);
        m_notifying = true;
    }
    ++m_attempt;
    m_sawError = false;
    m_offset = 0;
    m_packet = 0;
    m_step = Sending;
    sendNext();
}

void PrinterLink::write(const QByteArray &data, bool request, std::function<void(bool, QString)> then)
{
    QVariantMap options { { QStringLiteral("type"), request ? QStringLiteral("request") : QStringLiteral("command") } };
    QDBusMessage call = QDBusMessage::createMethodCall(Bluez, m_tx, CharIface, QStringLiteral("WriteValue"));
    call.setArguments({ data, options });
    auto *watcher = new QDBusPendingCallWatcher(bus().asyncCall(call, 10000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [then](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        then(!w->isError(), w->error().message());
    });
}

void PrinterLink::sendNext()
{
    if (m_offset >= m_body.size()) {
        // A confirmed line end makes sure everything before it has landed.
        write(QByteArrayLiteral("\r\n"), true, [this](bool ok, const QString &error) {
            if (!ok) {
                done(false, tr("The printer stopped listening: %1").arg(error));
                return;
            }
            m_step = Checking;
            m_timer.callOnTimeout(this, [this] { check(); }, Qt::SingleShotConnection);
            m_timer.start(CheckMs);
        });
        return;
    }
    const QByteArray piece = m_body.mid(m_offset, m_chunk);
    const bool request = (m_packet % 2) == 1;
    write(piece, request, [this, piece](bool ok, const QString &error) {
        if (!ok) {
            done(false, tr("The printer stopped listening: %1").arg(error));
            return;
        }
        m_offset += piece.size();
        ++m_packet;
        if (m_packet % 20 == 0)
            emit progress(tr("Sending the label..."), double(m_offset) / m_body.size());
        sendNext();
    });
}

void PrinterLink::check()
{
    if (!m_sawError) {
        sendPrint();
        return;
    }
    if (m_attempt >= 2) {
        write(QByteArrayLiteral("\r\nCLS\r\n"), true, [this](bool, const QString &) {
            done(false, tr("The label didn't reach the printer cleanly twice. Nothing was printed."));
        });
        return;
    }
    emit progress(tr("The label got garbled on the way. Sending it again..."), 0);
    write(QByteArrayLiteral("\r\nCLS\r\n"), true, [this](bool, const QString &) {
        m_timer.callOnTimeout(this, [this] { startSending(); }, Qt::SingleShotConnection);
        m_timer.start(500);
    });
}

void PrinterLink::sendPrint()
{
    m_step = Printing;
    m_waits = 0;
    emit progress(tr("Printing..."), 1);
    write("PRINT " + QByteArray::number(m_copies) + "\r\n", true, [this](bool ok, const QString &error) {
        if (!ok) {
            done(false, tr("The printer stopped listening: %1").arg(error));
            return;
        }
        // Wait for the printer's "done" report so it isn't cut off mid-label.
        m_timer.callOnTimeout(this, [this] {
            if (m_step == Printing)
                done(true, tr("Printed."));
        }, Qt::SingleShotConnection);
        m_timer.start(PrintTicks * TickMs);
    });
}

void PrinterLink::onPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &)
{
    if (interface != CharIface || !changed.contains(QStringLiteral("Value")))
        return;
    const QByteArray value = changed.value(QStringLiteral("Value")).toByteArray();
    if (value.contains("Cmd error"))
        m_sawError = true;
    if (m_step == Printing && value.contains("PRINTING:DONE")) {
        m_timer.stop();
        m_timer.disconnect();
        done(true, tr("Printed."));
    }
}

void PrinterLink::done(bool ok, const QString &message)
{
    if (m_step == Idle)
        return;
    m_step = Idle;
    m_timer.stop();
    m_timer.disconnect();
    if (m_notifying) {
        bus().disconnect(Bluez, m_rx, PropsIface, QStringLiteral("PropertiesChanged"), this,
            SLOT(onPropertiesChanged(QString, QVariantMap, QStringList)));
        bus().call(QDBusMessage::createMethodCall(Bluez, m_rx, CharIface, QStringLiteral("StopNotify")), QDBus::Block, 3000);
        m_notifying = false;
    }
    // Let go of the printer so the phone app can still reach it.
    if (!m_device.isEmpty())
        bus().asyncCall(QDBusMessage::createMethodCall(Bluez, m_device, DeviceIface, QStringLiteral("Disconnect")));
    m_body.clear();
    emit finished(ok, message);
}
