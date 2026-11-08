#pragma once

#include "backend.h"

#include <QQuickImageProvider>

// Serves the 1-bit preview. The URL carries the revision so QML reloads it.
class LabelImageProvider : public QQuickImageProvider {
public:
    explicit LabelImageProvider(Backend *backend)
        : QQuickImageProvider(QQuickImageProvider::Image), m_backend(backend) {}

    QImage requestImage(const QString &, QSize *size, const QSize &) override
    {
        const QImage image = m_backend->preview();
        if (size)
            *size = image.size();
        return image;
    }

private:
    Backend *m_backend;
};
