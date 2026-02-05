#include "SynthPageWidget.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include "PadBank.h"
#include "Theme.h"

namespace {
QString synthIdOrDefault(PadBank *pads, int pad) {
    if (!pads) {
        return QString("ZYN:KEYS/PIANO 1");
    }
    const QString id = pads->synthId(pad);
    if (!id.isEmpty()) {
        return id;
    }
    return QString("ZYN:KEYS/PIANO 1");
}

QString synthType(const QString &id) {
    const QString up = id.trimmed().toUpper();
    if (up.startsWith("ZYN")) {
        return "ZYN";
    }
    return "ZYN";
}

QString synthPreset(const QString &id) {
    const int colon = id.indexOf(':');
    if (colon >= 0) {
        return id.mid(colon + 1).trimmed();
    }
    return id.trimmed();
}
} // namespace

SynthPageWidget::SynthPageWidget(PadBank *pads, QWidget *parent)
    : QWidget(parent), m_pads(pads) {
    setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);

    if (m_pads) {
        m_fluidPresets = PadBank::synthPresets();
    }
    if (m_fluidPresets.isEmpty()) {
        m_fluidPresets << "KEYS/PIANO 1" << "KEYS/PIANO 2" << "LEADS/SAW" << "BASS/FINGER"
                       << "PADS/WARM";
    }
    m_categories.clear();
    for (const QString &item : m_fluidPresets) {
        const QStringList parts = item.split('/');
        const QString group = parts.value(0, "MISC").toUpper();
        if (!m_categories.contains(group)) {
            m_categories << group;
        }
    }

    if (m_pads) {
        m_activePad = m_pads->activePad();
        connect(m_pads, &PadBank::activePadChanged, this, [this](int index) {
            m_activePad = index;
            update();
        });
        connect(m_pads, &PadBank::padChanged, this, [this](int) { update(); });
        connect(m_pads, &PadBank::padParamsChanged, this, [this](int) { update(); });
    }
}

void SynthPageWidget::setActivePad(int pad) {
    m_activePad = pad;
    update();
}

void SynthPageWidget::keyPressEvent(QKeyEvent *event) {
    const int key = event->key();
    if (key == Qt::Key_Space && m_pads) {
        m_pads->triggerPad(m_activePad);
        return;
    }
}

void SynthPageWidget::mousePressEvent(QMouseEvent *event) {
    setFocus(Qt::MouseFocusReason);
    const QPointF pos = event->position();

    for (int i = 0; i < m_categoryRects.size(); ++i) {
        if (m_categoryRects[i].contains(pos)) {
            m_selectedCategory = i;
            update();
            return;
        }
    }

    for (const PresetRow &row : m_presetRows) {
        if (!row.header && row.rect.contains(pos) && m_pads) {
            m_pads->setSynth(m_activePad, QString("ZYN:%1").arg(row.presetId));
            update();
            return;
        }
    }

    for (int i = 0; i < m_adsrRects.size(); ++i) {
        if (m_adsrRects[i].contains(pos) && m_pads) {
            m_dragParam = i;
            const QRectF r = m_adsrRects[i];
            float value = 1.0f - qBound(0.0f, static_cast<float>((pos.y() - r.top()) / r.height()), 1.0f);
            auto sp = m_pads->synthParams(m_activePad);
            if (i == 0) sp.attack = value;
            if (i == 1) sp.decay = value;
            if (i == 2) sp.sustain = value;
            if (i == 3) sp.release = value;
            m_pads->setSynthAdsr(m_activePad, sp.attack, sp.decay, sp.sustain, sp.release);
            update();
            return;
        }
    }

    return;
}

void SynthPageWidget::mouseMoveEvent(QMouseEvent *event) {
    if (!(event->buttons() & Qt::LeftButton) || m_dragParam < 0 || !m_pads) {
        return;
    }
    const QRectF r = m_adsrRects[m_dragParam];
    float value = 1.0f - qBound(0.0f, static_cast<float>((event->position().y() - r.top()) / r.height()), 1.0f);
    auto sp = m_pads->synthParams(m_activePad);
    if (m_dragParam == 0) sp.attack = value;
    if (m_dragParam == 1) sp.decay = value;
    if (m_dragParam == 2) sp.sustain = value;
    if (m_dragParam == 3) sp.release = value;
    m_pads->setSynthAdsr(m_activePad, sp.attack, sp.decay, sp.sustain, sp.release);
    update();
}

void SynthPageWidget::mouseReleaseEvent(QMouseEvent *) {
    m_dragParam = -1;
}

void SynthPageWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter p(this);
    Theme::paintBackground(p, rect());
    Theme::applyRenderHints(p);

    const int margin = Theme::px(20);
    const QRectF panel = rect().adjusted(margin, margin, -margin, -margin);
    p.setBrush(Theme::bg1());
    p.setPen(QPen(Theme::stroke(), 1.2));
    p.drawRoundedRect(panel, Theme::px(14), Theme::px(14));

    const QRectF header(panel.left() + Theme::px(12), panel.top() + Theme::px(8),
                        panel.width() - Theme::px(24), Theme::px(28));
    p.setPen(Theme::accent());
    p.setFont(Theme::condensedFont(14, QFont::Bold));
    p.drawText(header, Qt::AlignLeft | Qt::AlignVCenter, "SYNTH");

    const QString id = synthIdOrDefault(m_pads, m_activePad);
    const QString preset = synthPreset(id);
    p.setPen(Theme::textMuted());
    p.setFont(Theme::baseFont(10, QFont::DemiBold));
    p.drawText(header, Qt::AlignRight | Qt::AlignVCenter,
               QString("PAD %1  %2").arg(m_activePad + 1).arg(preset));

    auto sp = m_pads ? m_pads->synthParams(m_activePad) : PadBank::SynthParams();

    const float gap = Theme::pxF(14.0f);
    QRectF left(panel.left() + Theme::px(12), header.bottom() + Theme::px(10),
                panel.width() * 0.32f, panel.height() - header.height() - Theme::px(20));
    QRectF right(left.right() + gap, left.top(), panel.right() - left.right() - Theme::px(16),
                 left.height());

    // ADSR section (right side)
    const QRectF adsrArea(right.left(), right.top(), right.width(), right.height() * 0.62f);
    p.setBrush(QColor(20, 18, 26));
    p.setPen(QPen(Theme::stroke(), 1.0));
    p.drawRoundedRect(adsrArea, Theme::px(10), Theme::px(10));

    const float a = sp.attack;
    const float d = sp.decay;
    const float s = sp.sustain;
    const float r = sp.release;

    QPainterPath env;
    const float x0 = adsrArea.left() + Theme::px(10);
    const float y0 = adsrArea.bottom() - Theme::px(12);
    const float width = adsrArea.width() - Theme::px(20);
    const float height = adsrArea.height() - Theme::px(24);

    const float aX = x0 + width * (0.12f + a * 0.18f);
    const float dX = aX + width * (0.12f + d * 0.18f);
    const float sY = y0 - height * (0.15f + s * 0.75f);
    const float rX = dX + width * (0.34f + r * 0.25f);

    env.moveTo(QPointF(x0, y0));
    env.lineTo(QPointF(aX, adsrArea.top() + Theme::px(10)));
    env.lineTo(QPointF(dX, sY));
    env.lineTo(QPointF(rX, sY));
    env.lineTo(QPointF(x0 + width, y0));

    p.setPen(QPen(Theme::accentAlt(), 2.0));
    p.drawPath(env);

    // ADSR sliders
    const QRectF adsrStrip(right.left(), adsrArea.bottom() + Theme::px(10),
                           right.width(), right.height() - adsrArea.height() - Theme::px(10));
    const QStringList labels = {"A", "D", "S", "R"};
    m_adsrRects.clear();
    for (int i = 0; i < 4; ++i) {
        const float w = (adsrStrip.width() - gap * 3) / 4.0f;
        QRectF rRect(adsrStrip.left() + i * (w + gap), adsrStrip.top(), w, adsrStrip.height());
        m_adsrRects.push_back(rRect);
        p.setBrush(Theme::bg2());
        p.setPen(QPen(Theme::stroke(), 1.0));
        p.drawRoundedRect(rRect, Theme::px(8), Theme::px(8));
        const float value = (i == 0) ? a : (i == 1) ? d : (i == 2) ? s : r;
        const float fillH = rRect.height() * value;
        QRectF fill(rRect.left() + Theme::px(4), rRect.bottom() - fillH,
                    rRect.width() - Theme::px(8), fillH);
        p.setBrush(Theme::accent());
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(fill, Theme::px(6), Theme::px(6));
        p.setPen(Theme::text());
        p.setFont(Theme::baseFont(10, QFont::DemiBold));
        p.drawText(rRect.adjusted(0, Theme::px(4), 0, 0), Qt::AlignTop | Qt::AlignHCenter, labels[i]);
    }

    // Left categories
    p.setBrush(QColor(24, 24, 30));
    p.setPen(QPen(Theme::stroke(), 1.0));
    p.drawRoundedRect(left, Theme::px(10), Theme::px(10));

    m_categoryRects.clear();
    const float catRowH = Theme::pxF(34.0f);
    float cy = left.top() + Theme::px(10);
    for (int i = 0; i < m_categories.size(); ++i) {
        QRectF r(left.left() + Theme::px(10), cy,
                 left.width() - Theme::px(20), catRowH - Theme::px(4));
        m_categoryRects.push_back(r);
        const bool active = (i == m_selectedCategory);
        p.setBrush(active ? Theme::accentAlt() : Theme::bg2());
        p.setPen(QPen(Theme::stroke(), 1.0));
        p.drawRoundedRect(r, Theme::px(8), Theme::px(8));
        p.setPen(active ? Theme::bg0() : Theme::text());
        p.setFont(Theme::baseFont(11, QFont::DemiBold));
        p.drawText(r, Qt::AlignCenter, m_categories[i]);
        cy += catRowH;
    }

    // Right presets filtered by category
    p.setBrush(QColor(24, 24, 30));
    p.setPen(QPen(Theme::stroke(), 1.0));
    p.drawRoundedRect(right, Theme::px(10), Theme::px(10));

    const QString activeCat =
        m_categories.isEmpty() ? QString("MISC")
                               : m_categories[qBound(0, m_selectedCategory, m_categories.size() - 1)];
    const float rowH = Theme::pxF(30.0f);
    float y = right.top() + Theme::px(8);
    m_presetRows.clear();
    for (const QString &item : m_fluidPresets) {
        const QStringList parts = item.split('/');
        const QString group = parts.value(0, "MISC").toUpper();
        if (group != activeCat) {
            continue;
        }
        PresetRow row;
        row.header = false;
        row.label = parts.size() > 1 ? parts[1] : item;
        row.presetId = item;
        row.rect = QRectF(right.left() + Theme::px(12), y,
                          right.width() - Theme::px(20), rowH - Theme::px(4));
        m_presetRows.push_back(row);
        y += rowH - Theme::px(2);
    }

    for (const PresetRow &row : m_presetRows) {
        const bool active = preset.toUpper() == row.presetId.toUpper();
        p.setBrush(active ? Theme::accentAlt() : Theme::bg2());
        p.setPen(QPen(Theme::stroke(), 1.0));
        p.drawRoundedRect(row.rect, Theme::px(6), Theme::px(6));
        p.setPen(active ? Theme::bg0() : Theme::text());
        p.setFont(Theme::baseFont(10, QFont::DemiBold));
        p.drawText(row.rect.adjusted(Theme::px(8), 0, -Theme::px(4), 0),
                   Qt::AlignLeft | Qt::AlignVCenter, row.label);
    }
}
