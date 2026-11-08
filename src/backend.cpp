#include "backend.h"

#include "tspljob.h"

#include "filepicker.h"
#include "labelsize.h"

#include <QFileInfo>

Backend::Backend(FilePicker *picker, QObject *parent) : QObject(parent), m_picker(picker)
{
    connect(&m_link, &PrinterLink::progress, this, [this](const QString &message, double fraction) {
        m_status = message;
        m_printProgress = fraction;
        emit changed();
    });
    connect(&m_link, &PrinterLink::finished, this, [this](bool ok, const QString &message) {
        if (ok) {
            m_status = message;
            m_error.clear();
        } else {
            m_status.clear();
            m_error = message;
        }
        m_printProgress = ok ? 1 : 0;
        emit changed();
    });
    if (m_picker) {
        connect(m_picker, &FilePicker::openSelected, this, [this](const QUrl &url) {
            const QString path = url.toLocalFile();
            if (path.isEmpty()) {
                m_error = tr("Couldn't use that file.");
                m_status.clear();
                emit changed();
                return;
            }
            openPath(path);
        });
        connect(m_picker, &FilePicker::failed, this, [this](const QString &message) {
            m_error = message;
            m_status.clear();
            emit changed();
        });
    }

    m_renderTimer.setSingleShot(true);
    connect(&m_renderTimer, &QTimer::timeout, this, &Backend::renderNow);
    updateSize();
}

LabelRenderer::Request Backend::request() const
{
    LabelRenderer::Request request;
    request.widthMm = m_widthMm;
    request.heightMm = m_heightMm;
    request.pageIndex = m_page - 1;
    request.fit = static_cast<LabelRenderer::Fit>(qBound(0, m_fit, 2));
    request.turn = static_cast<LabelRenderer::Turn>(qBound(0, m_turn, 2));
    request.photo = m_photo;
    return request;
}

void Backend::updateSize()
{
    if (m_sizeIndex >= 0 && m_sizeIndex < kLabelPresetCount) {
        m_widthMm = kLabelPresets[m_sizeIndex].widthMm;
        m_heightMm = kLabelPresets[m_sizeIndex].heightMm;
    } else {
        m_widthMm = m_customWidthMm;
        m_heightMm = m_customHeightMm;
    }
    m_labelWidth = labelWidthDots(m_widthMm);
    m_labelHeight = labelHeightDots(m_heightMm);
}

Backend::Snapshot Backend::capture() const
{
    Snapshot snapshot;
    snapshot.path = m_path;
    snapshot.page = m_page;
    snapshot.darkness = m_darkness;
    snapshot.copies = m_copies;
    snapshot.fit = m_fit;
    snapshot.turn = m_turn;
    snapshot.sizeIndex = m_sizeIndex;
    snapshot.photo = m_photo;
    snapshot.fitChosen = m_fitChosen;
    snapshot.customWidthMm = m_customWidthMm;
    snapshot.customHeightMm = m_customHeightMm;
    return snapshot;
}

void Backend::pushUndo()
{
    if (m_restoring)
        return;
    const Snapshot now = capture();
    if (!m_undo.isEmpty() && m_undo.last() == now)
        return;
    m_undo.append(now);
    if (m_undo.size() > 40)
        m_undo.removeFirst();
    m_redo.clear();
}

void Backend::restore(const Snapshot &snapshot)
{
    m_restoring = true;
    m_path = snapshot.path;
    m_fileName = QFileInfo(m_path).fileName();
    m_page = snapshot.page;
    m_darkness = snapshot.darkness;
    m_copies = snapshot.copies;
    m_fit = snapshot.fit;
    m_turn = snapshot.turn;
    m_sizeIndex = snapshot.sizeIndex;
    m_photo = snapshot.photo;
    m_fitChosen = snapshot.fitChosen;
    m_customWidthMm = snapshot.customWidthMm;
    m_customHeightMm = snapshot.customHeightMm;
    m_restoring = false;
    updateSize();
    if (m_path.isEmpty()) {
        m_preview = {};
        m_pageCount = 0;
        m_blank = false;
        m_error.clear();
        m_status.clear();
        ++m_revision;
        emit changed();
        return;
    }
    scheduleRender();
}

void Backend::openFile()
{
    if (m_picker)
        m_picker->openLabel();
}

void Backend::openPath(const QString &path)
{
    pushUndo();
    m_path = path;
    m_fileName = QFileInfo(path).fileName();
    m_page = 1;
    m_error.clear();
    if (!m_fitChosen) {
        const bool pdf = path.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive);
        m_fit = pdf ? 0 : 1;
    }
    m_turn = 0;
    m_status = tr("Reading the file...");
    emit changed();
    scheduleRender();
}

void Backend::print()
{
    if (!hasLabel()) {
        openFile();
        return;
    }
    if (m_link.busy())
        return;
    TsplOptions options;
    options.widthMm = m_widthMm;
    options.heightMm = m_heightMm;
    options.density = m_darkness;
    options.copies = m_copies;
    const QByteArray body = TsplJob::encodeBody(m_preview, options);
    if (body.isEmpty()) {
        m_error = tr("Couldn't build the label.");
        emit changed();
        return;
    }
    m_error.clear();
    m_printProgress = 0;
    m_link.print(body, m_copies);
    emit changed();
}

void Backend::undo()
{
    if (m_undo.isEmpty())
        return;
    m_redo.append(capture());
    restore(m_undo.takeLast());
}

void Backend::redo()
{
    if (m_redo.isEmpty())
        return;
    m_undo.append(capture());
    restore(m_redo.takeLast());
}

void Backend::setPage(int page)
{
    page = qMax(1, page);
    if (page == m_page)
        return;
    pushUndo();
    m_page = page;
    scheduleRender();
}

void Backend::setDarkness(int darkness)
{
    darkness = qBound(1, darkness, 15);
    if (darkness == m_darkness)
        return;
    pushUndo();
    m_darkness = darkness;
    emit changed();
}

void Backend::setCopies(int copies)
{
    copies = qBound(1, copies, 99);
    if (copies == m_copies)
        return;
    pushUndo();
    m_copies = copies;
    emit changed();
}

void Backend::setPhoto(bool photo)
{
    if (photo == m_photo)
        return;
    pushUndo();
    m_photo = photo;
    scheduleRender();
}

void Backend::setFit(int fit)
{
    fit = qBound(0, fit, 2);
    if (fit == m_fit)
        return;
    pushUndo();
    m_fit = fit;
    m_fitChosen = true;
    scheduleRender();
}

void Backend::setTurn(int turn)
{
    turn = qBound(0, turn, 2);
    if (turn == m_turn)
        return;
    pushUndo();
    m_turn = turn;
    scheduleRender();
}

void Backend::setSizeIndex(int index)
{
    index = qBound(0, index, kCustomSizeIndex);
    if (index == m_sizeIndex)
        return;
    pushUndo();
    m_sizeIndex = index;
    updateSize();
    scheduleRender();
}

void Backend::setCustomWidthMm(double mm)
{
    if (qFuzzyCompare(mm, m_customWidthMm))
        return;
    pushUndo();
    m_customWidthMm = mm;
    if (m_sizeIndex == kCustomSizeIndex) {
        updateSize();
        scheduleRender();
    } else {
        emit changed();
    }
}

void Backend::setCustomHeightMm(double mm)
{
    if (qFuzzyCompare(mm, m_customHeightMm))
        return;
    pushUndo();
    m_customHeightMm = mm;
    if (m_sizeIndex == kCustomSizeIndex) {
        updateSize();
        scheduleRender();
    } else {
        emit changed();
    }
}

void Backend::scheduleRender()
{
    emit changed();
    if (m_path.isEmpty())
        return;
    m_renderTimer.start(30);
}

void Backend::renderNow()
{
    if (m_path.isEmpty())
        return;

    const LabelRenderer::Result result = LabelRenderer::renderFile(m_path, request());
    m_pageCount = result.pageCount;
    if (!result.ok()) {
        m_preview = {};
        m_blank = false;
        m_error = result.error;
        m_status.clear();
    } else {
        m_preview = result.image;
        m_blank = result.blank;
        m_error.clear();
        m_status.clear();
        if (m_page > qMax(1, m_pageCount))
            m_page = qMax(1, m_pageCount);
    }
    ++m_revision;
    emit changed();
}
