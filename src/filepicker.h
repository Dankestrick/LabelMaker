#pragma once

#include <QObject>
#include <QUrl>

// The open dialog. Tests can hand the backend a fake, the app uses the
// desktop portal so the user gets the normal file picker.
class FilePicker : public QObject {
    Q_OBJECT

public:
    explicit FilePicker(QObject *parent = nullptr) : QObject(parent) {}
    virtual void openLabel() = 0;

signals:
    void openSelected(const QUrl &url);
    void failed(const QString &message);
};
