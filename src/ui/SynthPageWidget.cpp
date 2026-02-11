#include "SynthPageWidget.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include "PadBank.h"
#include "Theme.h"

namespace {
QString defaultSynthType() {
    return PadBank::synthTypes().isEmpty() ? QString("DX7") : PadBank::synthTypes().first();
}

QString defaultSynthBank() {
    const QStringList banks = PadBank::synthBanks();
    return banks.isEmpty() ? QString("INTERNAL") : banks.first();
}

QString defaultSynthProgram(const QString &bank) {
    const QStringList presets = PadBank::synthPresetsForBank(bank);
    return presets.isEmpty() ? QString("PROGRAM 01") : presets.first();
}

QString synthIdOrDefault(PadBank *pads, int pad) {
    const QString defaultType = defaultSynthType();
    const QString bank = defaultSynthBank();
    const QString program = defaultSynthProgram(bank);
    const QString fallback = bank.isEmpty()
                                 ? QString("%1:%2").arg(defaultType, program)
                                 : QString("%1:%2/%3").arg(defaultType, bank, program);
    if (!pads) {
        return fallback;
    }
    const QString id = pads->synthId(pad);
    return id.isEmpty() ? fallback : id;
}

QString synthPreset(const QString &id) {
    const int colon = id.indexOf(':');
    if (colon >= 0) {
        return id.mid(colon + 1).trimmed();
    }
    return id.trimmed();
}

QString synthBank(const QString &id) {
    const QString preset = synthPreset(id);
    const int slash = preset.indexOf('/');
    if (slash >= 0) {
        return preset.left(slash).trimmed();
    }
    const QStringList banks = PadBank::synthBanks();
    return banks.isEmpty() ? QString() : banks.first();
}

QString synthProgram(const QString &id) {
    const QString preset = synthPreset(id);
    const int slash = preset.indexOf('/');
    if (slash >= 0) {
        return preset.mid(slash + 1).trimmed();
    }
    return preset.trimmed();
}
} // namespace

SynthPageWidget::SynthPageWidget(PadBank *pads, QWidget *parent)
    : QWidget(parent), m_pads(pads) {
    setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);
    if (m_pads) {
        m_activePad = m_pads->activePad();
    }
    reloadBanks(true);

    if (m_pads) {
        connect(m_pads, &PadBank::activePadChanged, this, [this](int index) {
            m_activePad = index;
            reloadBanks(true);
            update();
        });
        connect(m_pads, &PadBank::padChanged, this, [this](int) {
            reloadBanks(true);
            update();
        });
        connect(m_pads, &PadBank::padParamsChanged, this, [this](int) { update(); });
    }
}

void SynthPageWidget::reloadBanks(bool syncSelection) {
    if (m_pads) {
        m_categories = PadBank::synthBanks();
    } else {
        m_categories.clear();
    }
    if (m_categories.isEmpty()) {
        m_categories << "INTERNAL";
    }
    if (syncSelection) {
        const QString id = synthIdOrDefault(m_pads, m_activePad);
        const QString bank = synthBank(id);
        for (int i = 0; i < m_categories.size(); ++i) {
            if (QString::compare(m_categories[i], bank, Qt::CaseInsensitive) == 0) {
                m_selectedCategory = i;
                break;
            }
        }
    }
    if (m_selectedCategory < 0 || m_selectedCategory >= m_categories.size()) {
        m_selectedCategory = 0;
    }
    const QString bankName =
        m_categories.value(qBound(0, m_selectedCategory, m_categories.size() - 1));
    if (m_pads) {
        m_fluidPresets = PadBank::synthPresetsForBank(bankName);
    } else {
        m_fluidPresets.clear();
    }
    if (m_fluidPresets.isEmpty()) {
        m_fluidPresets << "PROGRAM 01";
    }
}

void SynthPageWidget::setActivePad(int pad) {
    m_activePad = pad;
    reloadBanks(true);
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
            reloadBanks(false);
            update();
            return;
        }
    }

    for (const PresetRow &row : m_presetRows) {
        if (!row.header && row.rect.contains(pos) && m_pads) {
            const QString type = defaultSynthType();
            const QString bank =
                m_categories.value(qBound(0, m_selectedCategory, m_categories.size() - 1));
            const QString presetId = row.presetId;
            const QString fullPreset =
                bank.isEmpty() ? presetId : QString("%1/%2").arg(bank, presetId);
            m_pads->setSynth(m_activePad, QString("%1:%2").arg(type, fullPreset));
            update();
            return;
        }
    }
}

void SynthPageWidget::mouseMoveEvent(QMouseEvent *event) {
    Q_UNUSED(event);
}

void SynthPageWidget::mouseReleaseEvent(QMouseEvent *) {
}

void SynthPageWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter p(this);
    Theme::paintBackground(p, rect());
    Theme::applyRenderHints(p);
    reloadBanks(false);

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
    const QString bankName = synthBank(id);
    const QString programName = synthProgram(id);
    p.setPen(Theme::textMuted());
    p.setFont(Theme::baseFont(10, QFont::DemiBold));
    const QString displayPreset =
        bankName.isEmpty() ? programName : QString("%1 / %2").arg(bankName, programName);
    p.drawText(header, Qt::AlignRight | Qt::AlignVCenter,
               QString("PAD %1  %2").arg(m_activePad + 1).arg(displayPreset));

    const float gap = Theme::pxF(14.0f);
    QRectF left(panel.left() + Theme::px(12), header.bottom() + Theme::px(10),
                panel.width() * 0.32f, panel.height() - header.height() - Theme::px(20));
    QRectF right(left.right() + gap, left.top(), panel.right() - left.right() - Theme::px(16),
                 left.height());

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

    // Right presets for the selected bank
    p.setBrush(QColor(24, 24, 30));
    p.setPen(QPen(Theme::stroke(), 1.0));
    p.drawRoundedRect(right, Theme::px(10), Theme::px(10));

    const QString activeBank =
        m_categories.isEmpty() ? QString()
                               : m_categories[qBound(0, m_selectedCategory, m_categories.size() - 1)];
    const float rowH = Theme::pxF(30.0f);
    float y = right.top() + Theme::px(8);
    m_presetRows.clear();
    for (const QString &item : m_fluidPresets) {
        PresetRow row;
        row.header = false;
        row.label = item;
        row.presetId = item;
        row.rect = QRectF(right.left() + Theme::px(12), y,
                          right.width() - Theme::px(20), rowH - Theme::px(4));
        m_presetRows.push_back(row);
        y += rowH - Theme::px(2);
    }

    for (const PresetRow &row : m_presetRows) {
        const bool bankMatch =
            QString::compare(activeBank, bankName, Qt::CaseInsensitive) == 0;
        const bool active =
            bankMatch && QString::compare(programName, row.presetId, Qt::CaseInsensitive) == 0;
        p.setBrush(active ? Theme::accentAlt() : Theme::bg2());
        p.setPen(QPen(Theme::stroke(), 1.0));
        p.drawRoundedRect(row.rect, Theme::px(6), Theme::px(6));
        p.setPen(active ? Theme::bg0() : Theme::text());
        p.setFont(Theme::baseFont(10, QFont::DemiBold));
        p.drawText(row.rect.adjusted(Theme::px(8), 0, -Theme::px(4), 0),
                   Qt::AlignLeft | Qt::AlignVCenter, row.label);
    }
}
