#include "PianoRollOverlay.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <cmath>

#include "PadBank.h"
#include "Theme.h"

PianoRollOverlay::PianoRollOverlay(PadBank *pads, QWidget *parent)
    : QWidget(parent), m_pads(pads) {
    setAutoFillBackground(false);
    setAttribute(Qt::WA_StyledBackground, true);
    setVisible(false);
}

void PianoRollOverlay::showForPad(int pad) {
    m_activePad = qBound(0, pad, 7);
    if (parentWidget()) {
        setGeometry(parentWidget()->rect());
    }
    if (m_pads) {
        m_pads->setActivePad(m_activePad);
    }
    setVisible(true);
    raise();
    emitStepsChanged();
    update();
}

QRectF PianoRollOverlay::panelRect() const {
    return rect().adjusted(Theme::px(14), Theme::px(14),
                           -Theme::px(14), -Theme::px(14));
}

QRectF PianoRollOverlay::rightPanelRect() const {
    const QRectF panel = panelRect();
    const float w = Theme::pxF(180.0f);
    return QRectF(panel.right() - w, panel.top() + Theme::px(48),
                  w - Theme::px(8), panel.height() - Theme::px(56));
}

QRectF PianoRollOverlay::timelineRect() const {
    const QRectF panel = panelRect();
    const QRectF rightPanel = rightPanelRect();
    const float top = panel.top() + Theme::px(48);
    const float left = panel.left() + Theme::px(12);
    return QRectF(left, top,
                  rightPanel.left() - left - Theme::px(10),
                  panel.bottom() - top - Theme::px(12));
}

float PianoRollOverlay::baseCellWidth() const {
    return Theme::pxF(28.0f);
}

float PianoRollOverlay::cellWidth() const {
    return baseCellWidth() * m_zoom;
}

int PianoRollOverlay::clampStep(int step) const {
    return qBound(0, step, m_totalSteps - 1);
}

int PianoRollOverlay::stepFromX(float x) const {
    const QRectF grid = timelineRect();
    const float local = x - grid.left();
    const float step = local / cellWidth() + m_scroll;
    return clampStep(static_cast<int>(std::floor(step + 0.5f)));
}

float PianoRollOverlay::xFromStep(int step) const {
    const QRectF grid = timelineRect();
    return grid.left() + (step - m_scroll) * cellWidth();
}

int PianoRollOverlay::noteAt(const QPointF &pos) const {
    const QRectF grid = timelineRect();
    if (!grid.contains(pos)) {
        return -1;
    }
    const auto &notes = m_notes[static_cast<size_t>(m_activePad)];
    for (int i = 0; i < notes.size(); ++i) {
        const Note &note = notes[i];
        const float x = xFromStep(note.start);
        const float w = note.length * cellWidth();
        QRectF r(x, grid.top() + Theme::px(10),
                 w, grid.height() - Theme::px(20));
        if (r.contains(pos)) {
            return i;
        }
    }
    return -1;
}

bool PianoRollOverlay::hitNoteRightEdge(const Note &note, float x) const {
    const float edge = xFromStep(note.start + note.length);
    return std::abs(x - edge) <= Theme::pxF(6.0f);
}

void PianoRollOverlay::zoomBy(float factor) {
    const float before = m_zoom;
    m_zoom = qBound(0.5f, m_zoom * factor, 4.0f);
    if (!qFuzzyCompare(before, m_zoom)) {
        clampScroll();
        update();
    }
}

void PianoRollOverlay::clampScroll() {
    const QRectF grid = timelineRect();
    const float visibleSteps = grid.width() / cellWidth();
    const float maxScroll = qMax(0.0f, static_cast<float>(m_totalSteps) - visibleSteps);
    m_scroll = qBound(0.0f, m_scroll, maxScroll);
}

void PianoRollOverlay::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(Theme::withAlpha(Theme::bg0(), 235));
    p.setPen(Qt::NoPen);
    p.drawRect(rect());

    const QRectF panel = panelRect();
    p.setBrush(Theme::bg1());
    p.setPen(QPen(Theme::stroke(), 1.2));
    p.drawRoundedRect(panel, Theme::px(12), Theme::px(12));

    const QRectF header(panel.left() + Theme::px(12), panel.top() + Theme::px(8),
                        panel.width() - Theme::px(24), Theme::px(32));
    p.setPen(Theme::accent());
    p.setFont(Theme::condensedFont(14, QFont::Bold));
    p.drawText(header, Qt::AlignLeft | Qt::AlignVCenter, "PIANO ROLL");

    QString padLabel = QString("PAD %1").arg(m_activePad + 1);
    if (m_pads) {
        const QString name = m_pads->padName(m_activePad);
        if (!name.isEmpty()) {
            padLabel += QString("  %1").arg(name.left(18));
        }
    }
    p.setPen(Theme::textMuted());
    p.setFont(Theme::baseFont(10, QFont::DemiBold));
    p.drawText(header, Qt::AlignRight | Qt::AlignVCenter, padLabel);

    m_closeRect = QRectF(panel.right() - Theme::px(28), panel.top() + Theme::px(10),
                         Theme::px(18), Theme::px(18));
    p.setPen(QPen(Theme::text(), 1.6));
    p.drawLine(m_closeRect.topLeft(), m_closeRect.bottomRight());
    p.drawLine(m_closeRect.topRight(), m_closeRect.bottomLeft());

    m_zoomOutRect = QRectF(panel.left() + Theme::px(12), panel.top() + Theme::px(10),
                           Theme::px(22), Theme::px(22));
    m_zoomInRect = QRectF(m_zoomOutRect.right() + Theme::px(6),
                          m_zoomOutRect.top(), Theme::px(22), Theme::px(22));
    m_deleteRect = QRectF(m_zoomInRect.right() + Theme::px(10),
                          m_zoomOutRect.top(), Theme::px(80), Theme::px(22));

    auto drawButton = [&](const QRectF &r, const QString &label, bool active) {
        p.setBrush(active ? Theme::accentAlt() : Theme::bg2());
        p.setPen(QPen(Theme::stroke(), 1.0));
        p.drawRoundedRect(r, Theme::px(6), Theme::px(6));
        p.setPen(active ? Theme::bg0() : Theme::text());
        p.setFont(Theme::baseFont(9, QFont::DemiBold));
        p.drawText(r, Qt::AlignCenter, label);
    };

    drawButton(m_zoomOutRect, "-", false);
    drawButton(m_zoomInRect, "+", false);
    drawButton(m_deleteRect, "DELETE", m_deleteMode);

    const QRectF rightPanel = rightPanelRect();
    p.setBrush(Theme::bg2());
    p.setPen(QPen(Theme::stroke(), 1.0));
    p.drawRoundedRect(rightPanel, Theme::px(10), Theme::px(10));
    p.setPen(Theme::textMuted());
    p.setFont(Theme::baseFont(9, QFont::DemiBold));
    p.drawText(rightPanel.adjusted(Theme::px(10), Theme::px(8),
                                   -Theme::px(10), -Theme::px(8)),
               Qt::AlignTop | Qt::AlignLeft,
               "PARAMETERS\n(soon)");

    const QRectF grid = timelineRect();
    p.setBrush(QColor(28, 28, 32));
    p.setPen(QPen(QColor(70, 70, 80), 1.0));
    p.drawRoundedRect(grid, Theme::px(8), Theme::px(8));

    const float cellW = cellWidth();
    const float visibleSteps = grid.width() / cellW;
    const int startStep = qMax(0, static_cast<int>(std::floor(m_scroll)));
    const int endStep = qMin(m_totalSteps, startStep + static_cast<int>(std::ceil(visibleSteps)) + 1);

    // Bars header.
    p.setFont(Theme::baseFont(9, QFont::DemiBold));
    p.setPen(Theme::textMuted());
    const int stepsPerBar = 16;
    for (int bar = 0; bar < (m_totalSteps / stepsPerBar); ++bar) {
        const int step = bar * stepsPerBar;
        const float x = xFromStep(step);
        if (x > grid.right()) {
            break;
        }
        if (x + Theme::px(40) >= grid.left()) {
            p.drawText(QRectF(x, grid.top() - Theme::px(20), Theme::px(40), Theme::px(18)),
                       Qt::AlignLeft | Qt::AlignVCenter, QString::number(bar + 1));
        }
    }

    // Grid lines.
    for (int step = startStep; step <= endStep; ++step) {
        const float x = xFromStep(step);
        const bool major = (step % 4 == 0);
        p.setPen(QPen(major ? QColor(80, 80, 90) : QColor(50, 50, 70),
                      major ? 1.4 : 1.0));
        p.drawLine(QPointF(x, grid.top()), QPointF(x, grid.bottom()));
    }
    p.setPen(QPen(QColor(55, 55, 70), 1.0));
    p.drawLine(QPointF(grid.left(), grid.center().y()),
               QPointF(grid.right(), grid.center().y()));

    // Notes.
    const auto &notes = m_notes[static_cast<size_t>(m_activePad)];
    for (int i = 0; i < notes.size(); ++i) {
        const Note &note = notes[i];
        const float x = xFromStep(note.start);
        const float w = note.length * cellW;
        QRectF r(x, grid.top() + Theme::px(10),
                 w, grid.height() - Theme::px(20));
        if (r.right() < grid.left() || r.left() > grid.right()) {
            continue;
        }
        p.setBrush(QColor(180, 70, 100));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(r, Theme::px(6), Theme::px(6));
    }
}

void PianoRollOverlay::emitStepsChanged() {
    QVector<int> steps;
    const auto &notes = m_notes[static_cast<size_t>(m_activePad)];
    steps.reserve(notes.size());
    for (const auto &note : notes) {
        steps.push_back(qBound(0, note.start, m_totalSteps - 1));
    }
    emit stepsChanged(m_activePad, steps);
}

void PianoRollOverlay::mousePressEvent(QMouseEvent *event) {
    const QPointF pos = event->position();
    if (m_closeRect.contains(pos)) {
        setVisible(false);
        emit closed();
        return;
    }
    if (m_zoomOutRect.contains(pos)) {
        zoomBy(0.8f);
        return;
    }
    if (m_zoomInRect.contains(pos)) {
        zoomBy(1.25f);
        return;
    }
    if (m_deleteRect.contains(pos)) {
        m_deleteMode = !m_deleteMode;
        update();
        return;
    }

    const QRectF grid = timelineRect();
    if (!grid.contains(pos)) {
        return;
    }

    m_pressPos = pos;
    m_pressScroll = m_scroll;
    m_dragMode = DragNone;
    m_dragNoteIndex = noteAt(pos);

    if (m_dragNoteIndex >= 0) {
        auto &note = m_notes[static_cast<size_t>(m_activePad)][m_dragNoteIndex];
        if (m_deleteMode) {
            m_notes[static_cast<size_t>(m_activePad)].removeAt(m_dragNoteIndex);
            m_dragNoteIndex = -1;
            emitStepsChanged();
            update();
            return;
        }
        m_pressNote = note;
        if (hitNoteRightEdge(note, pos.x())) {
            m_dragMode = DragResize;
        } else {
            m_dragMode = DragMove;
        }
    } else {
        m_dragMode = DragPan;
    }
}

void PianoRollOverlay::mouseMoveEvent(QMouseEvent *event) {
    const QPointF pos = event->position();
    if (m_dragMode == DragPan) {
        const float dx = pos.x() - m_pressPos.x();
        if (std::abs(dx) < Theme::pxF(2.0f)) {
            return;
        }
        m_scroll = m_pressScroll - dx / cellWidth();
        clampScroll();
        update();
        return;
    }

    if (m_dragNoteIndex < 0) {
        return;
    }

    auto &notes = m_notes[static_cast<size_t>(m_activePad)];
    if (m_dragMode == DragMove) {
        const int step = stepFromX(pos.x());
        notes[m_dragNoteIndex].start = clampStep(step - m_pressNote.length / 2);
        emitStepsChanged();
        update();
        return;
    }
    if (m_dragMode == DragResize) {
        const int step = stepFromX(pos.x());
        int length = step - m_pressNote.start;
        length = qMax(1, length);
        notes[m_dragNoteIndex].length = length;
        emitStepsChanged();
        update();
        return;
    }
}

void PianoRollOverlay::mouseReleaseEvent(QMouseEvent *event) {
    if (m_dragMode == DragPan) {
        const float dx = event->position().x() - m_pressPos.x();
        if (std::abs(dx) < Theme::pxF(3.0f)) {
            const int step = stepFromX(event->position().x());
            Note note;
            note.start = step;
            note.length = 4;
            m_notes[static_cast<size_t>(m_activePad)].push_back(note);
            emitStepsChanged();
            update();
        }
    }
    m_dragMode = DragNone;
    m_dragNoteIndex = -1;
}

void PianoRollOverlay::wheelEvent(QWheelEvent *event) {
    if (event->angleDelta().y() > 0) {
        zoomBy(1.1f);
    } else {
        zoomBy(0.9f);
    }
}
