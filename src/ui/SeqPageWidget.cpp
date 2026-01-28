#include "SeqPageWidget.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QtGlobal>
#include "PadBank.h"
#include "Theme.h"

SeqPageWidget::SeqPageWidget(PadBank *pads, QWidget *parent) : QWidget(parent), m_pads(pads) {
    setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);


    m_padColors = {Theme::accent(), Theme::accentAlt(), QColor(110, 170, 255), QColor(255, 188, 64),
                   QColor(210, 120, 255), QColor(90, 220, 120), QColor(255, 90, 110), QColor(120, 200, 210)};

    for (int pad = 0; pad < 8; ++pad) {
        for (int step = 0; step < 64; ++step) {
            m_steps[pad][step] = false;
        }
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

    const QRectF grid = gridRect();
    if (!grid.contains(pos)) {
        return;
    }

    const int cols = 64;
    const int rows = 8;
    const float labelW = Theme::pxF(48.0f);
    const float headerH = Theme::pxF(24.0f);
    const QRectF gridArea(grid.left() + labelW, grid.top() + headerH,
                          grid.width() - labelW, grid.height() - headerH);
    if (!gridArea.contains(pos)) {
        return;
    }
    const float cellW = gridArea.width() / cols;
    const float cellH = gridArea.height() / rows;

    const int col = static_cast<int>((pos.x() - gridArea.left()) / cellW);
    const int row = static_cast<int>((pos.y() - gridArea.top()) / cellH);
    const int step = col;
    if (step < 0 || step >= 64 || row < 0 || row >= 8) {
        return;
    }

    m_activePad = row;
    if (m_pads) {
        m_pads->setActivePad(row);
    }

    if (event->modifiers().testFlag(Qt::ShiftModifier)) {
        const bool nextState = !m_steps[row][step];
        for (int i = 0; i < 64; ++i) {
            if (i % 2 == step % 2) {
                m_steps[row][i] = nextState;
            }
        }
    } else {
        m_steps[row][step] = !m_steps[row][step];
    }

    update();
}

void SeqPageWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);

    QPainter p(this);

    Theme::paintBackground(p, rect());
    Theme::applyRenderHints(p);

    const QRectF grid = gridRect();
    p.setBrush(QColor(28, 28, 32));
    p.setPen(QPen(QColor(70, 70, 80), 1.0));
    p.drawRoundedRect(grid, Theme::px(6), Theme::px(6));

    const int cols = 64;
    const int rows = 8;
    const float labelW = Theme::pxF(48.0f);
    const float headerH = Theme::pxF(24.0f);
    const QRectF gridArea(grid.left() + labelW, grid.top() + headerH,
                          grid.width() - labelW, grid.height() - headerH);
    const float cellW = gridArea.width() / cols;
    const float cellH = gridArea.height() / rows;

    // Top bar numbers.
    p.setFont(Theme::baseFont(9, QFont::DemiBold));
    p.setPen(Theme::textMuted());
    for (int bar = 0; bar < 4; ++bar) {
        const float x = gridArea.left() + bar * (gridArea.width() / 4.0f);
        p.drawText(QRectF(x, grid.top(), gridArea.width() / 4.0f, headerH),
                   Qt::AlignCenter, QString::number(bar + 1));
    }

    // Left labels.
    for (int row = 0; row < rows; ++row) {
        const QRectF labelRect(grid.left(), gridArea.top() + row * cellH,
                               labelW - Theme::px(6), cellH);
        const bool active = (row == m_activePad);
        p.setPen(active ? Theme::accent() : Theme::textMuted());
        p.setFont(Theme::baseFont(10, QFont::DemiBold));
        p.drawText(labelRect, Qt::AlignCenter, QString("A%1").arg(row + 1));
    }

    // Grid lines.
    for (int col = 0; col <= cols; ++col) {
        const float x = gridArea.left() + col * cellW;
        const bool major = (col % 4 == 0);
        p.setPen(QPen(major ? QColor(80, 80, 90) : QColor(50, 50, 70),
                      major ? 1.4 : 1.0));
        p.drawLine(QPointF(x, gridArea.top()), QPointF(x, gridArea.bottom()));
    }
    for (int row = 0; row <= rows; ++row) {
        const float y = gridArea.top() + row * cellH;
        p.setPen(QPen(QColor(55, 55, 70), 1.0));
        p.drawLine(QPointF(gridArea.left(), y), QPointF(gridArea.right(), y));
    }

    // Steps for all pads.
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            if (!m_steps[row][col]) {
                continue;
            }
            const QRectF cell(gridArea.left() + col * cellW, gridArea.top() + row * cellH,
                              cellW, cellH);
            p.setBrush(QColor(180, 70, 100));
            p.setPen(Qt::NoPen);
            p.drawRect(cell.adjusted(Theme::px(2), Theme::px(4),
                                     -Theme::px(2), -Theme::px(4)));
        }
    }

    // Playhead
    if (m_playing || m_waiting) {
        const float x = gridArea.left() + m_playStep * cellW;
        p.setPen(QPen(Theme::accentAlt(), 2.0));
        p.drawLine(QPointF(x, gridArea.top()), QPointF(x, gridArea.bottom()));
    }
}
