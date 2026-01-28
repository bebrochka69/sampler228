#include "SeqPageWidget.h"

#include <QGestureEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPinchGesture>
#include <QWheelEvent>
#include <QtGlobal>
#include <cmath>
#include "PadBank.h"
#include "Theme.h"

SeqPageWidget::SeqPageWidget(PadBank *pads, QWidget *parent) : QWidget(parent), m_pads(pads) {
    setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_AcceptTouchEvents);
    grabGesture(Qt::PinchGesture);


    m_padColors = {Theme::accent(), Theme::accentAlt(), QColor(110, 170, 255), QColor(255, 188, 64),
                   QColor(210, 120, 255), QColor(90, 220, 120), QColor(255, 90, 110), QColor(120, 200, 210)};

    for (int pad = 0; pad < 8; ++pad) {
        for (int step = 0; step < 64; ++step) {
            m_steps[pad][step] = false;
        }
        m_views[pad].zoom = 1.0f;
        m_views[pad].offset = 0.0f;
    }

    m_playTimer.setTimerType(Qt::PreciseTimer);
    m_playTimer.setInterval(stepIntervalMs());
    connect(&m_playTimer, &QTimer::timeout, this, &SeqPageWidget::advancePlayhead);

    m_readyTimer.setInterval(60);
    connect(&m_readyTimer, &QTimer::timeout, this, [this]() {
        if (!m_waiting) {
            m_readyTimer.stop();
            return;
        }
        if (padsReady()) {
            m_waiting = false;
            m_readyTimer.stop();
            startPlayback();
        }
        update();
    });

    if (m_pads) {
        m_activePad = m_pads->activePad();
        connect(m_pads, &PadBank::activePadChanged, this, [this](int index) {
            m_activePad = index;
            update();
        });
        connect(m_pads, &PadBank::bpmChanged, this, [this](int) {
            if (m_playing) {
                m_playTimer.setInterval(stepIntervalMs());
            }
            update();
        });
    }
}

QRectF SeqPageWidget::gridRect() const {
    const int margin = Theme::px(12);
    const int top = Theme::px(10);
    return QRectF(margin, top, width() - 2 * margin, height() - margin - top);
}

QRectF SeqPageWidget::listRect() const {
    const QRectF grid = gridRect();
    const float listW = qMax(Theme::pxF(180.0f), grid.width() * 0.24f);
    return QRectF(grid.left(), grid.top(), listW, grid.height());
}

QRectF SeqPageWidget::gridInnerRect() const {
    const QRectF grid = gridRect();
    const QRectF list = listRect();
    return QRectF(list.right() + Theme::px(10), grid.top(),
                  grid.right() - list.right() - Theme::px(10), grid.height());
}

int SeqPageWidget::stepIntervalMs() const {
    const int bpm = m_pads ? m_pads->bpm() : m_bpm;
    const int ms = 60000 / qMax(1, bpm) / 4;
    return qMax(20, ms);
}

bool SeqPageWidget::padsReady() const {
    if (!m_pads) {
        return true;
    }
    for (int pad = 0; pad < 8; ++pad) {
        bool used = false;
        for (int step = 0; step < 64; ++step) {
            if (m_steps[pad][step]) {
                used = true;
                break;
            }
        }
        if (used && !m_pads->isPadReady(pad)) {
            return false;
        }
    }
    return true;
}

void SeqPageWidget::startPlayback() {
    m_playing = true;
    m_playStep = 0;
    triggerStep(m_playStep);
    m_playTimer.setInterval(stepIntervalMs());
    m_playTimer.start();
    update();
}

void SeqPageWidget::togglePlayback() {
    if (m_playing || m_waiting) {
        m_playing = false;
        m_waiting = false;
        m_readyTimer.stop();
        m_playTimer.stop();
        if (m_pads) {
            m_pads->stopAll();
        }
    } else {
        if (!padsReady()) {
            m_waiting = true;
            m_readyTimer.start();
        } else {
            startPlayback();
        }
    }
    update();
}

void SeqPageWidget::advancePlayhead() {
    m_playStep = (m_playStep + 1) % 64;
    triggerStep(m_playStep);
    update();
}

void SeqPageWidget::triggerStep(int step) {
    if (!m_pads) {
        return;
    }
    for (int pad = 0; pad < 8; ++pad) {
        if (m_steps[pad][step]) {
            m_pads->triggerPad(pad);
        }
    }
}

void SeqPageWidget::clampView(int pad) {
    if (pad < 0 || pad >= 8) {
        return;
    }
    ViewState &view = m_views[pad];
    view.zoom = qBound(1.0f, view.zoom, 8.0f);
    const float visibleSteps = 64.0f / view.zoom;
    const float maxOffset = qMax(0.0f, 64.0f - visibleSteps);
    view.offset = qBound(0.0f, view.offset, maxOffset);
}

void SeqPageWidget::applyZoom(int pad, float scale, float centerRatio) {
    if (pad < 0 || pad >= 8) {
        return;
    }
    ViewState &view = m_views[pad];
    const float beforeZoom = view.zoom;
    const float visibleBefore = 64.0f / beforeZoom;
    const float centerStep = view.offset + visibleBefore * centerRatio;

    view.zoom *= scale;
    clampView(pad);

    const float visibleAfter = 64.0f / view.zoom;
    view.offset = centerStep - visibleAfter * centerRatio;
    clampView(pad);
}

void SeqPageWidget::applyPan(int pad, float deltaSteps) {
    if (pad < 0 || pad >= 8) {
        return;
    }
    ViewState &view = m_views[pad];
    view.offset = view.offset + deltaSteps;
    clampView(pad);
}

void SeqPageWidget::keyPressEvent(QKeyEvent *event) {
    const int key = event->key();
    if (key == Qt::Key_Space) {
        togglePlayback();
        return;
    }
    if (key >= Qt::Key_1 && key <= Qt::Key_8) {
        m_activePad = key - Qt::Key_1;
        if (m_pads) {
            m_pads->setActivePad(m_activePad);
        }
        update();
        return;
    }
    if (key == Qt::Key_R) {
        m_playStep = 0;
        update();
        return;
    }
}

void SeqPageWidget::mousePressEvent(QMouseEvent *event) {
    setFocus(Qt::MouseFocusReason);
    const QPointF pos = event->position();

    const QRectF list = listRect();
    if (list.contains(pos)) {
        const float rowH = list.height() / 8.0f;
        const int row = static_cast<int>((pos.y() - list.top()) / rowH);
        if (row >= 0 && row < 8) {
            m_activePad = row;
            if (m_pads) {
                m_pads->setActivePad(row);
            }
            update();
            return;
        }
    }

    const QRectF gridArea = gridInnerRect();
    if (!gridArea.contains(pos)) {
        return;
    }

    m_dragging = true;
    m_dragMoved = false;
    m_dragStartPos = pos;
    m_dragStartOffset = m_views[m_activePad].offset;
}

void SeqPageWidget::mouseMoveEvent(QMouseEvent *event) {
    if (!m_dragging) {
        return;
    }
    const QPointF pos = event->position();
    const QRectF gridArea = gridInnerRect();
    if (!gridArea.contains(pos)) {
        return;
    }
    const float dx = pos.x() - m_dragStartPos.x();
    if (std::fabs(dx) > Theme::pxF(3.0f)) {
        m_dragMoved = true;
    }
    const float visibleSteps = 64.0f / m_views[m_activePad].zoom;
    const float cellW = gridArea.width() / visibleSteps;
    const float deltaSteps = -dx / qMax(1.0f, cellW);
    m_views[m_activePad].offset = m_dragStartOffset + deltaSteps;
    clampView(m_activePad);
    update();
}

void SeqPageWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (!m_dragging) {
        return;
    }
    m_dragging = false;

    if (m_dragMoved) {
        update();
        return;
    }

    const QPointF pos = event->position();
    const QRectF gridArea = gridInnerRect();
    if (!gridArea.contains(pos)) {
        return;
    }

    const float visibleSteps = 64.0f / m_views[m_activePad].zoom;
    const float cellW = gridArea.width() / visibleSteps;
    const float stepFloat = m_views[m_activePad].offset + (pos.x() - gridArea.left()) / cellW;
    const int step = static_cast<int>(std::floor(stepFloat));
    if (step < 0 || step >= 64) {
        return;
    }

    if (event->modifiers().testFlag(Qt::ShiftModifier)) {
        const bool nextState = !m_steps[m_activePad][step];
        for (int i = 0; i < 64; ++i) {
            if (i % 2 == step % 2) {
                m_steps[m_activePad][i] = nextState;
            }
        }
    } else {
        m_steps[m_activePad][step] = !m_steps[m_activePad][step];
    }
    update();
}

void SeqPageWidget::wheelEvent(QWheelEvent *event) {
    const QRectF gridArea = gridInnerRect();
    const QPointF pos = event->position();
    if (!gridArea.contains(pos)) {
        QWidget::wheelEvent(event);
        return;
    }
    const float delta = static_cast<float>(event->angleDelta().y());
    if (delta == 0.0f) {
        return;
    }
    const float scale = delta > 0 ? 1.1f : 0.9f;
    const float centerRatio = (pos.x() - gridArea.left()) / qMax(1.0f, gridArea.width());
    applyZoom(m_activePad, scale, centerRatio);
    update();
}

bool SeqPageWidget::event(QEvent *event) {
    if (event->type() == QEvent::Gesture) {
        auto *gestureEvent = static_cast<QGestureEvent *>(event);
        if (QGesture *gesture = gestureEvent->gesture(Qt::PinchGesture)) {
            auto *pinch = static_cast<QPinchGesture *>(gesture);
            const QRectF gridArea = gridInnerRect();
            const QPointF center = pinch->centerPoint();
            const float centerRatio = gridArea.contains(center)
                                          ? (center.x() - gridArea.left()) /
                                                qMax(1.0f, gridArea.width())
                                          : 0.5f;
            const float scale = pinch->scaleFactor();
            if (scale > 0.0f) {
                applyZoom(m_activePad, scale, centerRatio);
                update();
            }
            return true;
        }
    }
    return QWidget::event(event);
}

void SeqPageWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);

    QPainter p(this);

    Theme::paintBackground(p, rect());
    Theme::applyRenderHints(p);

    const QRectF grid = gridRect();
    const QRectF list = listRect();
    const QRectF gridArea = gridInnerRect();

    p.setBrush(QColor(28, 28, 32));
    p.setPen(QPen(QColor(70, 70, 80), 1.0));
    p.drawRoundedRect(grid, Theme::px(6), Theme::px(6));

    // Left pad list.
    p.setBrush(QColor(26, 26, 30));
    p.setPen(QPen(QColor(60, 60, 70), 1.0));
    p.drawRoundedRect(list, Theme::px(6), Theme::px(6));

    const float rowH = list.height() / 8.0f;
    for (int row = 0; row < 8; ++row) {
        const QRectF rowRect(list.left(), list.top() + row * rowH, list.width(), rowH);
        const bool active = (row == m_activePad);
        p.setBrush(active ? QColor(38, 38, 48) : QColor(26, 26, 30));
        p.setPen(Qt::NoPen);
        p.drawRect(rowRect.adjusted(0, 0, 0, -1));

        p.setPen(active ? Theme::accent() : Theme::textMuted());
        p.setFont(Theme::condensedFont(12, QFont::Bold));
        p.drawText(rowRect.adjusted(Theme::px(10), 0, -Theme::px(10), 0),
                   Qt::AlignLeft | Qt::AlignVCenter, QString("A%1").arg(row + 1));

        if (m_pads) {
            const QString name = m_pads->padName(row);
            p.setPen(active ? Theme::text() : Theme::textMuted());
            p.setFont(Theme::baseFont(9, QFont::DemiBold));
            p.drawText(rowRect.adjusted(Theme::px(44), 0, -Theme::px(10), 0),
                       Qt::AlignLeft | Qt::AlignVCenter, name.isEmpty() ? "EMPTY" : name.left(18));
        }
    }

    // Grid for active pad.
    p.setBrush(QColor(20, 20, 24));
    p.setPen(QPen(QColor(60, 60, 70), 1.0));
    p.drawRoundedRect(gridArea, Theme::px(6), Theme::px(6));

    const ViewState &view = m_views[m_activePad];
    const float visibleSteps = 64.0f / view.zoom;
    const float cellW = gridArea.width() / visibleSteps;
    const float cellH = gridArea.height();

    // Vertical grid lines.
    for (int i = 0; i <= static_cast<int>(visibleSteps); ++i) {
        const float x = gridArea.left() + i * cellW;
        const bool major = (static_cast<int>(view.offset) + i) % 4 == 0;
        p.setPen(QPen(major ? QColor(70, 70, 90) : QColor(50, 50, 70),
                      major ? 1.4 : 1.0));
        p.drawLine(QPointF(x, gridArea.top()), QPointF(x, gridArea.bottom()));
    }

    // Steps for active pad.
    for (int i = 0; i < static_cast<int>(visibleSteps); ++i) {
        const int step = static_cast<int>(view.offset) + i;
        if (step < 0 || step >= 64) {
            continue;
        }
        if (!m_steps[m_activePad][step]) {
            continue;
        }
        const QRectF cell(gridArea.left() + i * cellW, gridArea.top(), cellW, cellH);
        p.setBrush(QColor(180, 70, 100));
        p.setPen(Qt::NoPen);
        p.drawRect(cell.adjusted(Theme::px(4), Theme::px(6), -Theme::px(4), -Theme::px(6)));
    }

    // Playhead
    if (m_playing || m_waiting) {
        const float stepPos = static_cast<float>(m_playStep);
        const float rel = stepPos - view.offset;
        if (rel >= 0.0f && rel <= visibleSteps) {
            const float x = gridArea.left() + rel * cellW;
            p.setPen(QPen(Theme::accentAlt(), 2.0));
            p.drawLine(QPointF(x, gridArea.top()), QPointF(x, gridArea.bottom()));
        }
    }
}
