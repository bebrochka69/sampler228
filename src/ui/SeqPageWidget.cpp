#include "SeqPageWidget.h"

#include <QKeyEvent>
#include <QLineF>
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

    m_animTimer.setTimerType(Qt::PreciseTimer);
    m_animTimer.setInterval(33);
    connect(&m_animTimer, &QTimer::timeout, this, [this]() {
        if (m_playing || m_waiting) {
            update();
        }
    });

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

    m_longPressTimer.setSingleShot(true);
    m_longPressTimer.setInterval(450);
    connect(&m_longPressTimer, &QTimer::timeout, this, [this]() {
        if (m_pressedPad >= 0 && m_pressOnLabel) {
            m_longPressTriggered = true;
            emit padMenuRequested(m_pressedPad);
        }
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
                m_animTimer.setInterval(33);
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
        if (!used && !m_pianoNotes[pad].isEmpty()) {
            used = true;
        }
        if (used && !m_pads->isLoaded(pad)) {
            continue;
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
    if (!m_playClock.isValid()) {
        m_playClock.start();
    } else {
        m_playClock.restart();
    }
    m_lastStepMs = 0;
    m_animTimer.setInterval(33);
    m_animTimer.start();
    update();
}

void SeqPageWidget::togglePlayback() {
    if (m_playing || m_waiting) {
        m_playing = false;
        m_waiting = false;
        m_readyTimer.stop();
        m_playTimer.stop();
        m_animTimer.stop();
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
    if (m_rendering) {
        m_renderStepCount++;
        if (m_renderStepCount >= m_renderStepsTotal) {
            m_rendering = false;
            m_playing = false;
            m_waiting = false;
            m_playTimer.stop();
            m_animTimer.stop();
            if (m_pads) {
                m_pads->stopAll();
            }
        }
    }
    if (m_playClock.isValid()) {
        m_lastStepMs = m_playClock.elapsed();
    }
    update();
}

void SeqPageWidget::triggerStep(int step) {
    if (!m_pads) {
        return;
    }
    if (m_metronomeEnabled && (step % 4 == 0)) {
        const bool accent = (step % 16 == 0);
        m_pads->triggerMetronome(accent);
    }
    for (int pad = 0; pad < 8; ++pad) {
        if (m_pads->isSynth(pad) && !m_pianoNotes[pad].isEmpty()) {
            const int baseMidi = 48;
            const int rows = 49;
            const auto &notes = m_pianoNotes[pad];
            for (const auto &note : notes) {
                if (note.start != step) {
                    continue;
                }
                const int midi = baseMidi + (rows - 1 - note.row);
                m_pads->triggerPadMidi(pad, midi, note.length);
            }
            continue;
        }
        if (m_steps[pad][step]) {
            m_pads->triggerPad(pad);
        }
    }
}

void SeqPageWidget::applyPianoSteps(int pad, const QVector<int> &steps) {
    if (pad < 0 || pad >= 8) {
        return;
    }
    for (int i = 0; i < 64; ++i) {
        m_steps[pad][i] = false;
    }
    for (int step : steps) {
        if (step >= 0 && step < 64) {
            m_steps[pad][step] = true;
        }
    }
    update();
}

QVector<int> SeqPageWidget::pianoSteps(int pad) const {
    QVector<int> steps;
    if (pad < 0 || pad >= 8) {
        return steps;
    }
    for (int i = 0; i < 64; ++i) {
        if (m_steps[pad][i]) {
            steps.push_back(i);
        }
    }
    return steps;
}

QVector<int> SeqPageWidget::pianoNotesData(int pad) const {
    QVector<int> data;
    if (pad < 0 || pad >= 8) {
        return data;
    }
    const auto &notes = m_pianoNotes[pad];
    data.reserve(notes.size() * 3);
    for (const auto &note : notes) {
        data.push_back(note.start);
        data.push_back(note.length);
        data.push_back(note.row);
    }
    return data;
}

void SeqPageWidget::setMetronomeEnabled(bool enabled) {
    m_metronomeEnabled = enabled;
}

void SeqPageWidget::renderToFile(const QString &path, int bars, int targetRate) {
    if (!m_pads || bars <= 0 || path.isEmpty()) {
        return;
    }
    const int steps = bars * 16;
    const int totalMs = steps * stepIntervalMs();
    if (!m_pads->startRecording(path, totalMs, targetRate)) {
        return;
    }
    if (m_playing || m_waiting) {
        m_playing = false;
        m_waiting = false;
        m_playTimer.stop();
        m_animTimer.stop();
        m_readyTimer.stop();
    }
    m_rendering = true;
    m_renderStepsTotal = steps;
    m_renderStepCount = 0;
    startPlayback();
}

void SeqPageWidget::applyPianoNotes(int pad, const QVector<int> &notesData) {
    if (pad < 0 || pad >= 8) {
        return;
    }
    auto &notes = m_pianoNotes[pad];
    notes.clear();
    for (int i = 0; i + 2 < notesData.size(); i += 3) {
        PianoNote note;
        note.start = qMax(0, notesData[i]);
        note.length = qMax(1, notesData[i + 1]);
        note.row = qBound(0, notesData[i + 2], 48);
        notes.push_back(note);
    }
    update();
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

    // Scrub playhead by dragging in header area (right side).
    if (pos.x() >= gridArea.left() && pos.x() <= gridArea.right() &&
        pos.y() >= grid.top() && pos.y() <= gridArea.top()) {
        const float cellW = gridArea.width() / cols;
        const int step = qBound(0, static_cast<int>((pos.x() - gridArea.left()) / cellW), cols - 1);
        m_playStep = step;
        if (m_playClock.isValid()) {
            m_lastStepMs = m_playClock.elapsed();
        }
        if (m_playing) {
            triggerStep(m_playStep);
        }
        m_scrubActive = true;
        update();
        return;
    }

    // Click on label column -> open edit/synth for that pad or long-press for piano roll.
    if (pos.x() < gridArea.left()) {
        const float cellH = gridArea.height() / rows;
        const int row = static_cast<int>((pos.y() - gridArea.top()) / cellH);
        if (row >= 0 && row < rows) {
            m_activePad = row;
            if (m_pads) {
                m_pads->setActivePad(row);
            }
            m_pressedPad = row;
            m_pressOnLabel = true;
            m_longPressTriggered = false;
            m_pressPos = pos;
            m_longPressTimer.start();
        }
        return;
    }
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

    m_pressOnLabel = false;
    m_pressedPad = -1;
    m_longPressTimer.stop();
    m_scrubActive = false;

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

void SeqPageWidget::mouseMoveEvent(QMouseEvent *event) {
    if (m_scrubActive) {
        const QRectF grid = gridRect();
        const float labelW = Theme::pxF(48.0f);
        const float headerH = Theme::pxF(24.0f);
        const QRectF gridArea(grid.left() + labelW, grid.top() + headerH,
                              grid.width() - labelW, grid.height() - headerH);
        const int cols = 64;
        const float cellW = gridArea.width() / cols;
        const int step = qBound(0, static_cast<int>((event->position().x() - gridArea.left()) / cellW),
                                cols - 1);
        m_playStep = step;
        if (m_playClock.isValid()) {
            m_lastStepMs = m_playClock.elapsed();
        }
        update();
        return;
    }
    if (!m_pressOnLabel || m_pressedPad < 0) {
        return;
    }
    const QPointF pos = event->position();
    if (QLineF(pos, m_pressPos).length() > Theme::pxF(6.0f)) {
        m_longPressTimer.stop();
    }
}

void SeqPageWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (m_scrubActive) {
        m_scrubActive = false;
        return;
    }
    if (!m_pressOnLabel || m_pressedPad < 0) {
        return;
    }

    m_longPressTimer.stop();
    if (!m_longPressTriggered) {
        if (event->modifiers().testFlag(Qt::ShiftModifier)) {
            emit padAssignRequested(m_pressedPad);
        } else {
            emit padOpenRequested(m_pressedPad);
        }
    }
    m_pressOnLabel = false;
    m_pressedPad = -1;
    m_longPressTriggered = false;
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

    // Mini piano-roll preview for synth pads.
    for (int row = 0; row < rows; ++row) {
        const auto &notes = m_pianoNotes[row];
        if (notes.isEmpty()) {
            continue;
        }
        const float rowTop = gridArea.top() + row * cellH;
        const QRectF rowRect(gridArea.left(), rowTop, gridArea.width(), cellH);
        p.save();
        p.setClipRect(rowRect.adjusted(Theme::px(1), Theme::px(2), -Theme::px(1), -Theme::px(2)));
        const QColor baseColor = Theme::withAlpha(m_padColors[row], 140);
        const float barH = Theme::pxF(3.0f);
        for (const auto &note : notes) {
            const float x = gridArea.left() + note.start * cellW;
            const float w = qMax(cellW, note.length * cellW);
            const float pitchFrac = qBound(0.0f, static_cast<float>(note.row) / 48.0f, 1.0f);
            const float y = rowTop + (1.0f - pitchFrac) * (cellH - barH);
            QRectF r(x + Theme::pxF(1.0f), y, w - Theme::pxF(2.0f), barH);
            p.setBrush(baseColor);
            p.setPen(Qt::NoPen);
            p.drawRect(r);
        }
        p.restore();
    }

    // Steps for all pads.
    for (int row = 0; row < rows; ++row) {
        if (!m_pianoNotes[row].isEmpty()) {
            continue;
        }
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

    // Playhead (smooth)
    if (m_playing || m_waiting) {
        float frac = 0.0f;
        const int stepMs = stepIntervalMs();
        if (m_playClock.isValid() && stepMs > 0) {
            const qint64 elapsed = m_playClock.elapsed() - m_lastStepMs;
            frac = qBound(0.0f, static_cast<float>(elapsed) / static_cast<float>(stepMs), 1.0f);
        }
        const float x = gridArea.left() + (m_playStep + frac) * cellW;
        p.setPen(QPen(Theme::accentAlt(), 2.0));
        p.drawLine(QPointF(x, gridArea.top()), QPointF(x, gridArea.bottom()));
    }
}
