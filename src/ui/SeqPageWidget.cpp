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
    const int margin = Theme::px(10);
    const int top = Theme::px(10);
    const float heightRatio = 0.85f;
    return QRectF(margin, top, width() - 2 * margin, height() * heightRatio);
}

QRectF SeqPageWidget::padsRect() const {
    const QRectF grid = gridRect();
    return QRectF(grid.left(), grid.bottom() + Theme::px(12), grid.width(), Theme::px(80));
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
    const float labelW = Theme::pxF(34.0f);
    const QRectF gridArea(grid.left() + labelW, grid.top(),
                          grid.width() - labelW, grid.height());
    if (!gridArea.contains(pos)) {
        return;
    }
    const float cellW = gridArea.width() / cols;
    const float cellH = gridArea.height() / rows;

    const int col = static_cast<int>((pos.x() - gridArea.left()) / cellW);
    const int row = static_cast<int>((pos.y() - gridArea.top()) / cellH);
    const int step = col;
    if (step < 0 || step >= 64) {
        return;
    }

    if (row < 0 || row >= 8) {
        return;
    }

    m_activePad = row;
    if (m_pads) {
        m_pads->setActivePad(row);
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

void SeqPageWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);

    QPainter p(this);

    Theme::paintBackground(p, rect());
    Theme::applyRenderHints(p);
    const bool lite = Theme::liteMode();

    const QRectF grid = gridRect();
    const int cols = 64;
    const int rows = 8;
    const float labelW = Theme::pxF(34.0f);
    const QRectF gridArea(grid.left() + labelW, grid.top(),
                          grid.width() - labelW, grid.height());
    const float cellW = gridArea.width() / cols;
    const float cellH = gridArea.height() / rows;

    p.setBrush(QColor(28, 28, 32));
    p.setPen(QPen(QColor(70, 70, 80), 1.0));
    p.drawRoundedRect(grid, Theme::px(6), Theme::px(6));

    // Row labels
    p.setFont(Theme::baseFont(9, QFont::DemiBold));
    for (int row = 0; row < rows; ++row) {
        const QRectF labelRect(grid.left(), gridArea.top() + row * cellH,
                               labelW - Theme::px(4), cellH);
        const bool active = (row == m_activePad);
        p.setPen(active ? Theme::accent() : Theme::textMuted());
        p.drawText(labelRect, Qt::AlignCenter, QString("A%1").arg(row + 1));
    }

    // Grid and notes.
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const QRectF cell(gridArea.left() + col * cellW, gridArea.top() + row * cellH,
                              cellW, cellH);
            const QRectF box = cell.adjusted(Theme::px(6), Theme::px(6),
                                             -Theme::px(6), -Theme::px(6));
            const int step = col;

            p.setPen(QPen(QColor(60, 60, 70), 1.0));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(box, Theme::px(4), Theme::px(4));

            if (!lite) {
                for (int pad = 0; pad < 8; ++pad) {
                    if (pad == m_activePad) {
                        continue;
                    }
                    if (!m_steps[pad][step]) {
                        continue;
                    }
                    QColor ghost = m_padColors[pad];
                    ghost.setAlpha(70);
                    const QRectF ghostBox = box.adjusted(Theme::px(6), Theme::px(6),
                                                         -Theme::px(6), -Theme::px(6));
                    p.setBrush(ghost);
                    p.setPen(Qt::NoPen);
                    p.drawRoundedRect(ghostBox, Theme::px(4), Theme::px(4));
                }
            }

            if (m_steps[m_activePad][step]) {
                p.setBrush(QColor(180, 70, 100));
                p.setPen(Qt::NoPen);
                p.drawRoundedRect(box.adjusted(Theme::px(4), Theme::px(4),
                                               -Theme::px(4), -Theme::px(4)),
                                  Theme::px(4), Theme::px(4));
            }

            if (step == m_playStep) {
                p.setPen(QPen(Theme::accentAlt(), 2.0));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(cell.adjusted(Theme::px(2), Theme::px(2),
                                                -Theme::px(2), -Theme::px(2)),
                                  Theme::px(6), Theme::px(6));
            }
        }
    }
}
