#include "FxPageWidget.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QShowEvent>
#include <cmath>

#include "PadBank.h"
#include "Theme.h"

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

    if (key == Qt::Key_Left) {
        m_selectedTrack = (m_selectedTrack - 1 + m_tracks.size()) % m_tracks.size();
        update();
        return;
    }
    if (key == Qt::Key_Right) {
        m_selectedTrack = (m_selectedTrack + 1) % m_tracks.size();
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

void FxPageWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);

    QPainter p(this);
    Theme::paintBackground(p, rect());

    const int margin = 20;
    const int headerH = 26;
    const int rightPanelW = 560;
    const int gap = 12;

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

    // Editor panel.
    p.setBrush(Theme::bg1());
    p.setPen(QPen(Theme::stroke(), 1.2));
    p.drawRect(editorRect);

    QRectF editorHeader(editorRect.left() + 12, editorRect.top() + 8, editorRect.width() - 24, 20);
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
    p.drawText(QRectF(editorRect.left() + 12, editorHeader.bottom() + 8,
                      editorRect.width() - 24, 20),
               Qt::AlignLeft | Qt::AlignVCenter, "FX: " + currentEffect.toUpper());

    // Parameter knobs.
    const QRectF knobArea(editorRect.left() + 10, editorHeader.bottom() + 40,
                          editorRect.width() - 20, 120);
    const float knobW = knobArea.width() / 3.0f;
    p.setPen(QPen(Theme::stroke(), 1.0));
    FxInsert slot;
    if (m_selectedTrack >= 0 && m_selectedTrack < m_tracks.size() &&
        m_selectedSlot >= 0 && m_selectedSlot < m_tracks[m_selectedTrack].inserts.size()) {
        slot = m_tracks[m_selectedTrack].inserts[m_selectedSlot];
    }
    const float params[3] = {slot.p1, slot.p2, slot.p3};
    for (int i = 0; i < 3; ++i) {
        QRectF knob(knobArea.left() + i * knobW + 8, knobArea.top() + 10, 42, 42);
        const bool selected = (i == m_selectedParam);
        p.setBrush(selected ? Theme::bg3() : Theme::bg2());
        p.drawEllipse(knob);
        p.setPen(selected ? Theme::accent() : Theme::accentAlt());
        const float angle = (-140.0f + params[i] * 280.0f) * 3.14159f / 180.0f;
        p.drawLine(knob.center(),
                   QPointF(knob.center().x() + std::cos(angle) * 16.0f,
                           knob.center().y() + std::sin(angle) * 16.0f));
        p.setPen(Theme::textMuted());
        p.drawText(QRectF(knob.left() - 8, knob.bottom() + 6, knob.width() + 16, 16),
                   Qt::AlignCenter, QString("P%1").arg(i + 1));
        p.setPen(QPen(Theme::stroke(), 1.0));
    }

    // Strips.
    const int trackCount = m_tracks.size();
    const float stripW =
        (stripsRect.width() - (trackCount - 1) * gap) / static_cast<float>(trackCount);
    const float stripH = stripsRect.height();
    const float slotH = 24.0f;
    const int slotCount = m_tracks.first().inserts.size();

    m_slotHits.clear();
    p.setFont(Theme::baseFont(9, QFont::DemiBold));

    for (int i = 0; i < trackCount; ++i) {
        const float x = stripsRect.left() + i * (stripW + gap);
        QRectF stripRect(x, stripsRect.top(), stripW, stripH);
        const bool activeTrack = (i == m_selectedTrack);

        p.setBrush(Theme::bg1());
        p.setPen(QPen(activeTrack ? Theme::accentAlt() : Theme::stroke(), 1.2));
        p.drawRect(stripRect);

        QRectF nameRect(stripRect.left(), stripRect.top(), stripRect.width(), 20);
        p.setPen(activeTrack ? Theme::accentAlt() : Theme::textMuted());
        p.drawText(nameRect, Qt::AlignCenter, m_tracks[i].name);

        // Meter (real bus level).
        QRectF meterRect(stripRect.left() + 6, nameRect.bottom() + 8, 10, stripRect.height() - 60);
        p.setBrush(Theme::bg2());
        p.setPen(QPen(Theme::stroke(), 1.0));
        p.drawRect(meterRect);
        float level = 0.0f;
        if (m_pads) {
            level = m_pads->busMeter(i);
        }
        level = qBound(0.0f, level, 1.0f);
        QRectF meterFill(meterRect.left() + 1,
                         meterRect.bottom() - meterRect.height() * level,
                         meterRect.width() - 2,
                         meterRect.height() * level - 1);
        p.setBrush(Theme::accent());
        p.setPen(Qt::NoPen);
        p.drawRect(meterFill);

        // Insert slots.
        float slotTop = nameRect.bottom() + 6;
        for (int s = 0; s < slotCount; ++s) {
            QRectF slotRect(stripRect.left() + 22, slotTop, stripRect.width() - 30, slotH);
            const bool slotSelected = (activeTrack && s == m_selectedSlot);
            p.setBrush(slotSelected ? Theme::bg3() : Theme::bg2());
            p.setPen(QPen(slotSelected ? Theme::accent() : Theme::stroke(), 1.0));
            p.drawRect(slotRect);
            const QString effectName = m_tracks[i].inserts[s].effect;
            QString label = effectName.isEmpty() ? QString("INSERT %1").arg(s + 1)
                                                 : effectName.toUpper();
            p.setPen(slotSelected ? Theme::accent() : Theme::text());
            p.drawText(slotRect.adjusted(6, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, label);
            m_slotHits.push_back({slotRect, i, s});
            slotTop += slotH + 6;
        }

        // Fader.
        QRectF faderRect(stripRect.left() + stripRect.width() / 2.0f - 6,
                         stripRect.bottom() - 120, 12, 90);
        p.setBrush(Theme::bg2());
        p.setPen(QPen(Theme::stroke(), 1.0));
        p.drawRect(faderRect);
        QRectF knob(faderRect.left() - 6, faderRect.bottom() - 30, 24, 12);
        p.setBrush(Theme::accentAlt());
        p.setPen(Qt::NoPen);
        p.drawRect(knob);
    }

    if (m_showMenu) {
        const QRectF overlay = rect();
        p.setBrush(Theme::withAlpha(Theme::bg0(), 200));
        p.setPen(Qt::NoPen);
        p.drawRect(overlay);

        const float menuW = 380.0f;
        const float menuH = 220.0f;
        const QRectF menuRect((width() - menuW) * 0.5f, (height() - menuH) * 0.5f, menuW,
                              menuH);
        p.setBrush(Theme::bg2());
        p.setPen(QPen(Theme::accentAlt(), 1.4));
        p.drawRect(menuRect);

        p.setPen(Theme::accentAlt());
        p.setFont(Theme::condensedFont(12, QFont::DemiBold));
        p.drawText(QRectF(menuRect.left() + 12, menuRect.top() + 8, menuRect.width() - 24, 20),
                   Qt::AlignLeft | Qt::AlignVCenter, "PLUGIN MENU");

        const int rows = 2;
        const int cols = 4;
        const float gridGap = 8.0f;
        const QRectF gridRect(menuRect.left() + 12, menuRect.top() + 34,
                              menuRect.width() - 24, menuRect.height() - 46);
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
            p.drawRect(cell);
            p.setPen(selected ? Theme::accent() : Theme::text());
            p.drawText(cell.adjusted(6, 0, -6, 0), Qt::AlignCenter, m_effects[i].toUpper());
            m_effectHits.push_back({cell, i});
        }
    }
}
