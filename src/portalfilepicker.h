#pragma once

#include "filepicker.h"

class QDBusPendingCallWatcher;

// Desktop file chooser over xdg-desktop-portal. The request handshake follows
// Omacut (MIT, David Heinemeier Hansson): subscribe to the predicted request
// path before the call, so the response cannot arrive first.
class PortalFilePicker : public FilePicker {
    Q_OBJECT

public:
    explicit PortalFilePicker(QObject *parent = nullptr);
    void openLabel() override;

private slots:
    void handleResponse(uint response, const QVariantMap &results);

private:
    void clearPending();

    QString m_path;
    bool m_busy = false;
    QDBusPendingCallWatcher *m_watcher = nullptr;
};
