#pragma once

#include <QImage>
#include <QObject>
#include <QTimer>
#include <QVector>

#include "labelrenderer.h"
#include "printerlink.h"

class FilePicker;

// The window's state. Printing stays off until the printer link is wired up.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool hasLabel READ hasLabel NOTIFY changed)
    Q_PROPERTY(QString fileName READ fileName NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(int page READ page WRITE setPage NOTIFY changed)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY changed)
    Q_PROPERTY(int darkness READ darkness WRITE setDarkness NOTIFY changed)
    Q_PROPERTY(int copies READ copies WRITE setCopies NOTIFY changed)
    Q_PROPERTY(bool photo READ photo WRITE setPhoto NOTIFY changed)
    Q_PROPERTY(int fit READ fit WRITE setFit NOTIFY changed)
    Q_PROPERTY(int sizeIndex READ sizeIndex WRITE setSizeIndex NOTIFY changed)
    Q_PROPERTY(double customWidthMm READ customWidthMm WRITE setCustomWidthMm NOTIFY changed)
    Q_PROPERTY(double customHeightMm READ customHeightMm WRITE setCustomHeightMm NOTIFY changed)
    Q_PROPERTY(int labelWidth READ labelWidth NOTIFY changed)
    Q_PROPERTY(int labelHeight READ labelHeight NOTIFY changed)
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(bool canPrint READ canPrint NOTIFY changed)
    Q_PROPERTY(bool printing READ printing NOTIFY changed)
    Q_PROPERTY(double printProgress READ printProgress NOTIFY changed)

public:
    explicit Backend(FilePicker *picker, QObject *parent = nullptr);

    bool hasLabel() const { return !m_preview.isNull(); }
    QString fileName() const { return m_fileName; }
    QString error() const { return m_error; }
    QString status() const { return m_status; }
    int page() const { return m_page; }
    int pageCount() const { return m_pageCount; }
    int darkness() const { return m_darkness; }
    int copies() const { return m_copies; }
    bool photo() const { return m_photo; }
    int fit() const { return m_fit; }
    int sizeIndex() const { return m_sizeIndex; }
    double customWidthMm() const { return m_customWidthMm; }
    double customHeightMm() const { return m_customHeightMm; }
    int labelWidth() const { return m_labelWidth; }
    int labelHeight() const { return m_labelHeight; }
    int revision() const { return m_revision; }
    bool canPrint() const { return hasLabel() && !m_link.busy(); }
    bool printing() const { return m_link.busy(); }
    double printProgress() const { return m_printProgress; }

    QImage preview() const { return m_preview; }
    QString path() const { return m_path; }
    LabelRenderer::Request request() const;

    void setPage(int page);
    void setDarkness(int darkness);
    void setCopies(int copies);
    void setPhoto(bool photo);
    void setFit(int fit);
    void setSizeIndex(int index);
    void setCustomWidthMm(double mm);
    void setCustomHeightMm(double mm);

    Q_INVOKABLE void openFile();
    Q_INVOKABLE void openPath(const QString &path);
    Q_INVOKABLE void print();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

signals:
    void changed();

private:
    struct Snapshot {
        QString path;
        int page = 1;
        int darkness = 10;
        int copies = 1;
        int fit = 0;
        int sizeIndex = 0;
        bool photo = false;
        bool fitChosen = false;
        double customWidthMm = 50;
        double customHeightMm = 30;
        bool operator==(const Snapshot &other) const
        {
            return path == other.path && page == other.page && darkness == other.darkness
                && copies == other.copies && fit == other.fit && sizeIndex == other.sizeIndex
                && photo == other.photo && fitChosen == other.fitChosen
                && customWidthMm == other.customWidthMm && customHeightMm == other.customHeightMm;
        }
    };

    void pushUndo();
    Snapshot capture() const;
    void restore(const Snapshot &snapshot);
    void updateSize();
    void scheduleRender();
    void renderNow();

    FilePicker *m_picker = nullptr;
    QString m_path;
    QString m_fileName;
    QString m_error;
    QString m_status;
    int m_page = 1;
    int m_pageCount = 0;
    int m_darkness = 10;
    int m_copies = 1;
    bool m_photo = false;
    int m_fit = 0;
    int m_sizeIndex = 0;
    bool m_fitChosen = false;
    double m_customWidthMm = 50;
    double m_customHeightMm = 30;
    double m_widthMm = 101.6;
    double m_heightMm = 152.4;
    int m_labelWidth = 808;
    int m_labelHeight = 1218;
    int m_revision = 0;
    QImage m_preview;
    bool m_restoring = false;
    QVector<Snapshot> m_undo;
    QVector<Snapshot> m_redo;
    QTimer m_renderTimer;
    PrinterLink m_link;
    double m_printProgress = 0;
};
