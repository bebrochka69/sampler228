#include "SynthPageWidget.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include "PadBank.h"
#include "Theme.h"

namespace {
enum EditParamType {
    EditAlg = 0,
    EditFeedback = 1,
    EditOpSelect = 2,
    EditOpLevel = 3,
    EditOpCoarse = 4,
    EditOpFine = 5,
    EditOpDetune = 6
};

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
    m_editParams = {
        {"ALGORITHM", EditAlg},
        {"FEEDBACK", EditFeedback},
        {"OPERATOR", EditOpSelect},
        {"LEVEL", EditOpLevel},
        {"COARSE", EditOpCoarse},
        {"FINE", EditOpFine},
        {"DETUNE", EditOpDetune},
    };

    if (m_pads) {
        connect(m_pads, &PadBank::activePadChanged, this, [this](int index) {
            m_activePad = index;
            m_selectedOp = 0;
            m_selectedEditParam = 0;
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
    m_selectedOp = 0;
    m_selectedEditParam = 0;
    reloadBanks(true);
    update();
}

void SynthPageWidget::keyPressEvent(QKeyEvent *event) {
    const int key = event->key();
    if (key == Qt::Key_Space && m_pads) {
        m_pads->triggerPad(m_activePad);
        return;
    }
    if (m_editParams.isEmpty() || !m_pads) {
        return;
    }
    if (key == Qt::Key_Down) {
        m_selectedEditParam = (m_selectedEditParam + 1) % m_editParams.size();
        update();
        return;
    }
    if (key == Qt::Key_Up) {
        m_selectedEditParam =
            (m_selectedEditParam - 1 + m_editParams.size()) % m_editParams.size();
        update();
        return;
    }
    if (key == Qt::Key_Left || key == Qt::Key_Minus) {
        adjustEditParam(-1);
        return;
    }
    if (key == Qt::Key_Right || key == Qt::Key_Plus || key == Qt::Key_Equal) {
        adjustEditParam(1);
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

    for (int i = 0; i < m_editParams.size(); ++i) {
        if (m_editParams[i].rect.contains(pos)) {
            m_selectedEditParam = i;
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

    const float splitGap = Theme::pxF(10.0f);
    const float presetRatio = 0.6f;
    QRectF presetRect = right;
    QRectF editRect = right;
    presetRect.setHeight(right.height() * presetRatio - splitGap * 0.5f);
    editRect.setTop(presetRect.bottom() + splitGap);
    editRect.setHeight(right.bottom() - editRect.top());

    const QString activeBank =
        m_categories.isEmpty() ? QString()
                               : m_categories[qBound(0, m_selectedCategory, m_categories.size() - 1)];
    const float rowH = Theme::pxF(30.0f);
    float y = presetRect.top() + Theme::px(8);
    m_presetRows.clear();
    for (const QString &item : m_fluidPresets) {
        PresetRow row;
        row.header = false;
        row.label = item;
        row.presetId = item;
        row.rect = QRectF(presetRect.left() + Theme::px(12), y,
                          presetRect.width() - Theme::px(20), rowH - Theme::px(4));
        m_presetRows.push_back(row);
        y += rowH - Theme::px(2);
        if (y > presetRect.bottom() - Theme::px(8)) {
            break;
        }
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

    // Edit panel.
    p.setBrush(QColor(20, 20, 26));
    p.setPen(QPen(Theme::stroke(), 1.0));
    p.drawRoundedRect(editRect, Theme::px(8), Theme::px(8));

    p.setPen(Theme::textMuted());
    p.setFont(Theme::baseFont(9, QFont::DemiBold));
    p.drawText(QRectF(editRect.left() + Theme::px(10), editRect.top() + Theme::px(6),
                      editRect.width() - Theme::px(20), Theme::px(16)),
               Qt::AlignLeft | Qt::AlignVCenter, "EDIT (UP/DOWN + LEFT/RIGHT)");

    const float editRowH = Theme::pxF(26.0f);
    float ey = editRect.top() + Theme::px(26);
    for (int i = 0; i < m_editParams.size(); ++i) {
        EditParam &param = m_editParams[i];
        param.rect = QRectF(editRect.left() + Theme::px(10), ey,
                            editRect.width() - Theme::px(20), editRowH - Theme::px(4));
        const bool selected = (i == m_selectedEditParam);
        p.setBrush(selected ? Theme::accentAlt() : Theme::bg2());
        p.setPen(QPen(Theme::stroke(), 1.0));
        p.drawRoundedRect(param.rect, Theme::px(6), Theme::px(6));

        p.setPen(selected ? Theme::bg0() : Theme::text());
        p.setFont(Theme::baseFont(9, QFont::DemiBold));
        p.drawText(param.rect.adjusted(Theme::px(8), 0, -Theme::px(4), 0),
                   Qt::AlignLeft | Qt::AlignVCenter, param.label);

        const int value = currentEditValue(param);
        QString valueText;
        switch (param.type) {
            case EditAlg:
                valueText = QString::number(value + 1);
                break;
            case EditFeedback:
                valueText = QString::number(value);
                break;
            case EditOpSelect:
                valueText = QString("OP %1").arg(value + 1);
                break;
            case EditOpLevel:
                valueText = QString::number(value);
                break;
            case EditOpCoarse:
                valueText = QString::number(value);
                break;
            case EditOpFine:
                valueText = QString::number(value);
                break;
            case EditOpDetune:
                valueText = QString::number(value);
                break;
            default:
                valueText = QString::number(value);
                break;
        }
        p.setPen(selected ? Theme::bg0() : Theme::textMuted());
        p.drawText(param.rect.adjusted(Theme::px(8), 0, -Theme::px(4), 0),
                   Qt::AlignRight | Qt::AlignVCenter, valueText);

        ey += editRowH;
        if (ey > editRect.bottom() - Theme::px(6)) {
            break;
        }
    }
}

int SynthPageWidget::currentEditValue(const EditParam &param) const {
    if (!m_pads) {
        return 0;
    }
    const int pad = m_activePad;
    if (!m_pads->isSynth(pad)) {
        return 0;
    }
    const int opIndex = qBound(0, m_selectedOp, 5);
    const int base = opIndex * 21;
    switch (param.type) {
        case EditAlg:
            return m_pads->synthVoiceParam(pad, 134);
        case EditFeedback:
            return m_pads->synthVoiceParam(pad, 135);
        case EditOpSelect:
            return opIndex;
        case EditOpLevel:
            return m_pads->synthVoiceParam(pad, base + 16);
        case EditOpCoarse:
            return m_pads->synthVoiceParam(pad, base + 18);
        case EditOpFine:
            return m_pads->synthVoiceParam(pad, base + 19);
        case EditOpDetune:
            return m_pads->synthVoiceParam(pad, base + 20);
        default:
            return 0;
    }
}

void SynthPageWidget::adjustEditParam(int delta) {
    if (!m_pads || m_editParams.isEmpty()) {
        return;
    }
    const int pad = m_activePad;
    if (!m_pads->isSynth(pad)) {
        return;
    }
    const EditParam &param = m_editParams[m_selectedEditParam];
    const int opIndex = qBound(0, m_selectedOp, 5);
    const int base = opIndex * 21;
    switch (param.type) {
        case EditAlg: {
            const int cur = m_pads->synthVoiceParam(pad, 134);
            m_pads->setSynthVoiceParam(pad, 134, cur + delta);
            break;
        }
        case EditFeedback: {
            const int cur = m_pads->synthVoiceParam(pad, 135);
            m_pads->setSynthVoiceParam(pad, 135, cur + delta);
            break;
        }
        case EditOpSelect:
            m_selectedOp = qBound(0, m_selectedOp + delta, 5);
            break;
        case EditOpLevel: {
            const int cur = m_pads->synthVoiceParam(pad, base + 16);
            m_pads->setSynthVoiceParam(pad, base + 16, cur + delta);
            break;
        }
        case EditOpCoarse: {
            const int cur = m_pads->synthVoiceParam(pad, base + 18);
            m_pads->setSynthVoiceParam(pad, base + 18, cur + delta);
            break;
        }
        case EditOpFine: {
            const int cur = m_pads->synthVoiceParam(pad, base + 19);
            m_pads->setSynthVoiceParam(pad, base + 19, cur + delta);
            break;
        }
        case EditOpDetune: {
            const int cur = m_pads->synthVoiceParam(pad, base + 20);
            m_pads->setSynthVoiceParam(pad, base + 20, cur + delta);
            break;
        }
        default:
            break;
    }
    update();
}
