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

    m_effects = {"reverb", "comp", "dist", "lofi",
                 "cassette", "chorus", "eq", "sidechan",
                 "delay", "tremolo", "ringmod", "robot",
                 "punch", "subharm", "keyharm", "freeze"};

    m_animTimer.setInterval(33);
    m_animTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_animTimer, &QTimer::timeout, this, &FxPageWidget::advanceAnimation);

    m_waveHistory.fill(0.0f, 128);
    m_waveHead = 0;
    m_waveFilled = false;
}

QVector<FxTrack> FxPageWidget::trackData() const {
    return m_tracks;
}

void FxPageWidget::setTrackData(const QVector<FxTrack> &tracks) {
    m_tracks = tracks;
    const QStringList defaultNames = {"MASTER", "A", "B", "C", "D", "E"};
    if (m_tracks.isEmpty()) {
        for (const QString &name : defaultNames) {
            m_tracks.push_back({name, QVector<FxInsert>(4)});
        }
    }
    // Normalize track count and slot sizes.
    while (m_tracks.size() < defaultNames.size()) {
        m_tracks.push_back({defaultNames[m_tracks.size()], QVector<FxInsert>(4)});
    }
    if (m_tracks.size() > defaultNames.size()) {
        m_tracks.resize(defaultNames.size());
    }
    for (int i = 0; i < m_tracks.size(); ++i) {
        if (m_tracks[i].name.isEmpty()) {
            m_tracks[i].name = defaultNames.value(i);
        }
        if (m_tracks[i].inserts.size() != 4) {
            m_tracks[i].inserts.resize(4);
        }
        syncBusEffects(i);
    }
    m_selectedTrack = qBound(0, m_selectedTrack, m_tracks.size() - 1);
    m_selectedSlot = qBound(0, m_selectedSlot, m_tracks[m_selectedTrack].inserts.size() - 1);
    update();
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
    track.inserts[m_selectedSlot].p4 = 0.5f;
    track.inserts[m_selectedSlot].p5 = 0.0f;
    const QString name = track.inserts[m_selectedSlot].effect.toLower();
    if (name == "delay") {
        track.inserts[m_selectedSlot].p3 = 0.35f; // mix
        track.inserts[m_selectedSlot].p4 = 1.0f;  // stereo
    } else if (name == "tremolo") {
        track.inserts[m_selectedSlot].p1 = 0.6f;
        track.inserts[m_selectedSlot].p3 = 1.0f;  // sync
    } else if (name == "keyharm") {
        track.inserts[m_selectedSlot].p1 = 0.35f;
        track.inserts[m_selectedSlot].p2 = 0.0f;  // C
        track.inserts[m_selectedSlot].p3 = 0.0f;  // major
    } else if (name == "freeze") {
        track.inserts[m_selectedSlot].p1 = 0.45f;
        track.inserts[m_selectedSlot].p2 = 0.8f;  // wet
        track.inserts[m_selectedSlot].p3 = 0.0f;  // hold
    }
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
        if (m_showMenu) {
            assignEffect(m_selectedEffect);
            m_showMenu = false;
            m_showEditor = true;
        } else {
            m_showMenu = true;
            m_showEditor = false;
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
        if (m_showEditor) {
            m_showEditor = false;
            update();
            return;
        }
    }

    if (m_showMenu) {
        const int cols = 4;
        const int rows = qMax(1, (m_effects.size() + cols - 1) / cols);
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
    if (key == Qt::Key_4) {
        m_selectedParam = 3;
        update();
        return;
    }
    if (key == Qt::Key_5) {
        m_selectedParam = 4;
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
                if (m_selectedParam == 2) {
                    slot.p3 = qBound(0.0f, slot.p3 - 0.05f, 1.0f);
                } else if (m_selectedParam == 3) {
                    slot.p4 = qBound(0.0f, slot.p4 - 0.05f, 1.0f);
                } else {
                    slot.p5 = qBound(0.0f, slot.p5 - 0.05f, 1.0f);
                }
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
                if (m_selectedParam == 2) {
                    slot.p3 = qBound(0.0f, slot.p3 + 0.05f, 1.0f);
                } else if (m_selectedParam == 3) {
                    slot.p4 = qBound(0.0f, slot.p4 + 0.05f, 1.0f);
                } else {
                    slot.p5 = qBound(0.0f, slot.p5 + 0.05f, 1.0f);
                }
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
        const float attack = 0.12f + clamp01(slot.p3) * 0.25f;
        const float release = 0.04f + clamp01(slot.p4) * 0.18f;
        const float coeff = (target > m_compValue) ? attack : release;
        m_compValue = m_compValue + (target - m_compValue) * coeff;
    } else {
        m_compValue *= 0.9f;
    }

    const float waveLevel = m_pads ? m_pads->busMeter(m_selectedTrack) : 0.0f;
    if (!m_waveHistory.isEmpty()) {
        m_waveHistory[m_waveHead] = clamp01(waveLevel);
        m_waveHead = (m_waveHead + 1) % m_waveHistory.size();
        if (m_waveHead == 0) {
            m_waveFilled = true;
        }
    }

    update();
}

void FxPageWidget::mousePressEvent(QMouseEvent *event) {
    setFocus(Qt::MouseFocusReason);
    const QPointF pos = event->position();

    if (m_synthBusRect.contains(pos) && m_pads) {
        const int pad = m_pads->activePad();
        if (m_pads->isSynth(pad)) {
            const int nextBus = (m_pads->fxBus(pad) + 1) % 6;
            m_pads->setFxBus(pad, nextBus);
            update();
            return;
        }
    }

    if (m_showEditor && m_closeRect.contains(pos)) {
        m_showEditor = false;
        update();
        return;
    }

    if (m_showMenu) {
        for (const FxEffectHit &hit : m_effectHits) {
            if (hit.rect.contains(pos)) {
                m_selectedEffect = hit.index;
                assignEffect(hit.index);
                m_showMenu = false;
                m_showEditor = true;
                return;
            }
        }
        m_showMenu = false;
        update();
        return;
    }

    if (m_makeupRect.contains(pos)) {
        if (m_selectedTrack >= 0 && m_selectedTrack < m_tracks.size() &&
            m_selectedSlot >= 0 && m_selectedSlot < m_tracks[m_selectedTrack].inserts.size()) {
            FxInsert &slot = m_tracks[m_selectedTrack].inserts[m_selectedSlot];
            if (slot.effect.toLower() == "comp") {
                slot.p5 = (slot.p5 >= 0.5f) ? 0.0f : 1.0f;
                syncBusEffects(m_selectedTrack);
                update();
                return;
            }
        }
    }

    for (int i = 0; i < m_faderHits.size(); ++i) {
        if (m_faderHits[i].contains(pos)) {
            m_dragFaderTrack = i;
            if (m_pads) {
                const QRectF fader = m_faderHits[i];
                const float norm =
                    1.0f - (pos.y() - fader.top()) / qMax(1.0f, fader.height());
                m_pads->setBusGain(i, qBound(0.0f, norm, 1.2f));
            }
            update();
            return;
        }
    }

    for (const FxInsertHit &hit : m_slotHits) {
        if (hit.rect.contains(pos)) {
            m_selectedTrack = hit.track;
            m_selectedSlot = hit.slot;
            m_showMenu = true;
            m_showEditor = false;
            update();
            return;
        }
    }

    m_showMenu = false;
    update();
}

void FxPageWidget::mouseMoveEvent(QMouseEvent *event) {
    if (m_dragFaderTrack < 0 || !m_pads) {
        return;
    }
    const QRectF fader = m_faderHits.value(m_dragFaderTrack);
    const QPointF pos = event->position();
    const float norm = 1.0f - (pos.y() - fader.top()) / qMax(1.0f, fader.height());
    m_pads->setBusGain(m_dragFaderTrack, qBound(0.0f, norm, 1.2f));
    update();
}

void FxPageWidget::mouseReleaseEvent(QMouseEvent *event) {
    Q_UNUSED(event);
    m_dragFaderTrack = -1;
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
            fx.p4 = slot.p4;
            fx.p5 = slot.p5;
            ids.push_back(fx);
        }
    }
    m_pads->setBusEffects(trackIndex, ids);
}

void FxPageWidget::drawEffectPreview(QPainter &p, const QRectF &rect, const FxInsert &slot,
                                     float level) {
    p.save();
    Theme::applyRenderHints(p);
    m_makeupRect = QRectF();
    m_closeRect = QRectF();

    const QRectF r = rect.adjusted(10, 10, -10, -10);
    const QPointF c = r.center();
    const float w = r.width();
    const float h = r.height();
    const float t = m_animTime;

    const float p1 = clamp01(slot.p1);
    const float p2 = clamp01(slot.p2);
    const float p3 = clamp01(slot.p3);
    const float p4 = clamp01(slot.p4);
    const float p5 = clamp01(slot.p5);
    const QString fx = slot.effect.toLower();

    struct ParamInfo {
        QString label;
        QString value;
        float norm = 0.0f;
        int index = 0;
    };

    auto percent = [](float v) { return QString("%1%").arg(qRound(v * 100.0f)); };
    auto hzLabel = [](float v) { return QString("%1 Hz").arg(v, 0, 'f', 1); };
    auto msLabel = [](float v) { return QString("%1 ms").arg(qRound(v * 1000.0f)); };

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
        // Compressor UI preview (style reference).
        const QColor bg(40, 40, 40);
        const QColor panel(30, 30, 30);
        const QColor cyan(20, 210, 255);
        const QColor magenta(255, 50, 100);
        const QColor white(235, 235, 240);
        const QColor grid(80, 80, 90);

        p.setBrush(bg);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(rect, 14, 14);

        const float pad = 10.0f;
        QRectF inner = rect.adjusted(pad, pad, -pad, -pad);

        // Header
        QRectF header(inner.left(), inner.top(), inner.width(), 26);
        p.setPen(white);
        p.setFont(Theme::condensedFont(12, QFont::Bold));
        p.drawText(header, Qt::AlignLeft | Qt::AlignVCenter, "COMPRESSOR");

        // Close icon (right)
        const float menuX = inner.right() - 26;
        const float menuY = header.top() + 6;
        p.setPen(QPen(white, 2.0, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(menuX, menuY), QPointF(menuX + 16, menuY + 16));
        p.drawLine(QPointF(menuX + 16, menuY), QPointF(menuX, menuY + 16));

        // Layout zones
        const float meterW = inner.width() * 0.12f;
        const float gap = 10.0f;
        const float footerH = inner.height() * 0.28f;
        QRectF metersTop(inner.left(), header.bottom() + 6, inner.width(), inner.height() - header.height() - 6);
        QRectF graphRect(inner.left() + meterW + gap,
                         header.bottom() + 6,
                         inner.width() - meterW * 2.0f - gap * 2.0f,
                         metersTop.height() - footerH - 8);
        QRectF footerRect(inner.left(), graphRect.bottom() + 8, inner.width(), footerH);

        // IN/OUT meters
        auto drawMeter = [&](const QRectF &mr, float lvl, const QString &label) {
            p.setPen(QPen(grid, 1.0));
            p.setBrush(panel);
            p.drawRoundedRect(mr, 8, 8);
            const float fillH = qMax(2.0f, (mr.height() - 6) * lvl);
            QRectF fill(mr.left() + 3, mr.bottom() - 3 - fillH, mr.width() - 6, fillH);
            p.setBrush(cyan);
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(fill, 6, 6);
            p.setPen(white);
            p.setFont(Theme::baseFont(9, QFont::DemiBold));
            p.drawText(QRectF(mr.left(), mr.top() - 18, mr.width(), 16),
                       Qt::AlignCenter, label);
            p.setPen(QPen(grid, 1.0));
            p.drawLine(QPointF(mr.center().x(), mr.bottom() - 12), QPointF(mr.center().x(), mr.bottom() - 24));
        };

        const float inLevel = clamp01(level);
        const float outLevel = clamp01(level * (1.0f - m_compValue * 0.5f));
        QRectF inRect(inner.left(), graphRect.top(), meterW - 6, graphRect.height());
        QRectF outRect(inner.right() - meterW + 6, graphRect.top(), meterW - 6, graphRect.height());
        drawMeter(inRect, inLevel, "IN");
        drawMeter(outRect, outLevel, "OUT");

        // Graph area (cached grid + labels)
        const QSize graphSize = graphRect.size().toSize();
        if (m_compGraphCacheSize != graphSize || m_compGraphCache.isNull()) {
            m_compGraphCacheSize = graphSize;
            m_compGraphCache = QPixmap(graphSize);
            m_compGraphCache.fill(Qt::transparent);
            QPainter gp(&m_compGraphCache);
            gp.setRenderHint(QPainter::Antialiasing, false);
            const QRectF g(0, 0, graphSize.width(), graphSize.height());
            gp.setBrush(panel);
            gp.setPen(QPen(grid, 1.0));
            gp.drawRoundedRect(g, 10, 10);
            gp.setFont(Theme::baseFont(8, QFont::DemiBold));
            for (int i = 0; i <= 6; ++i) {
                const float y = g.top() + i * (g.height() / 6.0f);
                gp.setPen(QPen(grid, 1.0));
                gp.drawLine(QPointF(g.left() + 6, y), QPointF(g.right() - 6, y));
                const int db = -6 * i;
                gp.setPen(white);
                gp.drawText(QRectF(g.left() + 8, y - 8, 40, 14),
                            Qt::AlignLeft | Qt::AlignVCenter, QString::number(db));
            }
        }
        p.drawPixmap(graphRect.topLeft(), m_compGraphCache);

        // Compression amount label
        const float grDb = -m_compValue * 18.0f;
        p.setPen(magenta);
        p.setFont(Theme::baseFont(9, QFont::DemiBold));
        p.drawText(QRectF(graphRect.left() + 8, graphRect.top() + 2, graphRect.width(), 14),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QString("GR %1 dB").arg(QString::number(grDb, 'f', 1)));

        // Threshold line
        const float thrDb = -36.0f + p1 * 36.0f;
        const float thrNorm = (0.0f - thrDb) / 36.0f;
        const float thrY = graphRect.top() + thrNorm * graphRect.height();
        p.setPen(QPen(magenta, 1.4));
        p.drawLine(QPointF(graphRect.left() + 6, thrY),
                   QPointF(graphRect.right() - 6, thrY));

        // Waveform (real level history)
        if (!m_waveHistory.isEmpty()) {
            QPainterPath wave;
            QPainterPath compWave;
            const int total = m_waveHistory.size();
            const int count = m_waveFilled ? total : qMax(1, m_waveHead);
            const int start = m_waveFilled ? m_waveHead : 0;
            const float compScale = qBound(0.2f, 1.0f - m_compValue * 0.7f, 1.0f);
            for (int i = 0; i < count; ++i) {
                const int idx = (start + i) % total;
                const float x = graphRect.left() + 6 +
                                (graphRect.width() - 12) * (i / static_cast<float>(qMax(1, count - 1)));
                const float amp = m_waveHistory[idx];
                const float y = graphRect.center().y() - (amp * 0.9f) * (graphRect.height() * 0.45f);
                const float yc = graphRect.center().y() -
                                 (amp * 0.9f * compScale) * (graphRect.height() * 0.45f);
                if (i == 0) {
                    wave.moveTo(QPointF(x, y));
                    compWave.moveTo(QPointF(x, yc));
                } else {
                    wave.lineTo(QPointF(x, y));
                    compWave.lineTo(QPointF(x, yc));
                }
            }
            p.save();
            p.setRenderHint(QPainter::Antialiasing, false);
            p.setPen(QPen(QColor(220, 220, 220, 210), 1.2));
            p.drawPath(wave);
            p.setPen(QPen(QColor(255, 80, 110, 220), 1.4));
            p.drawPath(compWave);
            p.restore();
        }

        // Makeup button
        const float btnY = footerRect.top() + 6;
        const float btnW = 80;
        m_makeupRect = QRectF(inner.left() + 6, btnY, btnW, 26);
        const bool makeupOn = (p5 >= 0.5f);
        p.setBrush(makeupOn ? QColor(40, 180, 230) : panel);
        p.setPen(QPen(makeupOn ? QColor(120, 220, 255) : grid, 1.0));
        p.drawRoundedRect(m_makeupRect, 8, 8);
        p.setPen(makeupOn ? QColor(20, 40, 60) : white);
        p.setFont(Theme::baseFont(9, QFont::DemiBold));
        p.drawText(m_makeupRect, Qt::AlignCenter, "makeup");

        // Knobs
        auto drawKnob = [&](const QPointF &center, float radius, const QString &label,
                            const QString &value, float norm, bool highlight) {
            p.setBrush(Qt::white);
            p.setPen(QPen(QColor(230, 230, 230), 1.0));
            p.drawEllipse(center, radius, radius);
            const float ang = -120.0f + norm * 240.0f;
            const float rad = ang * static_cast<float>(M_PI) / 180.0f;
            p.setPen(QPen(QColor(40, 40, 40), 2.0));
            p.drawLine(center, QPointF(center.x() + std::cos(rad) * radius * 0.8f,
                                       center.y() + std::sin(rad) * radius * 0.8f));
            p.setPen(highlight ? magenta : QColor(255, 100, 130));
            p.setFont(Theme::baseFont(8, QFont::DemiBold));
            p.drawText(QRectF(center.x() - radius, center.y() - radius - 18, radius * 2, 16),
                       Qt::AlignCenter, label);
            QRectF valRect(center.x() - radius, center.y() + radius + 6, radius * 2, 18);
            p.setBrush(QColor(0, 0, 0));
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(valRect, 6, 6);
            p.setPen(white);
            p.drawText(valRect, Qt::AlignCenter, value);
        };

        const float knobR = 26;
        const float knobY = footerRect.bottom() - 40;
        const float knobGap = (inner.width() - knobR * 2 * 4) / 5.0f;
        const float threshDb = -36.0f + p1 * 36.0f;
        const float ratio = 1.0f + p2 * 11.0f;
        const float attackMs = 5.0f + p3 * 45.0f;
        const float releaseMs = 30.0f + p4 * 350.0f;
        const QString tVal = QString("%1dB").arg(QString::number(threshDb, 'f', 0));
        const QString rVal = QString::number(ratio, 'f', 1);
        const QString aVal = QString("%1ms").arg(QString::number(attackMs, 'f', 0));
        const QString relVal = QString("%1ms").arg(QString::number(releaseMs, 'f', 0));
        QPointF k1(inner.left() + knobGap + knobR, knobY);
        QPointF k2(k1.x() + (knobR * 2 + knobGap), knobY);
        QPointF k3(k2.x() + (knobR * 2 + knobGap), knobY);
        QPointF k4(k3.x() + (knobR * 2 + knobGap), knobY);
        drawKnob(k1, knobR, "THRESH", tVal, p1, m_selectedParam == 0);
        drawKnob(k2, knobR, "RATIO", rVal, p2, m_selectedParam == 1);
        drawKnob(k3, knobR, "ATTACK", aVal, p3, m_selectedParam == 2);
        drawKnob(k4, knobR, "RELEASE", relVal, p4, m_selectedParam == 3);
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
        // Old TV screen (bit crusher).
        const QRectF screen = r.adjusted(6, 10, -6, -12);
        p.setPen(QPen(QColor(255, 80, 120, 220), 2.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(screen, 12, 12);

        const QRectF inner = screen.adjusted(6, 6, -6, -8);
        const int cols = 6 + static_cast<int>(p1 * 18.0f);
        const int rows = 4 + static_cast<int>(p2 * 12.0f);
        const float cellW = inner.width() / cols;
        const float cellH = inner.height() / rows;
        const int phase = static_cast<int>(t * (1.0f + p3 * 5.0f));

        p.save();
        p.setClipRect(inner);
        for (int y = 0; y < rows; ++y) {
            for (int x = 0; x < cols; ++x) {
                const float hsh = hash2(x, y, phase);
                const float bright = 0.2f + 0.8f * hsh;
                QColor col(40, 200, 255, static_cast<int>(bright * 255));
                p.setPen(Qt::NoPen);
                p.setBrush(col);
                p.drawRect(QRectF(inner.left() + x * cellW, inner.top() + y * cellH,
                                  cellW - 1.0f, cellH - 1.0f));
            }
        }

        // Scanlines
        p.setPen(QPen(QColor(30, 40, 60, 120), 1.0));
        for (int y = 0; y < inner.height(); y += 3) {
            const float yy = inner.top() + y;
            p.drawLine(QPointF(inner.left(), yy), QPointF(inner.right(), yy));
        }

        // Wobbly sine trace (jitter)
        QPainterPath wave;
        const float amp = inner.height() * (0.1f + p3 * 0.15f);
        for (int x = 0; x <= inner.width(); ++x) {
            const float xx = inner.left() + x;
            const float yy =
                inner.center().y() + std::sin((x / inner.width()) * 6.28f * (1.2f + p2)) * amp;
            if (x == 0) {
                wave.moveTo(QPointF(xx, yy));
            } else {
                wave.lineTo(QPointF(xx, yy));
            }
        }
        p.setPen(QPen(QColor(120, 220, 255, 200), 2.0));
        p.drawPath(wave);
        p.restore();
    } else if (fx == "eq") {
        // Low/High cut preview.
        const QRectF frame = r.adjusted(8, 10, -8, -12);
        p.setPen(QPen(QColor(80, 160, 200, 200), 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(frame, 8, 8);

        p.setPen(QPen(QColor(60, 60, 80, 160), 1.0));
        for (int i = 1; i < 5; ++i) {
            const float y = frame.top() + (frame.height() / 5.0f) * i;
            p.drawLine(QPointF(frame.left() + 6, y), QPointF(frame.right() - 6, y));
        }

        float lowPos = lerp(0.08f, 0.45f, p1);
        float highPos = lerp(0.55f, 0.92f, p2);
        if (highPos - lowPos < 0.12f) {
            highPos = lowPos + 0.12f;
        }
        const float xLow = frame.left() + frame.width() * lowPos;
        const float xHigh = frame.left() + frame.width() * highPos;

        p.setPen(QPen(QColor(200, 220, 240, 220), 1.6));
        QPainterPath curve;
        curve.moveTo(frame.left(), frame.bottom());
        curve.lineTo(xLow, frame.bottom());
        curve.lineTo(xLow, frame.top());
        curve.lineTo(xHigh, frame.top());
        curve.lineTo(xHigh, frame.bottom());
        curve.lineTo(frame.right(), frame.bottom());
        p.drawPath(curve);

        p.setPen(QPen(QColor(255, 200, 120, 220), 1.4));
        p.drawLine(QPointF(xLow, frame.top()), QPointF(xLow, frame.bottom()));
        p.setPen(QPen(QColor(120, 200, 255, 220), 1.4));
        p.drawLine(QPointF(xHigh, frame.top()), QPointF(xHigh, frame.bottom()));

        p.setPen(QColor(200, 200, 220));
        p.setFont(Theme::baseFont(8, QFont::DemiBold));
        p.drawText(QRectF(frame.left() + 6, frame.top() + 6, 60, 14),
                   Qt::AlignLeft | Qt::AlignVCenter, "LOW CUT");
        p.drawText(QRectF(frame.right() - 70, frame.top() + 6, 60, 14),
                   Qt::AlignRight | Qt::AlignVCenter, "HIGH CUT");
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
    } else if (fx == "delay") {
        // Echo taps.
        const int taps = 4;
        for (int i = 0; i < taps; ++i) {
            const float f = static_cast<float>(i) / taps;
            const float alpha = 0.25f + (1.0f - f) * 0.5f * (0.2f + p3);
            const float dx = w * (0.08f + f * (0.6f + p1 * 0.3f));
            QRectF echo(r.left() + dx, r.top() + 20 + f * 8, w * 0.22f, h * 0.18f);
            p.setPen(QPen(QColor(120, 200, 255, static_cast<int>(alpha * 255)), 1.4));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(echo, 6, 6);
        }
        p.setPen(QColor(200, 200, 220));
        p.setFont(Theme::baseFont(9, QFont::DemiBold));
        p.drawText(r.adjusted(8, 8, -8, -8), Qt::AlignTop | Qt::AlignLeft,
                   (p4 > 0.5f) ? "STEREO" : "MONO");
    } else if (fx == "tremolo") {
        // Amplitude wave.
        QPainterPath wave;
        const float cycles = 1.0f + p2 * 3.0f;
        for (int x = 0; x <= static_cast<int>(w); ++x) {
            const float tX = static_cast<float>(x) / qMax(1.0f, w);
            const float amp =
                (std::sin(tX * 2.0f * static_cast<float>(M_PI) * cycles + t) + 1.0f) * 0.5f;
            const float yy = r.center().y() - (amp - 0.5f) * h * (0.6f + p1 * 0.3f);
            if (x == 0) {
                wave.moveTo(r.left() + x, yy);
            } else {
                wave.lineTo(r.left() + x, yy);
            }
        }
        p.setPen(QPen(QColor(140, 220, 160, 220), 1.6));
        p.drawPath(wave);
    } else if (fx == "ringmod") {
        // Ring modulation cross.
        p.setPen(QPen(QColor(180, 220, 255, 220), 1.4));
        p.drawEllipse(c, w * 0.18f, w * 0.18f);
        p.drawLine(QPointF(c.x() - w * 0.2f, c.y()),
                   QPointF(c.x() + w * 0.2f, c.y()));
        p.drawLine(QPointF(c.x(), c.y() - w * 0.2f),
                   QPointF(c.x(), c.y() + w * 0.2f));
    } else if (fx == "robot") {
        // Short comb blocks.
        const int blocks = 6;
        for (int i = 0; i < blocks; ++i) {
            const float f = static_cast<float>(i) / blocks;
            QRectF b(r.left() + f * w * 0.85f, r.center().y() - 12, w * 0.1f, 24);
            p.setBrush(QColor(160, 200, 240, static_cast<int>((0.3f + 0.5f * p3) * 255)));
            p.setPen(Qt::NoPen);
            p.drawRect(b);
        }
    } else if (fx == "punch") {
        // Transient spike.
        QPainterPath spike;
        spike.moveTo(r.left() + 10, r.bottom() - 18);
        spike.lineTo(c.x(), r.top() + 10);
        spike.lineTo(r.right() - 10, r.bottom() - 18);
        p.setPen(QPen(QColor(255, 180, 80, 230), 2.0));
        p.drawPath(spike);
    } else if (fx == "subharm") {
        // Low sine wave.
        const QRectF waveRect = r.adjusted(6, 18, -6, -18);
        QPainterPath wave;
        const int steps = 48;
        for (int i = 0; i <= steps; ++i) {
            const float tX = static_cast<float>(i) / steps;
            const float phase = tX * 2.0f * static_cast<float>(M_PI);
            const float yy = waveRect.center().y() - std::sin(phase * 0.5f) * waveRect.height() * 0.3f;
            const float xx = waveRect.left() + tX * waveRect.width();
            if (i == 0) {
                wave.moveTo(xx, yy);
            } else {
                wave.lineTo(xx, yy);
            }
        }
        p.setPen(QPen(QColor(120, 200, 255, 220), 1.6));
        p.drawPath(wave);
    } else if (fx == "keyharm") {
        // Key + mode text.
        static const QStringList keys = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
        const int keyIndex = qBound(0, static_cast<int>(std::floor(p2 * 11.99f)), 11);
        const QString mode = (p3 > 0.5f) ? "MINOR" : "MAJOR";
        p.setPen(QColor(220, 220, 240));
        p.setFont(Theme::condensedFont(12, QFont::Bold));
        p.drawText(r, Qt::AlignCenter, QString("%1 %2").arg(keys[keyIndex], mode));
    } else if (fx == "freeze") {
        // Freeze icon.
        p.setPen(QPen(QColor(180, 220, 255, 220), 2.0));
        p.drawLine(QPointF(c.x(), r.top() + 12), QPointF(c.x(), r.bottom() - 12));
        p.drawLine(QPointF(r.left() + 12, c.y()), QPointF(r.right() - 12, c.y()));
        p.drawLine(QPointF(c.x() - 14, c.y() - 14), QPointF(c.x() + 14, c.y() + 14));
        p.drawLine(QPointF(c.x() - 14, c.y() + 14), QPointF(c.x() + 14, c.y() - 14));
    }

    if (fx != "comp") {
        QVector<ParamInfo> params;
        if (fx == "reverb") {
            params.push_back({"WET", percent(p1), p1, 0});
            params.push_back({"FEED", percent(p2), p2, 1});
        } else if (fx == "dist") {
            const float drive = 1.0f + p1 * 6.0f;
            params.push_back({"DRIVE", QString("x%1").arg(drive, 0, 'f', 1), p1, 0});
            params.push_back({"MIX", percent(p2), p2, 1});
        } else if (fx == "lofi") {
            const float bits = 4.0f + p1 * 8.0f;
            const int hold = 1 + static_cast<int>(p2 * 7.0f);
            params.push_back({"BITS", QString::number(bits, 'f', 1), p1, 0});
            params.push_back({"HOLD", QString::number(hold), p2, 1});
        } else if (fx == "cassette") {
            params.push_back({"NOISE", percent(p1), p1, 0});
            params.push_back({"TONE", percent(p2), p2, 1});
        } else if (fx == "chorus") {
            const float depth = 0.002f + p1 * 0.008f;
            const float rate = 0.1f + p2 * 0.8f;
            params.push_back({"DEPTH", QString::number(depth * 1000.0f, 'f', 1), p1, 0});
            params.push_back({"RATE", hzLabel(rate), p2, 1});
            params.push_back({"MIX", percent(p3), p3, 2});
        } else if (fx == "eq") {
            const float lowCut = 30.0f * std::pow(2.0f, p1 * 5.5f);
            const float highCut = 800.0f * std::pow(2.0f, p2 * 4.5f);
            params.push_back({"LOW CUT", hzLabel(lowCut), p1, 0});
            params.push_back({"HIGH CUT", hzLabel(highCut), p2, 1});
        } else if (fx == "sidechan") {
            params.push_back({"THRESH", percent(p1), p1, 0});
            params.push_back({"AMOUNT", percent(p2), p2, 1});
            params.push_back({"RELEASE", percent(p3), p3, 2});
        } else if (fx == "delay") {
            const float timeSec = 0.03f + p1 * 0.9f;
            params.push_back({"TIME", msLabel(timeSec), p1, 0});
            params.push_back({"FEED", percent(p2), p2, 1});
            params.push_back({"MIX", percent(p3), p3, 2});
            params.push_back({"STEREO", (p4 >= 0.5f) ? "ON" : "OFF", p4, 3});
        } else if (fx == "tremolo") {
            const bool sync = (p3 >= 0.5f);
            QString rateLabel;
            if (sync) {
                static const QStringList divs = {"1/16", "1/8", "1/4", "1/2", "1/1"};
                const int idx = qBound(0, static_cast<int>(p2 * 4.99f), 4);
                rateLabel = divs[idx];
            } else {
                const float rate = 0.5f + p2 * 6.0f;
                rateLabel = hzLabel(rate);
            }
            params.push_back({"DEPTH", percent(p1), p1, 0});
            params.push_back({"RATE", rateLabel, p2, 1});
            params.push_back({"SYNC", sync ? "ON" : "OFF", p3, 2});
        } else if (fx == "ringmod") {
            const float freq = 50.0f * std::pow(2.0f, p1 * 5.0f);
            params.push_back({"FREQ", hzLabel(freq), p1, 0});
            params.push_back({"MIX", percent(p2), p2, 1});
        } else if (fx == "robot") {
            const float timeSec = 0.002f + p1 * 0.02f;
            params.push_back({"TIME", msLabel(timeSec), p1, 0});
            params.push_back({"FEED", percent(p2), p2, 1});
            params.push_back({"MIX", percent(p3), p3, 2});
        } else if (fx == "punch") {
            params.push_back({"AMOUNT", percent(p1), p1, 0});
            params.push_back({"ATTACK", percent(p2), p2, 1});
            params.push_back({"RELEASE", percent(p3), p3, 2});
        } else if (fx == "subharm") {
            params.push_back({"AMOUNT", percent(p1), p1, 0});
        } else if (fx == "keyharm") {
            static const QStringList keys = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
            const int keyIndex = qBound(0, static_cast<int>(p2 * 11.99f), 11);
            const bool minor = (p3 >= 0.5f);
            params.push_back({"MIX", percent(p1), p1, 0});
            params.push_back({"KEY", keys[keyIndex], p2, 1});
            params.push_back({"MODE", minor ? "MIN" : "MAJ", p3, 2});
        } else if (fx == "freeze") {
            const float lenSec = 0.15f + p1 * 0.85f;
            params.push_back({"LENGTH", msLabel(lenSec), p1, 0});
            params.push_back({"MIX", percent(p2), p2, 1});
            params.push_back({"REFRESH", (p3 >= 0.5f) ? "ON" : "OFF", p3, 2});
        }

        if (!params.isEmpty()) {
            const float knobRowH = Theme::pxF(70.0f);
            QRectF knobArea = rect.adjusted(Theme::px(12),
                                            rect.height() - knobRowH - Theme::px(12),
                                            -Theme::px(12),
                                            -Theme::px(12));
            const int count = params.size();
            const float cellW = knobArea.width() / count;
            const float radius = qMin(cellW, knobArea.height()) * 0.28f;

            for (int i = 0; i < count; ++i) {
                const ParamInfo &pi = params[i];
                const float cx = knobArea.left() + cellW * (i + 0.5f);
                const float cy = knobArea.top() + radius + Theme::pxF(8.0f);
                const QPointF center(cx, cy);
                const bool selected = (m_selectedParam == pi.index);
                p.setBrush(selected ? Theme::accentAlt() : QColor(30, 30, 36));
                p.setPen(QPen(Theme::stroke(), 1.0));
                p.drawEllipse(center, radius, radius);
                const float ang = -120.0f + pi.norm * 240.0f;
                const float rad = ang * static_cast<float>(M_PI) / 180.0f;
                p.setPen(QPen(selected ? Theme::bg0() : QColor(220, 220, 230), 1.6));
                p.drawLine(center, QPointF(center.x() + std::cos(rad) * radius * 0.8f,
                                           center.y() + std::sin(rad) * radius * 0.8f));
                QRectF labelRect(cx - cellW * 0.5f, knobArea.bottom() - Theme::px(24),
                                 cellW, Theme::px(12));
                p.setPen(selected ? Theme::accentAlt() : Theme::textMuted());
                p.setFont(Theme::baseFont(8, QFont::DemiBold));
                p.drawText(labelRect, Qt::AlignCenter, pi.label);
                QRectF valueRect(cx - cellW * 0.5f, labelRect.top() - Theme::px(14),
                                 cellW, Theme::px(12));
                p.setPen(Theme::text());
                p.setFont(Theme::baseFont(8, QFont::DemiBold));
                p.drawText(valueRect, Qt::AlignCenter, pi.value);
            }
        }
    }

    p.restore();
}

void FxPageWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);

    QPainter p(this);
    Theme::paintBackground(p, rect());
    Theme::applyRenderHints(p);

    const int margin = Theme::px(16);
    const int headerH = Theme::px(22);
    const int gap = Theme::px(10);

    const QRectF headerRect(margin, margin, width() - 2 * margin, headerH);
    p.setFont(Theme::condensedFont(12, QFont::Bold));
    p.setPen(Theme::accent());
    p.drawText(headerRect, Qt::AlignLeft | Qt::AlignVCenter, "FX / MIXER");
    m_synthBusRect = QRectF();
    if (m_pads && m_pads->isSynth(m_pads->activePad())) {
        const int pad = m_pads->activePad();
        const int bus = m_pads->fxBus(pad);
        const QString label = QString("SYNTH PAD %1 BUS: %2")
                                  .arg(pad + 1)
                                  .arg(PadBank::fxBusLabel(bus));
        const float w = Theme::pxF(200.0f);
        m_synthBusRect = QRectF(headerRect.right() - w, headerRect.top(),
                                w, headerRect.height());
        p.setBrush(Theme::bg1());
        p.setPen(QPen(Theme::accentAlt(), 1.1));
        p.drawRoundedRect(m_synthBusRect, Theme::px(6), Theme::px(6));
        p.setPen(Theme::accentAlt());
        p.setFont(Theme::baseFont(9, QFont::DemiBold));
        p.drawText(m_synthBusRect, Qt::AlignCenter, label);
    } else {
        p.setPen(Theme::textMuted());
        p.setFont(Theme::baseFont(9));
        p.drawText(headerRect, Qt::AlignRight | Qt::AlignVCenter,
                   "Enter = plugin menu  |  Ctrl+Up/Down = reorder  Del = clear");
    }

    const QRectF stripsRect(margin, headerRect.bottom() + Theme::px(8),
                            width() - 2 * margin,
                            height() - margin - headerRect.bottom() - Theme::px(8));

    m_effectHits.clear();

    // Strips.
    const int trackCount = m_tracks.size();
    const float stripW =
        (stripsRect.width() - (trackCount - 1) * gap) / static_cast<float>(trackCount);
    const float stripH = stripsRect.height();
    const float slotH = Theme::pxF(24.0f);
    const int slotCount = m_tracks.first().inserts.size();

    m_slotHits.clear();
    m_faderHits.clear();
    p.setFont(Theme::baseFont(9, QFont::DemiBold));

    for (int i = 0; i < trackCount; ++i) {
        const float x = stripsRect.left() + i * (stripW + gap);
        QRectF stripRect(x, stripsRect.top(), stripW, stripH);
        const bool activeTrack = (i == m_selectedTrack);

        const QColor busBg(46, 38, 80);
        const QColor slotCyan(12, 200, 255);
        const QColor meterPink(255, 50, 100);
        const QColor meterCyan(20, 210, 255);

        p.setBrush(busBg);
        p.setPen(QPen(activeTrack ? Theme::accentAlt() : Theme::stroke(), 1.2));
        p.drawRoundedRect(stripRect, 10, 10);

        QRectF nameRect(stripRect.left(), stripRect.top(), stripRect.width(), Theme::px(22));
        p.setPen(Qt::white);
        p.drawText(nameRect, Qt::AlignCenter, m_tracks[i].name);

        const float meterW = Theme::pxF(12.0f);
        const float barW = Theme::pxF(16.0f);
        QRectF faderRect(stripRect.right() - barW - Theme::px(6), nameRect.bottom() + Theme::px(6),
                         barW, stripRect.height() - Theme::px(58));
        QRectF meterRect(faderRect.left() - meterW - Theme::px(6), nameRect.bottom() + Theme::px(6),
                         meterW, stripRect.height() - Theme::px(58));
        p.setBrush(QColor(60, 50, 95));
        p.setPen(QPen(Theme::stroke(), 1.0));
        p.drawRoundedRect(meterRect, Theme::px(4), Theme::px(4));

        float level = 0.0f;
        if (m_pads) {
            level = m_pads->busMeter(i);
        }
        level = qBound(0.0f, level, 1.0f);
        QRectF meterFill(meterRect.left() + Theme::px(2),
                         meterRect.bottom() - meterRect.height() * level,
                         meterRect.width() - Theme::px(4),
                         meterRect.height() * level - Theme::px(2));
        p.setBrush(meterCyan);
        p.setPen(Qt::NoPen);
        p.drawRect(meterFill);

        auto dbToY = [&](float db) {
            const float amp = std::pow(10.0f, db / 20.0f);
            return meterRect.bottom() - amp * (meterRect.height() - Theme::pxF(2.0f));
        };
        const QVector<float> ticks = {0.0f, -12.0f, -24.0f, -36.0f};
        p.setPen(QPen(QColor(200, 200, 220, 140), 1.0));
        p.setFont(Theme::baseFont(7, QFont::DemiBold));
        for (float db : ticks) {
            const float y = dbToY(db);
            p.drawLine(QPointF(meterRect.left() + Theme::pxF(1.0f), y),
                       QPointF(meterRect.right() - Theme::pxF(1.0f), y));
            p.drawText(QRectF(meterRect.right() + Theme::px(2), y - Theme::px(6),
                              Theme::px(18), Theme::px(12)),
                       Qt::AlignLeft | Qt::AlignVCenter, QString::number(static_cast<int>(db)));
        }
        // zero line highlight
        const float y0 = dbToY(0.0f);
        p.setPen(QPen(QColor(255, 80, 110), 1.2));
        p.drawLine(QPointF(meterRect.left(), y0), QPointF(meterRect.right(), y0));
        // clip indicator
        if (level > 0.98f) {
            p.setBrush(QColor(255, 60, 90));
            p.setPen(Qt::NoPen);
            p.drawRect(QRectF(meterRect.left() + Theme::pxF(1.0f), meterRect.top() + Theme::pxF(1.0f),
                              meterRect.width() - Theme::pxF(2.0f), Theme::pxF(3.0f)));
        }

        // Pink volume bar (interactive) on the right.
        p.setBrush(QColor(70, 60, 95));
        p.setPen(Qt::NoPen);
        p.drawRect(faderRect);
        float gain = 1.0f;
        if (m_pads) {
            gain = m_pads->busGain(i);
        }
        gain = qBound(0.0f, gain, 1.2f);
        const float gainH = faderRect.height() * (gain / 1.2f);
        QRectF gainFill(faderRect.left(), faderRect.bottom() - gainH, faderRect.width(), gainH);
        p.setBrush(meterPink);
        p.drawRect(gainFill);
        m_faderHits.push_back(faderRect);

        // dB label in the pink bar.
        p.save();
        p.setPen(Qt::white);
        p.setFont(Theme::baseFont(8, QFont::DemiBold));
        p.translate(faderRect.center());
        p.rotate(-90.0);
        p.drawText(QRectF(-faderRect.height() * 0.5f, -Theme::px(6),
                          faderRect.height(), Theme::px(12)),
                   Qt::AlignCenter, "-0.0 dB");
        p.restore();

        // Insert slots (cyan blocks) - square tiles
        float slotTop = nameRect.bottom() + Theme::px(8);
        const float slotLeft = stripRect.left() + Theme::px(8);
        const float slotRight = meterRect.left() - Theme::px(8);
        const float slotSize = qMin(slotH, slotRight - slotLeft);
        for (int s = 0; s < slotCount; ++s) {
            QRectF slotRect(slotLeft, slotTop, slotSize, slotSize);
            const bool slotSelected = (activeTrack && s == m_selectedSlot);
            p.setBrush(slotSelected ? Theme::accentAlt() : slotCyan);
            p.setPen(Qt::NoPen);
            p.drawRect(slotRect);
            const QString effectName = m_tracks[i].inserts[s].effect;
            QString label = effectName.isEmpty() ? QString("--") : effectName.toUpper();
            p.setPen(QColor(20, 30, 40));
            p.drawText(slotRect.adjusted(Theme::px(6), 0, -Theme::px(6), 0),
                       Qt::AlignVCenter | Qt::AlignLeft, label);
            m_slotHits.push_back({slotRect, i, s});
            slotTop += slotSize + Theme::px(6);
        }

        // M / S buttons
        const QRectF msRect(stripRect.left(), stripRect.bottom() - Theme::px(30),
                            stripRect.width(), Theme::px(30));
        QRectF mRect(msRect.left(), msRect.top(), msRect.width() * 0.5f, msRect.height());
        QRectF sRect(msRect.center().x(), msRect.top(), msRect.width() * 0.5f, msRect.height());
        p.setBrush(QColor(36, 30, 70));
        p.setPen(QPen(Theme::stroke(), 1.0));
        p.drawRect(mRect);
        p.drawRect(sRect);
        p.setPen(QColor(255, 80, 120));
        p.drawText(mRect, Qt::AlignCenter, "M");
        p.setPen(QColor(20, 200, 255));
        p.drawText(sRect, Qt::AlignCenter, "S");
    }

    if (m_showEditor) {
        const QRectF overlay = rect();
        p.setBrush(Theme::withAlpha(Theme::bg0(), 230));
        p.setPen(Qt::NoPen);
        p.drawRect(overlay);

        FxInsert slot;
        if (m_selectedTrack >= 0 && m_selectedTrack < m_tracks.size() &&
            m_selectedSlot >= 0 && m_selectedSlot < m_tracks[m_selectedTrack].inserts.size()) {
            slot = m_tracks[m_selectedTrack].inserts[m_selectedSlot];
        }

        const QRectF editorRect(margin, margin, width() - 2 * margin, height() - 2 * margin);
        p.setBrush(Theme::bg1());
        p.setPen(QPen(Theme::stroke(), 1.2));
        p.drawRoundedRect(editorRect, 12, 12);

        QRectF editorHeader(editorRect.left() + Theme::px(12), editorRect.top() + Theme::px(8),
                            editorRect.width() - Theme::px(24), Theme::px(24));
        p.setFont(Theme::condensedFont(12, QFont::Bold));
        p.setPen(Theme::accentAlt());
        p.drawText(editorHeader, Qt::AlignLeft | Qt::AlignVCenter, "PLUGIN PARAMETERS");

        // Close button
        const float closeX = editorRect.right() - Theme::px(26);
        const float closeY = editorRect.top() + Theme::px(10);
        m_closeRect = QRectF(closeX - 4, closeY - 4, Theme::px(22), Theme::px(22));
        p.setPen(QPen(Theme::text(), 2.0, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(closeX, closeY), QPointF(closeX + Theme::px(14), closeY + Theme::px(14)));
        p.drawLine(QPointF(closeX + Theme::px(14), closeY), QPointF(closeX, closeY + Theme::px(14)));

        const float visualTop = editorHeader.bottom() + Theme::px(16);
        const float visualBottom = editorRect.bottom() - Theme::px(16);
        const QRectF visualRect(editorRect.left() + Theme::px(12), visualTop,
                                editorRect.width() - Theme::px(24), visualBottom - visualTop);
        const float level = m_pads ? m_pads->busMeter(m_selectedTrack) : 0.0f;
        drawEffectPreview(p, visualRect, slot, level);
    }

    if (m_showMenu) {
        const QRectF overlay = rect();
        p.setBrush(Theme::withAlpha(Theme::bg0(), 230));
        p.setPen(Qt::NoPen);
        p.drawRect(overlay);

        const QRectF menuRect(margin, margin, width() - 2 * margin, height() - 2 * margin);
        p.setBrush(Theme::bg2());
        p.setPen(QPen(Theme::accentAlt(), 1.4));
        p.drawRoundedRect(menuRect, 12, 12);

        p.setPen(Theme::accentAlt());
        p.setFont(Theme::condensedFont(14, QFont::DemiBold));
        p.drawText(QRectF(menuRect.left() + Theme::px(16), menuRect.top() + Theme::px(8),
                          menuRect.width() - Theme::px(32), Theme::px(24)),
                   Qt::AlignLeft | Qt::AlignVCenter, "PLUGIN MENU");

        const int cols = 4;
        const int rows = qMax(1, (m_effects.size() + cols - 1) / cols);
        const float gridGap = Theme::pxF(12.0f);
        const QRectF gridRect(menuRect.left() + Theme::px(16), menuRect.top() + Theme::px(48),
                              menuRect.width() - Theme::px(32), menuRect.height() - Theme::px(64));
        const float cellW = (gridRect.width() - (cols - 1) * gridGap) / cols;
        const float cellH = (gridRect.height() - (rows - 1) * gridGap) / rows;

        p.setFont(Theme::baseFont(12, QFont::DemiBold));
        for (int i = 0; i < m_effects.size(); ++i) {
            const int r = i / cols;
            const int c = i % cols;
            const QRectF cell(gridRect.left() + c * (cellW + gridGap),
                              gridRect.top() + r * (cellH + gridGap), cellW, cellH);
            const bool selected = (i == m_selectedEffect);
            p.setBrush(selected ? Theme::bg3() : Theme::bg1());
            p.setPen(QPen(selected ? Theme::accent() : Theme::stroke(), 1.0));
            p.drawRoundedRect(cell, Theme::px(10), Theme::px(10));
            p.setPen(selected ? Theme::accent() : Theme::text());
            p.drawText(cell.adjusted(Theme::px(8), 0, -Theme::px(8), 0),
                       Qt::AlignCenter, m_effects[i].toUpper());
            m_effectHits.push_back({cell, i});
        }
    }
}
