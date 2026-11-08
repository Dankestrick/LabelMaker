#pragma once

#include <functional>

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

// Sends one TSPL job to the D450 over Bluetooth LE, through BlueZ on the
// system D-Bus (no extra Qt modules). The printer needs no pairing.
//
// What the printer taught us (2026-11-07):
// - It speaks TSPL on the Microchip transparent UART service.
// - Writes without a response alone lose bytes at any pacing, so every
//   second packet waits for a response.
// - A damaged transfer shows up as "Cmd error:<bytes>" on notify. The job is
//   sent without PRINT, checked, and only then printed; a damaged one is
//   cleared with CLS and sent once more.
class PrinterLink : public QObject {
    Q_OBJECT

public:
    explicit PrinterLink(QObject *parent = nullptr);

    bool busy() const { return m_step != Idle; }

    // body: everything up to and including the BITMAP data, without PRINT.
    void print(const QByteArray &body, int copies);

signals:
    void progress(const QString &message, double fraction);
    void finished(bool ok, const QString &message);

private slots:
    void onPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);

private:
    enum Step { Idle, Finding, Connecting, Sending, Checking, Printing };

    void findDevice();
    bool scanObjects();
    void connectDevice();
    void waitForServices();
    void startSending();
    void sendNext();
    void check();
    void sendPrint();
    void done(bool ok, const QString &message);
    void write(const QByteArray &data, bool request, std::function<void(bool, QString)> then);

    Step m_step = Idle;
    QByteArray m_body;
    int m_copies = 1;
    int m_offset = 0;
    int m_packet = 0;
    int m_chunk = 20;
    int m_attempt = 0;
    int m_waits = 0;
    bool m_discovering = false;
    bool m_sawError = false;
    bool m_notifying = false;
    QString m_device;
    QString m_tx;
    QString m_rx;
    QTimer m_timer;
};
