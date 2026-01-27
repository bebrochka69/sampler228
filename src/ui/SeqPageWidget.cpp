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
    const int margin = Theme::px(18);
    const int top = Theme::px(64);
    const float heightRatio = 0.56f;
    return QRectF(margin, top, width() - 2 * margin, height() * heightRatio);
}

QRectF SeqPageWidget::padsRect() const {
    const QRectF grid = gridRect();
    return QRectF(grid.left(), grid.bottom() + Theme::px(18), grid.width(), Theme::px(110));
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
    Theme::applyRenderHints(p);
    const bool lite = Theme::liteMode();

    // Top transport bar
    const QRectF topBar(Theme::px(14), Theme::px(10), width() - Theme::px(28), Theme::px(40));
    p.setBrush(QColor(40, 38, 46));
    p.setPen(QPen(QColor(70, 70, 80), 1.0));
    p.drawRoundedRect(topBar, Theme::px(8), Theme::px(8));

    // Play + Record
    QRectF playRect(topBar.left() + Theme::px(8), topBar.top() + Theme::px(8),
                    Theme::px(24), Theme::px(24));
    QRectF recRect(playRect.right() + Theme::px(6), playRect.top(),
                   Theme::px(24), Theme::px(24));
    p.setBrush(QColor(30, 30, 30));
    p.setPen(QPen(QColor(90, 90, 100), 1.0));
    p.drawRoundedRect(playRect, Theme::px(6), Theme::px(6));
    p.drawRoundedRect(recRect, Theme::px(6), Theme::px(6));
    QPolygonF tri;
    tri << QPointF(playRect.left() + Theme::px(8), playRect.top() + Theme::px(6))
        << QPointF(playRect.right() - Theme::px(7), playRect.center().y())
        << QPointF(playRect.left() + Theme::px(8), playRect.bottom() - Theme::px(6));
    p.setBrush(m_playing ? Theme::accentAlt() : Theme::accent());
    p.setPen(Qt::NoPen);
    p.drawPolygon(tri);
    p.setBrush(QColor(230, 80, 100));
    p.drawEllipse(recRect.center(), Theme::px(6), Theme::px(6));

    // Bars dropdown
    QRectF barsRect(recRect.right() + Theme::px(10), playRect.top(),
                    Theme::px(86), Theme::px(24));
    p.setBrush(QColor(30, 30, 30));
    p.setPen(QPen(QColor(90, 90, 100), 1.0));
    p.drawRoundedRect(barsRect, Theme::px(6), Theme::px(6));
    p.setPen(Theme::accent());
    p.setFont(Theme::baseFont(9, QFont::DemiBold));
    p.drawText(barsRect, Qt::AlignCenter, "4 BARS");

    // Mini keyboard (static)
    QRectF kbRect(barsRect.right() + Theme::px(8), barsRect.top(),
                  Theme::px(90), Theme::px(24));
    p.setBrush(QColor(20, 20, 20));
    p.setPen(QPen(QColor(100, 100, 110), 1.0));
    p.drawRoundedRect(kbRect, Theme::px(4), Theme::px(4));
    p.setPen(QPen(QColor(220, 220, 220), 1.0));
    for (int i = 1; i < 7; ++i) {
        const float x = kbRect.left() + i * (kbRect.width() / 7.0f);
        p.drawLine(QPointF(x, kbRect.top() + 2), QPointF(x, kbRect.bottom() - 2));
    }

    // Timeline strip
    QRectF tlRect(topBar.right() - Theme::px(210), barsRect.top(),
                  Theme::px(180), Theme::px(24));
    p.setBrush(QColor(55, 55, 60));
    p.setPen(QPen(QColor(90, 90, 100), 1.0));
    p.drawRoundedRect(tlRect, Theme::px(6), Theme::px(6));
    p.setPen(QPen(QColor(170, 70, 90), Theme::pxF(3.0f)));
    for (int i = 0; i < 8; ++i) {
        const float x = tlRect.left() + Theme::px(10) + i * Theme::px(20);
        p.drawLine(QPointF(x, tlRect.center().y()), QPointF(x + Theme::px(8), tlRect.center().y()));
    }

    const QRectF grid = gridRect();
    const int cols = 16;
    const int rows = 4;
    const float cellW = grid.width() / cols;
    const float cellH = grid.height() / rows;

    p.setBrush(QColor(28, 28, 32));
    p.setPen(QPen(QColor(70, 70, 80), 1.0));
    p.drawRoundedRect(grid, Theme::px(8), Theme::px(8));

    // Grid and notes.
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const QRectF cell(grid.left() + col * cellW, grid.top() + row * cellH, cellW, cellH);
            const QRectF box = cell.adjusted(Theme::px(6), Theme::px(6),
                                             -Theme::px(6), -Theme::px(6));
            const int step = row * cols + col;

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

    // Pad input row.
    const QRectF pads = padsRect();
    const float padW = pads.width() / 8.0f;
    const float padH = Theme::pxF(58.0f);
    p.setFont(Theme::baseFont(10, QFont::DemiBold));

    for (int i = 0; i < 8; ++i) {
        const QRectF padRect(pads.left() + i * padW + Theme::px(6),
                             pads.top() + Theme::px(8),
                             padW - Theme::px(12), padH);
        const bool active = (i == m_activePad);
        p.setBrush(active ? Theme::accent() : Theme::bg1());
        p.setPen(QPen(active ? Theme::accentAlt() : Theme::stroke(), 1.2));
        p.drawRoundedRect(padRect, Theme::px(10), Theme::px(10));

        p.setPen(active ? Theme::bg0() : Theme::textMuted());
        p.drawText(padRect, Qt::AlignCenter, QString("PAD %1").arg(i + 1));
    }

    // Bottom toolbar (static)
    QRectF bottomBar(Theme::px(14), height() - Theme::px(46), width() - Theme::px(28), Theme::px(34));
    p.setBrush(QColor(40, 38, 46));
    p.setPen(QPen(QColor(70, 70, 80), 1.0));
    p.drawRoundedRect(bottomBar, Theme::px(6), Theme::px(6));
    p.setPen(Theme::accent());
    p.setFont(Theme::baseFont(9, QFont::DemiBold));
    p.drawText(bottomBar.adjusted(Theme::px(10), 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter,
               "SNAP: 1/16");
}
