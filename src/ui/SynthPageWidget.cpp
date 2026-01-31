#include "SynthPageWidget.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include "PadBank.h"
#include "Theme.h"

SynthPageWidget::SynthPageWidget(PadBank *pads, QWidget *parent) : QWidget(parent), m_pads(pads) {
    setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);
    m_waveforms << "SINE" << "SAW" << "SQUARE";

    if (m_pads) {
        m_activePad = m_pads->activePad();
        connect(m_pads, &PadBank::activePadChanged, this, [this](int index) {
            m_activePad = index;
            update();
        });
        connect(m_pads, &PadBank::padChanged, this, [this](int) { update(); });
    }
}

void SynthPageWidget::setActivePad(int pad) {
    m_activePad = pad;
    update();
}

void SynthPageWidget::keyPressEvent(QKeyEvent *event) {
    const int key = event->key();
    if (key == Qt::Key_Left) {
        m_selectedParam = (m_selectedParam - 1 + 3) % 3;
        update();
        return;
    }
    if (key == Qt::Key_Right) {
        m_selectedParam = (m_selectedParam + 1) % 3;
        update();
        return;
    }
    if (key == Qt::Key_Up || key == Qt::Key_Down) {
        if (!m_pads) {
            return;
        }
        const int idx = (key == Qt::Key_Up) ? 1 : -1;
        QString current = m_pads->synthName(m_activePad);
        int waveIndex = m_waveforms.indexOf(current);
        if (waveIndex < 0) {
            waveIndex = 0;
        }
        waveIndex = (waveIndex + idx + m_waveforms.size()) % m_waveforms.size();
        m_pads->setSynth(m_activePad, m_waveforms[waveIndex]);
        update();
        return;
    }
    if (key == Qt::Key_Space && m_pads) {
        m_pads->triggerPad(m_activePad);
        return;
    }
}

void SynthPageWidget::mousePressEvent(QMouseEvent *event) {
    setFocus(Qt::MouseFocusReason);
    const QPointF pos = event->position();
    for (int i = 0; i < m_paramRects.size(); ++i) {
        if (m_paramRects[i].contains(pos)) {
            m_selectedParam = i;
            update();
            return;
        }
    }
}

void SynthPageWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter p(this);
    Theme::paintBackground(p, rect());
    Theme::applyRenderHints(p);

    const int margin = Theme::px(24);
    const QRectF headerRect(margin, Theme::px(12), width() - margin * 2, Theme::px(28));
    p.setPen(Theme::accent());
    p.setFont(Theme::condensedFont(14, QFont::Bold));
    p.drawText(headerRect, Qt::AlignLeft | Qt::AlignVCenter, "SYNTH");

    QString name = m_pads ? m_pads->synthName(m_activePad) : QString();
    if (name.isEmpty()) {
        name = "SINE";
    }
    p.setPen(Theme::textMuted());
    p.setFont(Theme::baseFont(10, QFont::DemiBold));
    p.drawText(headerRect, Qt::AlignRight | Qt::AlignVCenter,
               QString("PAD %1  %2").arg(m_activePad + 1).arg(name));

    // Simple wave display
    const QRectF waveRect(margin, headerRect.bottom() + Theme::px(16),
                          width() - margin * 2, height() * 0.4f);
    p.setBrush(QColor(24, 18, 28));
    p.setPen(QPen(Theme::accent(), 1.2));
    p.drawRoundedRect(waveRect, Theme::px(10), Theme::px(10));

    QPainterPath wave;
    const int steps = 120;
    for (int i = 0; i <= steps; ++i) {
        const float x = waveRect.left() + (waveRect.width() * i) / steps;
        const float t = static_cast<float>(i) / steps;
        float v = 0.0f;
        const QString wav = name.toLower();
        if (wav.contains("saw")) {
            v = 2.0f * (t - std::floor(t + 0.5f));
        } else if (wav.contains("square")) {
            v = (t < 0.5f) ? 1.0f : -1.0f;
        } else {
            v = std::sin(t * 2.0f * static_cast<float>(M_PI));
        }
        const float y = waveRect.center().y() - v * waveRect.height() * 0.35f;
        if (i == 0) {
            wave.moveTo(QPointF(x, y));
        } else {
            wave.lineTo(QPointF(x, y));
        }
    }
    p.setPen(QPen(Theme::accentAlt(), 2.0));
    p.drawPath(wave);

    // Param blocks
    const QRectF paramsRect(margin, waveRect.bottom() + Theme::px(18),
                            width() - margin * 2, height() - waveRect.bottom() - Theme::px(48));
    const int cols = 3;
    const float gap = Theme::pxF(16.0f);
    const float blockW = (paramsRect.width() - gap * (cols - 1)) / cols;
    const float blockH = Theme::pxF(64.0f);

    m_paramRects.clear();
    const QStringList labels = {"WAVE", "FILTER", "ENV"};
    for (int i = 0; i < cols; ++i) {
        const float x = paramsRect.left() + i * (blockW + gap);
        const QRectF r(x, paramsRect.top(), blockW, blockH);
        m_paramRects.push_back(r);
        p.setBrush(i == m_selectedParam ? Theme::accentAlt() : Theme::bg1());
        p.setPen(QPen(Theme::stroke(), 1.0));
        p.drawRoundedRect(r, Theme::px(10), Theme::px(10));
        p.setPen(i == m_selectedParam ? Theme::bg0() : Theme::text());
        p.setFont(Theme::condensedFont(12, QFont::Bold));
        p.drawText(r, Qt::AlignCenter, labels[i]);
    }
}
