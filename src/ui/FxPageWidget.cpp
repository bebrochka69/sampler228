#include "FxPageWidget.h"

#include <QHideEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QShowEvent>
#include <QtGlobal>
#include <cmath>

#include "PadBank.h"
#include "Theme.h"

namespace {
float clamp01(float v) {
    return std::max(0.0f, std::min(1.0f, v));
}

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

float hash2(int x, int y, int t) {
    const int n = x * 374761393 + y * 668265263 + t * 69069;
    const int nn = (n ^ (n >> 13)) * 1274126177;
    return (nn & 0x7fffffff) / static_cast<float>(0x7fffffff);
}
}  // namespace

FxPageWidget::FxPageWidget(PadBank *pads, QWidget *parent) : QWidget(parent), m_pads(pads) {
    setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);

    m_tracks = {
        {"MASTER", QVector<FxInsert>(4)},
        {"A", QVector<FxInsert>(4)},
        {"B", QVector<FxInsert>(4)},
        {"C", QVector<FxInsert>(4)},
        {"D", QVector<FxInsert>(4)},
        {"E", QVector<FxInsert>(4)},
    };

    m_effects = {"reverb", "comp", "dist", "lofi", "cassette", "chorus", "eq", "sidechan"};

    m_animTimer.setInterval(16);
    m_animTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_animTimer, &QTimer::timeout, this, &FxPageWidget::advanceAnimation);
}

void FxPageWidget::assignEffect(int effectIndex) {
    if (effectIndex < 0 || effectIndex >= m_effects.size()) {
        return;
    }
    if (m_selectedTrack < 0 || m_selectedTrack >= m_tracks.size()) {
        return;
    }
    FxTrack &track = m_tracks[m_selectedTrack];
    if (m_selectedSlot < 0 || m_selectedSlot >= track.inserts.size()) {
        return;
    }
    track.inserts[m_selectedSlot].effect = m_effects[effectIndex];
    track.inserts[m_selectedSlot].p1 = 0.5f;
    track.inserts[m_selectedSlot].p2 = 0.5f;
    track.inserts[m_selectedSlot].p3 = 0.5f;
    syncBusEffects(m_selectedTrack);
    update();
}

void FxPageWidget::swapSlot(int trackIndex, int a, int b) {
    if (trackIndex < 0 || trackIndex >= m_tracks.size()) {
        return;
    }
    FxTrack &track = m_tracks[trackIndex];
    if (a < 0 || b < 0 || a >= track.inserts.size() || b >= track.inserts.size()) {
        return;
    }
    track.inserts.swapItemsAt(a, b);
    syncBusEffects(trackIndex);
    update();
}

void FxPageWidget::keyPressEvent(QKeyEvent *event) {
    const int key = event->key();
    const bool ctrl = event->modifiers().testFlag(Qt::ControlModifier);

    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        if (!m_showMenu) {
            m_showMenu = true;
        } else {
            assignEffect(m_selectedEffect);
            m_showMenu = false;
        }
        update();
        return;
    }
    if (key == Qt::Key_Escape) {
        if (m_showMenu) {
            m_showMenu = false;
            update();
            return;
        }
    }

    if (m_showMenu) {
        const int cols = 4;
        const int rows = 2;
        int row = m_selectedEffect / cols;
        int col = m_selectedEffect % cols;
        if (key == Qt::Key_Left) {
            col = (col - 1 + cols) % cols;
        } else if (key == Qt::Key_Right) {
            col = (col + 1) % cols;
        } else if (key == Qt::Key_Up) {
            row = (row - 1 + rows) % rows;
        } else if (key == Qt::Key_Down) {
            row = (row + 1) % rows;
        } else {
            return;
        }
        int next = row * cols + col;
        if (next >= m_effects.size()) {
            next = m_effects.size() - 1;
        }
        m_selectedEffect = qMax(0, next);
        update();
        return;
    }

    if (key == Qt::Key_Up) {
        if (ctrl) {
            swapSlot(m_selectedTrack, m_selectedSlot, m_selectedSlot - 1);
        } else {
            m_selectedSlot = qMax(0, m_selectedSlot - 1);
            update();
        }
        return;
    }
    if (key == Qt::Key_Down) {
        if (ctrl) {
            swapSlot(m_selectedTrack, m_selectedSlot, m_selectedSlot + 1);
        } else {
            m_selectedSlot = qMin(m_selectedSlot + 1, m_tracks[m_selectedTrack].inserts.size() - 1);
            update();
        }
        return;
    }

    if (key == Qt::Key_Delete || key == Qt::Key_Backspace) {
        FxTrack &track = m_tracks[m_selectedTrack];
        if (m_selectedSlot >= 0 && m_selectedSlot < track.inserts.size()) {
            track.inserts[m_selectedSlot].effect.clear();
            syncBusEffects(m_selectedTrack);
            update();
        }
        return;
    }

    if (key == Qt::Key_1 || key == Qt::Key_2 || key == Qt::Key_3) {
        m_selectedParam = key - Qt::Key_1;
        update();
        return;
    }
    if (key == Qt::Key_Minus || key == Qt::Key_Left) {
        if (ctrl) {
            m_selectedTrack = (m_selectedTrack - 1 + m_tracks.size()) % m_tracks.size();
            update();
            return;
        }
        FxTrack &track = m_tracks[m_selectedTrack];
        if (m_selectedSlot >= 0 && m_selectedSlot < track.inserts.size()) {
            FxInsert &slot = track.inserts[m_selectedSlot];
            if (m_selectedParam == 0) {
                slot.p1 = qBound(0.0f, slot.p1 - 0.05f, 1.0f);
            } else if (m_selectedParam == 1) {
                slot.p2 = qBound(0.0f, slot.p2 - 0.05f, 1.0f);
            } else {
                slot.p3 = qBound(0.0f, slot.p3 - 0.05f, 1.0f);
            }
            syncBusEffects(m_selectedTrack);
            update();
        }
        return;
    }
    if (key == Qt::Key_Plus || key == Qt::Key_Equal || key == Qt::Key_Right) {
        if (ctrl) {
            m_selectedTrack = (m_selectedTrack + 1) % m_tracks.size();
            update();
            return;
        }
        FxTrack &track = m_tracks[m_selectedTrack];
        if (m_selectedSlot >= 0 && m_selectedSlot < track.inserts.size()) {
            FxInsert &slot = track.inserts[m_selectedSlot];
            if (m_selectedParam == 0) {
                slot.p1 = qBound(0.0f, slot.p1 + 0.05f, 1.0f);
            } else if (m_selectedParam == 1) {
                slot.p2 = qBound(0.0f, slot.p2 + 0.05f, 1.0f);
            } else {
                slot.p3 = qBound(0.0f, slot.p3 + 0.05f, 1.0f);
            }
            syncBusEffects(m_selectedTrack);
            update();
        }
        return;
    }
}

void FxPageWidget::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    setFocus(Qt::OtherFocusReason);
    if (!m_clock.isValid()) {
        m_clock.start();
    }
    if (!m_animTimer.isActive()) {
        m_animTimer.start();
    }
}

void FxPageWidget::hideEvent(QHideEvent *event) {
    QWidget::hideEvent(event);
    m_animTimer.stop();
}

void FxPageWidget::advanceAnimation() {
    if (!m_clock.isValid()) {
        m_clock.start();
    }
    m_animTime = m_clock.elapsed() / 1000.0f;

    // Sidechain pulse smoothing (used by visual only).
    FxInsert slot;
    if (m_selectedTrack >= 0 && m_selectedTrack < m_tracks.size()) {
        const FxTrack &track = m_tracks[m_selectedTrack];
        if (m_selectedSlot >= 0 && m_selectedSlot < track.inserts.size()) {
            slot = track.inserts[m_selectedSlot];
        }
    }

    const QString effectName = slot.effect.toLower();
    if (effectName == "sidechan") {
        const float level = m_pads ? m_pads->busMeter(m_selectedTrack) : 0.0f;
        const float threshold = 0.08f + clamp01(slot.p1) * 0.6f;
        const float target = (level > threshold) ? clamp01(slot.p2) : 0.0f;
        const float attack = 0.35f;
        const float release = 0.08f + clamp01(slot.p3) * 0.2f;
        const float coeff = (target > m_sidechainValue) ? attack : release;
        m_sidechainValue = m_sidechainValue + (target - m_sidechainValue) * coeff;
    } else {
        m_sidechainValue *= 0.85f;
    }

    if (effectName == "comp") {
        const float level = m_pads ? m_pads->busMeter(m_selectedTrack) : 0.0f;
        const float threshold = 0.08f + clamp01(slot.p1) * 0.6f;
        float amount = 0.0f;
        if (level > threshold) {
            amount = (level - threshold) / qMax(0.001f, (1.0f - threshold));
        }
        const float target = clamp01(amount) * (0.4f + clamp01(slot.p2) * 0.6f);
        const float attack = 0.18f;
        const float release = 0.04f + clamp01(slot.p3) * 0.08f;
        const float coeff = (target > m_compValue) ? attack : release;
        m_compValue = m_compValue + (target - m_compValue) * coeff;
    } else {
        m_compValue *= 0.9f;
    }

    update();
}

void FxPageWidget::mousePressEvent(QMouseEvent *event) {
    setFocus(Qt::MouseFocusReason);
    const QPointF pos = event->position();

    if (m_showMenu) {
        for (const FxEffectHit &hit : m_effectHits) {
            if (hit.rect.contains(pos)) {
                m_selectedEffect = hit.index;
                assignEffect(hit.index);
                m_showMenu = false;
                return;
            }
        }
        m_showMenu = false;
        update();
        return;
    }

    for (const FxInsertHit &hit : m_slotHits) {
        if (hit.rect.contains(pos)) {
            m_selectedTrack = hit.track;
            m_selectedSlot = hit.slot;
            m_showMenu = true;
            update();
            return;
        }
    }

    m_showMenu = false;
    update();
}

void FxPageWidget::syncBusEffects(int trackIndex) {
    if (!m_pads) {
        return;
    }
    if (trackIndex < 0 || trackIndex >= m_tracks.size()) {
        return;
    }
    QVector<PadBank::BusEffect> ids;
    const FxTrack &track = m_tracks[trackIndex];
    for (const FxInsert &slot : track.inserts) {
        if (slot.effect.isEmpty()) {
            continue;
        }
        const int idx = m_effects.indexOf(slot.effect);
        if (idx >= 0) {
            PadBank::BusEffect fx;
            fx.type = idx + 1;
            fx.p1 = slot.p1;
            fx.p2 = slot.p2;
            fx.p3 = slot.p3;
            ids.push_back(fx);
        }
    }
    m_pads->setBusEffects(trackIndex, ids);
}

void FxPageWidget::drawEffectPreview(QPainter &p, const QRectF &rect, const FxInsert &slot,
                                     float level) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF r = rect.adjusted(10, 10, -10, -10);
    const QPointF c = r.center();
    const float w = r.width();
    const float h = r.height();
    const float t = m_animTime;

    const float p1 = clamp01(slot.p1);
    const float p2 = clamp01(slot.p2);
    const float p3 = clamp01(slot.p3);
    const QString fx = slot.effect.toLower();

    if (fx.isEmpty()) {
        p.setPen(Theme::textMuted());
        p.setFont(Theme::baseFont(10, QFont::DemiBold));
        p.drawText(r, Qt::AlignCenter, "NO EFFECT");
        p.restore();
        return;
    }

    if (fx != "comp") {
    // Preview canvas (black, OP-1 style).
    p.fillRect(rect, QColor(0, 0, 0, 255));
    p.setPen(QPen(QColor(40, 40, 60, 200), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r, 10, 10);
    }

    if (fx == "comp") {
        // OP-1 style compressor: rubber air bag between plates.
        const QColor lineColor(230, 230, 245, 220);
        p.setPen(QPen(lineColor, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);

        const float breathe = 0.02f * std::sin(t * 0.25f);
        const float squeeze = clamp01(m_compValue);
        const float bagW = w * 0.62f;
        const float baseH = h * (0.28f + p3 * 0.1f);
        const float bagH = baseH * (1.0f - squeeze * 0.6f) * (1.0f + breathe);
        const float bagX = c.x() - bagW * 0.5f;
        const float bagY = c.y() - bagH * 0.5f;

        // Plates.
        const float gap = 8.0f - squeeze * 5.0f;
        const float plateW = bagW * 1.15f;
        const float plateX = c.x() - plateW * 0.5f;
        const float topY = bagY - gap;
        const float bottomY = bagY + bagH + gap;
        p.drawLine(QPointF(plateX, topY), QPointF(plateX + plateW, topY));
        p.drawLine(QPointF(plateX, bottomY), QPointF(plateX + plateW, bottomY));

        // Side limiters.
        const float limiterH = bagH * 0.35f;
        p.drawLine(QPointF(plateX + 4, c.y() - limiterH), QPointF(plateX + 4, c.y() + limiterH));
        p.drawLine(QPointF(plateX + plateW - 4, c.y() - limiterH),
                   QPointF(plateX + plateW - 4, c.y() + limiterH));

        // Bag outline with slight irregularity.
        QPainterPath bag;
        const int points = 24;
        for (int i = 0; i < points; ++i) {
            const float ang = (static_cast<float>(i) / points) * 2.0f * static_cast<float>(M_PI);
            const float irregular = 1.0f + 0.04f * std::sin(ang * 3.0f + t * 0.6f);
            const float rx = (bagW * 0.5f) * irregular;
            const float ry = (bagH * 0.5f) * (1.0f + 0.03f * std::cos(ang * 2.0f + t * 0.4f));
            const QPointF pt(c.x() + std::cos(ang) * rx, c.y() + std::sin(ang) * ry);
            if (i == 0) {
                bag.moveTo(pt);
            } else {
                bag.lineTo(pt);
            }
        }
        bag.closeSubpath();
        p.drawPath(bag);

        // Airflow lines inside.
        p.save();
        p.setClipPath(bag);
        const int lines = 3 + static_cast<int>(squeeze * 4.0f);
        for (int i = 0; i < lines; ++i) {
            const float y = lerp(bagY + bagH * 0.2f, bagY + bagH * 0.8f,
                                 static_cast<float>(i) / qMax(1, lines - 1));
            QPainterPath flow;
            flow.moveTo(bagX + bagW * 0.12f, y);
            for (int k = 1; k <= 4; ++k) {
                const float x = bagX + bagW * (0.12f + k * 0.2f);
                const float amp = 3.0f + squeeze * 4.0f;
                const float dy = std::sin(t * 0.8f + k * 1.2f + i) * amp;
                flow.lineTo(x, y + dy);
            }
            p.drawPath(flow);
        }
        // Subtle pleat lines.
        p.setPen(QPen(QColor(210, 210, 235, 140), 1.0));
        for (int i = 0; i < 5; ++i) {
            const float y = bagY + bagH * (0.15f + i * 0.17f);
            p.drawLine(QPointF(bagX + bagW * 0.18f, y),
                       QPointF(bagX + bagW * 0.82f, y));
        }
        p.restore();
    } else if (fx == "dist") {
        // Overdriven loudspeaker.
        const float baseR = qMin(w, h) * 0.26f;
        const float jag = baseR * (0.06f + p1 * 0.25f);
        const float wobble = std::sin(t * (1.2f + p2 * 2.0f));
        const float coneR = baseR * (0.55f + p2 * 0.12f);
        const float domeR = baseR * (0.18f + p3 * 0.08f);

        QPainterPath rim;
        const int points = 28;
        for (int i = 0; i < points; ++i) {
            const float ang = (static_cast<float>(i) / points) * 2.0f * static_cast<float>(M_PI);
            const float rJ = baseR + jag * std::sin(ang * 5.0f + wobble * 2.0f);
            const QPointF pt(c.x() + std::cos(ang) * rJ, c.y() + std::sin(ang) * rJ);
            if (i == 0) {
                rim.moveTo(pt);
            } else {
                rim.lineTo(pt);
            }
        }
        rim.closeSubpath();
        p.setPen(QPen(QColor(255, 140, 180, 220), 1.8));
        p.setBrush(Qt::NoBrush);
        p.drawPath(rim);

        p.setPen(QPen(QColor(255, 170, 210, 200), 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(c, coneR, coneR * 0.85f);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(c, domeR, domeR);

        const int bolts = 6;
        for (int i = 0; i < bolts; ++i) {
            const float ang = (static_cast<float>(i) / bolts) * 2.0f * static_cast<float>(M_PI);
            const QPointF bolt(c.x() + std::cos(ang) * baseR * 1.05f,
                               c.y() + std::sin(ang) * baseR * 1.05f);
            p.setBrush(QColor(255, 200, 210, 200));
            p.setPen(Qt::NoPen);
            p.drawEllipse(bolt, 1.6f, 1.6f);
        }

        // Cone ribs.
        p.setPen(QPen(QColor(255, 150, 190, 120), 1.0));
        for (int i = 0; i < 4; ++i) {
            const float ang = static_cast<float>(i) / 4.0f * static_cast<float>(M_PI);
            p.drawLine(QPointF(c.x() - std::cos(ang) * coneR, c.y() - std::sin(ang) * coneR * 0.85f),
                       QPointF(c.x() + std::cos(ang) * coneR, c.y() + std::sin(ang) * coneR * 0.85f));
        }
    } else if (fx == "lofi") {
        // Broken memory screen.
        const QRectF screen = r.adjusted(10, 14, -10, -18);
        p.setPen(QPen(QColor(140, 255, 210, 200), 1.4));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(screen, 8, 8);

        const int cols = 10;
        const int rows = 6;
        const float dropProb = 0.08f + p1 * 0.55f;
        const int phase = static_cast<int>(t * (1.5f + p2 * 6.0f));
        const float cellW = screen.width() / cols;
        const float cellH = screen.height() / rows;
        for (int y = 0; y < rows; ++y) {
            for (int x = 0; x < cols; ++x) {
                const float hsh = hash2(x, y, phase);
                if (hsh < dropProb) {
                    continue;
                }
                QColor col(140, 255, 210, static_cast<int>((0.12f + hsh * 0.4f) * 255));
                p.setPen(QPen(col, 1.0));
                p.setBrush(Qt::NoBrush);
                p.drawRect(QRectF(screen.left() + x * cellW, screen.top() + y * cellH,
                                  cellW - 2.0f, cellH - 2.0f));
            }
        }

        // Broken scanline.
        p.setPen(QPen(QColor(255, 160, 190, 160), 1.6));
        const float scanY = screen.top() + screen.height() * (0.25f + p3 * 0.4f);
        p.drawLine(QPointF(screen.left() + 6, scanY),
                   QPointF(screen.right() - 6, scanY));
        p.setPen(QPen(QColor(90, 110, 140, 180), 1.2));
        p.drawLine(QPointF(screen.left() + 14, scanY + 6),
                   QPointF(screen.right() - 40, scanY + 6));

        // Bit rot blocks.
        p.setPen(QPen(QColor(255, 140, 180, 140), 1.0));
        p.drawLine(QPointF(screen.left() + 10, screen.top() + 10),
                   QPointF(screen.left() + 28, screen.top() + 10));
        p.drawLine(QPointF(screen.right() - 40, screen.bottom() - 14),
                   QPointF(screen.right() - 18, screen.bottom() - 14));
    } else if (fx == "eq") {
        // Living creature line (fish/snake).
        const float low = lerp(0.25f, 0.85f, p1);
        const float mid = lerp(0.2f, 0.9f, p2);
        const float high = lerp(0.25f, 0.85f, p3);
        const float sway = std::sin(t * 0.6f) * 0.05f;
        const QPointF head(r.left(), r.center().y() - h * (low * 0.3f + sway));
        const QPointF midPt(c.x(), r.center().y() - h * (mid * 0.4f - sway));
        const QPointF tail(r.right(), r.center().y() - h * (high * 0.3f + sway * 0.6f));
        QPainterPath body(head);
        body.cubicTo(QPointF(r.left() + w * 0.25f, head.y() - h * 0.2f),
                     QPointF(c.x() - w * 0.1f, midPt.y() + h * 0.15f), midPt);
        body.cubicTo(QPointF(c.x() + w * 0.1f, midPt.y() - h * 0.15f),
                     QPointF(r.right() - w * 0.25f, tail.y() + h * 0.18f), tail);
        p.setPen(QPen(QColor(140, 255, 210, 220), 2.0, Qt::SolidLine, Qt::RoundCap,
                      Qt::RoundJoin));
        p.drawPath(body);

        p.setBrush(QColor(140, 255, 210, 200));
        p.setPen(Qt::NoPen);
        p.drawEllipse(head + QPointF(6, -2), 3, 3);  // eye
        p.drawEllipse(midPt, 4, 4);
        p.drawEllipse(tail + QPointF(-6, 2), 3, 3);

        // Fin / ripple accents.
        p.setPen(QPen(QColor(140, 255, 210, 120), 1.0));
        p.drawLine(midPt + QPointF(-10, 6), midPt + QPointF(-2, 14));
        p.drawLine(midPt + QPointF(8, -6), midPt + QPointF(16, -14));
    } else if (fx == "cassette") {
        // Cassette shell.
        const QRectF shell = r.adjusted(10, 14, -10, -18);
        p.setPen(QPen(QColor(200, 180, 255, 200), 1.4));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(shell, 10, 10);

        const float reelR = qMin(w, h) * 0.14f;
        const QPointF left(shell.left() + shell.width() * 0.3f, shell.center().y());
        const QPointF right(shell.right() - shell.width() * 0.3f, shell.center().y());
        p.setPen(QPen(QColor(210, 200, 255, 200), 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(left, reelR, reelR);
        p.drawEllipse(right, reelR, reelR);

        const float angle = t * (0.4f + p2 * 1.4f) * 2.0f * static_cast<float>(M_PI);
        p.setPen(QPen(QColor(220, 210, 255, 220), 1.4));
        p.drawLine(left, QPointF(left.x() + std::cos(angle) * reelR * 0.9f,
                                 left.y() + std::sin(angle) * reelR * 0.9f));
        p.drawLine(right, QPointF(right.x() + std::cos(-angle) * reelR * 0.9f,
                                  right.y() + std::sin(-angle) * reelR * 0.9f));

        const float tapeY = shell.center().y() + std::sin(t * 1.2f) * (2.0f + p1 * 3.0f);
        p.setPen(QPen(QColor(255, 200, 180, 200), 1.6));
        p.drawLine(QPointF(left.x() + reelR, tapeY), QPointF(right.x() - reelR, tapeY));

        p.setBrush(QColor(200, 180, 255, 140));
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(shell.left() + 14, shell.top() + 12), 2.0f, 2.0f);
        p.drawEllipse(QPointF(shell.right() - 14, shell.top() + 12), 2.0f, 2.0f);

        // Window and label.
        p.setPen(QPen(QColor(140, 220, 255, 160), 1.0));
        p.setBrush(Qt::NoBrush);
        const QRectF window(shell.left() + shell.width() * 0.22f, shell.bottom() - 22,
                            shell.width() * 0.56f, 10);
        p.drawRoundedRect(window, 4, 4);
    } else if (fx == "chorus") {
        // Character with echoes.
        const int copies = 2 + static_cast<int>(p3 * 3.0f);
        const float depth = w * (0.04f + p1 * 0.12f);
        const float rate = 0.4f + p2 * 1.2f;
        auto drawDrop = [&](const QPointF &center, float alpha) {
            QPainterPath drop;
            drop.moveTo(center.x(), center.y() - 18);
            drop.cubicTo(center.x() + 12, center.y() - 8, center.x() + 10, center.y() + 10,
                         center.x(), center.y() + 16);
            drop.cubicTo(center.x() - 10, center.y() + 10, center.x() - 12, center.y() - 8,
                         center.x(), center.y() - 18);
            p.setPen(QPen(QColor(140, 220, 255, static_cast<int>(alpha * 255)), 1.2));
            p.setBrush(Qt::NoBrush);
            p.drawPath(drop);
        };
        for (int i = copies; i >= 0; --i) {
            const float phase = static_cast<float>(i) / (copies + 1) * 2.0f * static_cast<float>(M_PI);
            const float dx = std::cos(t * rate + phase) * depth;
            const float dy = std::sin(t * rate * 1.1f + phase) * depth * 0.4f;
            const float alpha = 0.12f + 0.16f * (static_cast<float>(copies - i) / (copies + 1));
            drawDrop(QPointF(c.x() + dx, c.y() + dy), alpha);
        }
    } else if (fx == "reverb") {
        // Room/arches depth.
        const int layers = 3 + static_cast<int>(p2 * 4.0f);
        const float spread = w * (0.08f + p1 * 0.2f);
        for (int i = 0; i < layers; ++i) {
            const float f = static_cast<float>(i + 1) / (layers + 1);
            const float inset = spread * f;
            const QRectF arch = r.adjusted(inset, inset * 0.6f, -inset, -inset * 0.6f);
            const int alpha = static_cast<int>((0.25f - f * 0.18f - p3 * 0.06f) * 255);
            p.setPen(QPen(QColor(200, 180, 255, qMax(0, alpha)), 1.4));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(arch, 18, 18);
        }
        p.setPen(QPen(QColor(220, 210, 255, 200), 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(c, 4, 4);

        // Perspective floor line.
        p.setPen(QPen(QColor(200, 180, 255, 120), 1.0));
        p.drawLine(QPointF(r.left() + 8, r.bottom() - 12),
                   QPointF(r.right() - 8, r.bottom() - 12));
    } else if (fx == "sidechan") {
        // Pressed object.
        const float depth = (0.2f + 0.6f * p2) * h;
        const float press = m_sidechainValue * depth;
        const QRectF floor(r.left() + 12, r.bottom() - 18, r.width() - 24, 6);
        p.setPen(QPen(QColor(255, 120, 120, 200), 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(floor, 4, 4);

        const float blobW = w * 0.22f;
        const float blobH = h * (0.22f - press / h * 0.12f);
        const QRectF blob(c.x() - blobW * 0.5f, r.bottom() - 26 - blobH - press,
                          blobW, blobH);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(255, 160, 160, 200), 1.4));
        p.drawRoundedRect(blob, 10, 10);

        // Top press plate.
        p.setPen(QPen(QColor(255, 120, 120, 160), 1.2));
        const QRectF pressPlate(r.left() + 18, r.top() + 12, r.width() - 36, 6);
        p.drawRoundedRect(pressPlate, 3, 3);
    }

    p.restore();
}

void FxPageWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);

    QPainter p(this);
    Theme::paintBackground(p, rect());
    p.setRenderHint(QPainter::Antialiasing, true);

    const int margin = Theme::px(20);
    const int headerH = Theme::px(26);
    const int rightPanelW = Theme::px(420);
    const int gap = Theme::px(12);

    const QRectF headerRect(margin, margin, width() - 2 * margin, headerH);
    p.setFont(Theme::condensedFont(12, QFont::Bold));
    p.setPen(Theme::accent());
    p.drawText(headerRect, Qt::AlignLeft | Qt::AlignVCenter, "FX / MIXER");
    p.setPen(Theme::textMuted());
    p.setFont(Theme::baseFont(9));
    p.drawText(headerRect, Qt::AlignRight | Qt::AlignVCenter,
               "Click slot + click effect  |  Ctrl+Up/Down = reorder  Del = clear");

    const QRectF rightRect(width() - rightPanelW - margin, margin + headerH + 8, rightPanelW,
                           height() - margin * 2 - headerH - 8);
    const QRectF editorRect(rightRect.left(), rightRect.top(), rightRect.width(),
                            rightRect.height());
    const QRectF stripsRect(margin, rightRect.top(), rightRect.left() - margin - gap,
                            rightRect.height());

    m_effectHits.clear();

    // FX atmosphere layer removed for performance.

    // Editor panel.
    p.setBrush(Theme::bg1());
    p.setPen(QPen(Theme::stroke(), 1.2));
    p.drawRoundedRect(editorRect, 12, 12);

    QRectF editorHeader(editorRect.left() + Theme::px(12), editorRect.top() + Theme::px(8),
                        editorRect.width() - Theme::px(24), Theme::px(20));
    p.setFont(Theme::condensedFont(11, QFont::DemiBold));
    p.setPen(Theme::accentAlt());
    p.drawText(editorHeader, Qt::AlignLeft | Qt::AlignVCenter, "EDITOR");

    QString currentEffect;
    if (m_selectedTrack >= 0 && m_selectedTrack < m_tracks.size()) {
        const FxTrack &track = m_tracks[m_selectedTrack];
        if (m_selectedSlot >= 0 && m_selectedSlot < track.inserts.size()) {
            currentEffect = track.inserts[m_selectedSlot].effect;
        }
    }
    if (currentEffect.isEmpty()) {
        currentEffect = "NONE";
    }
    p.setPen(Theme::text());
    p.setFont(Theme::baseFont(10, QFont::DemiBold));
    p.drawText(QRectF(editorRect.left() + Theme::px(12), editorHeader.bottom() + Theme::px(8),
                      editorRect.width() - Theme::px(24), Theme::px(20)),
               Qt::AlignLeft | Qt::AlignVCenter, "FX: " + currentEffect.toUpper());

    FxInsert slot;
    if (m_selectedTrack >= 0 && m_selectedTrack < m_tracks.size() &&
        m_selectedSlot >= 0 && m_selectedSlot < m_tracks[m_selectedTrack].inserts.size()) {
        slot = m_tracks[m_selectedTrack].inserts[m_selectedSlot];
    }

    const float visualTop = editorHeader.bottom() + 34;
    const float visualBottom = editorRect.bottom() - 14;
    const QRectF visualRect(editorRect.left() + Theme::px(10), visualTop,
                            editorRect.width() - Theme::px(20), visualBottom - visualTop);
    const float level = m_pads ? m_pads->busMeter(m_selectedTrack) : 0.0f;
    drawEffectPreview(p, visualRect, slot, level);

    // Strips.
    const int trackCount = m_tracks.size();
    const float stripW =
        (stripsRect.width() - (trackCount - 1) * gap) / static_cast<float>(trackCount);
    const float stripH = stripsRect.height();
    const float slotH = Theme::pxF(24.0f);
    const int slotCount = m_tracks.first().inserts.size();

    m_slotHits.clear();
    p.setFont(Theme::baseFont(9, QFont::DemiBold));

    for (int i = 0; i < trackCount; ++i) {
        const float x = stripsRect.left() + i * (stripW + gap);
        QRectF stripRect(x, stripsRect.top(), stripW, stripH);
        const bool activeTrack = (i == m_selectedTrack);

        p.setBrush(Theme::bg1());
        p.setPen(QPen(activeTrack ? Theme::accentAlt() : Theme::stroke(), 1.2));
        p.drawRoundedRect(stripRect, 10, 10);

        QRectF nameRect(stripRect.left(), stripRect.top(), stripRect.width(), 20);
        p.setPen(activeTrack ? Theme::accentAlt() : Theme::textMuted());
        p.drawText(nameRect, Qt::AlignCenter, m_tracks[i].name);

        // Meter (real bus level).
        QRectF meterRect(stripRect.left() + Theme::px(6), nameRect.bottom() + Theme::px(8),
                         Theme::px(10), stripRect.height() - Theme::px(60));
        p.setBrush(Theme::bg2());
        p.setPen(QPen(Theme::stroke(), 1.0));
        p.drawRoundedRect(meterRect, 4, 4);
        float level = 0.0f;
        if (m_pads) {
            level = m_pads->busMeter(i);
        }
        level = qBound(0.0f, level, 1.0f);
        QRectF meterFill(meterRect.left() + Theme::px(1),
                         meterRect.bottom() - meterRect.height() * level,
                         meterRect.width() - Theme::px(2),
                         meterRect.height() * level - Theme::px(1));
        p.setBrush(Theme::accent());
        p.setPen(Qt::NoPen);
        p.drawRect(meterFill);

        // Insert slots.
        float slotTop = nameRect.bottom() + Theme::px(6);
        for (int s = 0; s < slotCount; ++s) {
            QRectF slotRect(stripRect.left() + Theme::px(22), slotTop,
                            stripRect.width() - Theme::px(30), slotH);
            const bool slotSelected = (activeTrack && s == m_selectedSlot);
            p.setBrush(slotSelected ? Theme::bg3() : Theme::bg2());
            p.setPen(QPen(slotSelected ? Theme::accent() : Theme::stroke(), 1.0));
            p.drawRoundedRect(slotRect, Theme::px(6), Theme::px(6));
            const QString effectName = m_tracks[i].inserts[s].effect;
            QString label = effectName.isEmpty() ? QString("INSERT %1").arg(s + 1)
                                                 : effectName.toUpper();
            p.setPen(slotSelected ? Theme::accent() : Theme::text());
            p.drawText(slotRect.adjusted(Theme::px(6), 0, -Theme::px(6), 0),
                       Qt::AlignVCenter | Qt::AlignLeft, label);
            m_slotHits.push_back({slotRect, i, s});
            slotTop += slotH + Theme::px(6);
        }

        // Fader.
        QRectF faderRect(stripRect.left() + stripRect.width() / 2.0f - Theme::px(6),
                         stripRect.bottom() - Theme::px(120), Theme::px(12), Theme::px(90));
        p.setBrush(Theme::bg2());
        p.setPen(QPen(Theme::stroke(), 1.0));
        p.drawRoundedRect(faderRect, Theme::px(4), Theme::px(4));
        QRectF knob(faderRect.left() - Theme::px(6),
                    faderRect.bottom() - Theme::px(30), Theme::px(24), Theme::px(12));
        p.setBrush(Theme::accentAlt());
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(knob, Theme::px(3), Theme::px(3));
    }

    if (m_showMenu) {
        const QRectF overlay = rect();
        p.setBrush(Theme::withAlpha(Theme::bg0(), 200));
        p.setPen(Qt::NoPen);
        p.drawRect(overlay);

        const float menuW = Theme::pxF(320.0f);
        const float menuH = Theme::pxF(180.0f);
        const QRectF menuRect((width() - menuW) * 0.5f, (height() - menuH) * 0.5f, menuW,
                              menuH);
        p.setBrush(Theme::bg2());
        p.setPen(QPen(Theme::accentAlt(), 1.4));
        p.drawRoundedRect(menuRect, 12, 12);

        p.setPen(Theme::accentAlt());
        p.setFont(Theme::condensedFont(12, QFont::DemiBold));
        p.drawText(QRectF(menuRect.left() + Theme::px(12), menuRect.top() + Theme::px(8),
                          menuRect.width() - Theme::px(24), Theme::px(20)),
                   Qt::AlignLeft | Qt::AlignVCenter, "PLUGIN MENU");

        const int rows = 2;
        const int cols = 4;
        const float gridGap = Theme::pxF(8.0f);
        const QRectF gridRect(menuRect.left() + Theme::px(12), menuRect.top() + Theme::px(34),
                              menuRect.width() - Theme::px(24), menuRect.height() - Theme::px(46));
        const float cellW = (gridRect.width() - (cols - 1) * gridGap) / cols;
        const float cellH = (gridRect.height() - (rows - 1) * gridGap) / rows;

        p.setFont(Theme::baseFont(9, QFont::DemiBold));
        for (int i = 0; i < m_effects.size(); ++i) {
            const int r = i / cols;
            const int c = i % cols;
            const QRectF cell(gridRect.left() + c * (cellW + gridGap),
                              gridRect.top() + r * (cellH + gridGap), cellW, cellH);
            const bool selected = (i == m_selectedEffect);
            p.setBrush(selected ? Theme::bg3() : Theme::bg1());
            p.setPen(QPen(selected ? Theme::accent() : Theme::stroke(), 1.0));
            p.drawRoundedRect(cell, Theme::px(8), Theme::px(8));
            p.setPen(selected ? Theme::accent() : Theme::text());
            p.drawText(cell.adjusted(Theme::px(6), 0, -Theme::px(6), 0),
                       Qt::AlignCenter, m_effects[i].toUpper());
            m_effectHits.push_back({cell, i});
        }
    }
}
