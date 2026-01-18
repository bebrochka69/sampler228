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
    const int margin = 24;
    const int headerHeight = 24;
    const float heightRatio = 0.54f;
    const int top = margin + headerHeight + 6;
    return QRectF(margin, top, width() - 2 * margin, height() * heightRatio);
}

QRectF SeqPageWidget::padsRect() const {
    const QRectF grid = gridRect();
    return QRectF(grid.left(), grid.bottom() + 28, grid.width(), 110);
}

int SeqPageWidget::stepIntervalMs() const {
    const int bpm = m_pads ? m_pads->bpm() : m_bpm;
    const int ms = 60000 / qMax(1, bpm) / 4;
    return qMax(20, ms);
}

void SeqPageWidget::togglePlayback() {
    m_playing = !m_playing;
    if (m_playing) {
        m_playStep = 0;
        triggerStep(m_playStep);
        m_playTimer.setInterval(stepIntervalMs());
        m_playTimer.start();
    } else {
        m_playTimer.stop();
        if (m_pads) {
            m_pads->stopAll();
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
    const QRectF pads = padsRect();

    if (pads.contains(pos)) {
        const int cols = 8;
        const float padW = pads.width() / cols;
        const int idx = static_cast<int>((pos.x() - pads.left()) / padW);
        if (idx >= 0 && idx < 8) {
            m_activePad = idx;
            if (m_pads) {
                m_pads->setActivePad(idx);
            }
            update();
        }
        return;
    }

    const QRectF grid = gridRect();
    if (!grid.contains(pos)) {
        return;
    }

    const int cols = 16;
    const int rows = 4;
    const float cellW = grid.width() / cols;
    const float cellH = grid.height() / rows;

    const int col = static_cast<int>((pos.x() - grid.left()) / cellW);
    const int row = static_cast<int>((pos.y() - grid.top()) / cellH);
    const int step = row * cols + col;
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

void SeqPageWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);

    QPainter p(this);

    Theme::paintBackground(p, rect());
    const bool lite = Theme::liteMode();

    const QRectF headerRect(24, 18, width() - 48, 22);
    p.setPen(Theme::accent());
    p.setFont(Theme::condensedFont(12, QFont::Bold));
    p.drawText(headerRect, Qt::AlignLeft | Qt::AlignVCenter, "SEQ / 64 STEPS");
    p.setPen(Theme::textMuted());
    const int bpm = m_pads ? m_pads->bpm() : m_bpm;
    p.drawText(headerRect, Qt::AlignRight | Qt::AlignVCenter,
               QString("%1 BPM  %2").arg(bpm).arg(m_playing ? "PLAYING [SPACE]" : "STOPPED [SPACE]"));

    const QRectF grid = gridRect();
    const int cols = 16;
    const int rows = 4;
    const float cellW = grid.width() / cols;
    const float cellH = grid.height() / rows;

    const QColor groupA = Theme::bg1();
    const QColor groupB = Theme::withAlpha(Theme::stroke(), 32);

    // Background groups.
    if (!lite) {
        for (int col = 0; col < cols; ++col) {
            const int group = col / 4;
            const QColor groupColor = (group % 2 == 0) ? groupA : groupB;
            const QRectF groupRect(grid.left() + col * cellW, grid.top(), cellW, grid.height());
            p.fillRect(groupRect, groupColor);
        }
    } else {
        p.fillRect(grid, Theme::bg1());
    }

    // Grid and notes.
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const QRectF cell(grid.left() + col * cellW, grid.top() + row * cellH, cellW, cellH);
            const QRectF box = cell.adjusted(6, 6, -6, -6);
            const int step = row * cols + col;

            p.setPen(QPen(Theme::stroke(), 1.0));
            p.setBrush(Qt::NoBrush);
            p.drawRect(box);

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
                    const QRectF ghostBox = box.adjusted(6, 6, -6, -6);
                    p.setBrush(ghost);
                    p.setPen(Qt::NoPen);
                    p.drawRect(ghostBox);
                }
            }

            if (m_steps[m_activePad][step]) {
                p.setBrush(m_padColors[m_activePad]);
                p.setPen(Qt::NoPen);
                p.drawRect(box.adjusted(3, 3, -3, -3));
            }

            if (step == m_playStep) {
                p.setPen(QPen(Theme::accentAlt(), 2.0));
                p.setBrush(Qt::NoBrush);
                p.drawRect(cell.adjusted(2, 2, -2, -2));
            }
        }
    }

    // Pad input row.
    const QRectF pads = padsRect();
    const float padW = pads.width() / 8.0f;
    const float padH = 64.0f;
    p.setFont(Theme::baseFont(10, QFont::DemiBold));

    for (int i = 0; i < 8; ++i) {
        const QRectF padRect(pads.left() + i * padW + 6, pads.top() + 8, padW - 12, padH);
        const bool active = (i == m_activePad);
        p.setBrush(active ? m_padColors[i] : Theme::bg1());
        p.setPen(QPen(active ? Theme::accentAlt() : Theme::stroke(), 1.2));
        p.drawRect(padRect);

        p.setPen(active ? Theme::bg0() : Theme::textMuted());
        p.drawText(padRect, Qt::AlignCenter, QString("PAD %1").arg(i + 1));
    }

}
