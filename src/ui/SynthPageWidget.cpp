#include "SynthPageWidget.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

#include "PadBank.h"
#include "Theme.h"

namespace {
enum EditParamType {
    EditFm = 0,
    EditRatio = 1,
    EditFeedback = 2,
    EditCutoff = 3,
    EditResonance = 4,
    EditAttack = 5,
    EditDecay = 6,
    EditSustain = 7,
    EditRelease = 8,
    EditLfoRate = 9,
    EditLfoDepth = 10,
    EditMacro1 = 11,
    EditMacro2 = 12,
    EditMacro3 = 13,
    EditMacro4 = 14,
    EditMacro5 = 15,
    EditMacro6 = 16,
    EditMacro7 = 17,
    EditMacro8 = 18
};

constexpr int kMainParamCount = 11;
constexpr float kTwoPi = 6.28318530717958647692f;

QVector<int> visibleParamIndicesForType(const QString &type) {
    const QString upper = type.trimmed().toUpper();
    QVector<int> indices;
    if (upper == "DX7") {
        indices << EditAttack << EditDecay << EditSustain << EditRelease;
        return indices;
    }
    for (int i = 0; i <= EditLfoDepth; ++i) {
        indices << i;
    }
    for (int i = EditMacro1; i <= EditMacro8; ++i) {
        indices << i;
    }
    return indices;
}

int firstVisibleParam(const QString &type) {
    const QVector<int> indices = visibleParamIndicesForType(type);
    return indices.isEmpty() ? 0 : indices.first();
}

float clamp01(float value) {
    return qBound(0.0f, value, 1.0f);
}

QString defaultSynthType() {
    const QStringList types = PadBank::synthTypes();
    return types.isEmpty() ? QStringLiteral("DX7") : types.first();
}

QString defaultSynthBank() {
    const QStringList banks = PadBank::synthBanks();
    if (banks.isEmpty()) {
        return QStringLiteral("INTERNAL");
    }
    const QString type = defaultSynthType().trimmed().toUpper();
    if (type == "SERUM" || type == "FM") {
        return QStringLiteral("SERUM");
    }
    for (const QString &bank : banks) {
        if (bank.trimmed().toUpper() != "SERUM" && bank.trimmed().toUpper() != "FM") {
            return bank;
        }
    }
    return banks.first();
}

QString defaultSynthProgram(const QString &bank) {
    const QStringList presets = PadBank::synthPresetsForBank(bank);
    return presets.isEmpty() ? QStringLiteral("INIT") : presets.first();
}

QString synthIdOrDefault(PadBank *pads, int pad) {
    const QString bank = defaultSynthBank();
    const QString program = defaultSynthProgram(bank);
    const QString type = defaultSynthType();
    const QString fallback = bank.isEmpty()
                                 ? QString("%1:%2").arg(type, program)
                                 : QString("%1:%2/%3").arg(type, bank, program);
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

QString synthTypeFromId(const QString &id);

QString synthBank(const QString &id) {
    const QString type = synthTypeFromId(id).trimmed().toUpper();
    if (type == "SERUM" || type == "FM") {
        return QStringLiteral("SERUM");
    }
    const QString preset = synthPreset(id);
    const int slash = preset.indexOf('/');
    if (slash >= 0) {
        return preset.left(slash).trimmed();
    }
    const QStringList banks = PadBank::synthBanks();
    for (const QString &bank : banks) {
        const QString upper = bank.trimmed().toUpper();
        if (upper != "SERUM" && upper != "FM") {
            return bank;
        }
    }
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

QString synthTypeFromId(const QString &id) {
    if (id.isEmpty()) {
        return defaultSynthType();
    }
    const int colon = id.indexOf(':');
    if (colon >= 0) {
        return id.left(colon).trimmed().toUpper();
    }
    return defaultSynthType();
}

bool isFmBank(const QString &bank) {
    const QString upper = bank.trimmed().toUpper();
    return upper == QStringLiteral("FM") || upper == QStringLiteral("SERUM");
}
}  // namespace

SynthPageWidget::SynthPageWidget(PadBank *pads, QWidget *parent)
    : QWidget(parent), m_pads(pads) {
    setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);
    if (m_pads) {
        m_activePad = m_pads->activePad();
    }
    reloadBanks(true);
    m_selectedEditParam =
        firstVisibleParam(synthTypeFromId(synthIdOrDefault(m_pads, m_activePad)));
    m_editParams = {
        {"FM", EditFm},
        {"RATIO", EditRatio},
        {"FEEDBACK", EditFeedback},
        {"CUTOFF", EditCutoff},
        {"RESO", EditResonance},
        {"ATTACK", EditAttack},
        {"DECAY", EditDecay},
        {"SUSTAIN", EditSustain},
        {"RELEASE", EditRelease},
        {"LFO RATE", EditLfoRate},
        {"LFO DEPTH", EditLfoDepth},
        {"MACRO 1", EditMacro1},
        {"MACRO 2", EditMacro2},
        {"MACRO 3", EditMacro3},
        {"MACRO 4", EditMacro4},
        {"MACRO 5", EditMacro5},
        {"MACRO 6", EditMacro6},
        {"MACRO 7", EditMacro7},
        {"MACRO 8", EditMacro8},
    };

    if (m_pads) {
        connect(m_pads, &PadBank::activePadChanged, this, [this](int index) {
            m_activePad = index;
            m_selectedEditParam =
                firstVisibleParam(synthTypeFromId(synthIdOrDefault(m_pads, m_activePad)));
            reloadBanks(true);
            update();
        });
        connect(m_pads, &PadBank::padChanged, this, [this](int) {
            m_selectedEditParam =
                firstVisibleParam(synthTypeFromId(synthIdOrDefault(m_pads, m_activePad)));
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

    const QString id = synthIdOrDefault(m_pads, m_activePad);
    const QString type = synthTypeFromId(id).trimmed().toUpper();
    if (!m_categories.isEmpty()) {
        if (type == "SERUM" || type == "FM") {
            m_categories = {QStringLiteral("SERUM")};
        } else {
            QStringList filtered;
            for (const QString &bank : m_categories) {
                const QString upper = bank.trimmed().toUpper();
                if (upper != "SERUM" && upper != "FM") {
                    filtered << bank;
                }
            }
            if (!filtered.isEmpty()) {
                m_categories = filtered;
            }
        }
    }
    if (m_categories.isEmpty()) {
        m_categories << (type == "SERUM" || type == "FM" ? "SERUM" : "INTERNAL");
    }
    if (syncSelection) {
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
        m_fluidPresets << "INIT";
    }
}

void SynthPageWidget::setActivePad(int pad) {
    m_activePad = pad;
    m_selectedEditParam =
        firstVisibleParam(synthTypeFromId(synthIdOrDefault(m_pads, m_activePad)));
    reloadBanks(true);
    update();
}

void SynthPageWidget::keyPressEvent(QKeyEvent *event) {
    const int key = event->key();
    if (key == Qt::Key_Space && m_pads) {
        m_pads->triggerPad(m_activePad);
        return;
    }
    if (key == Qt::Key_P) {
        m_showPresetMenu = !m_showPresetMenu;
        update();
        return;
    }
    if (key == Qt::Key_Escape && m_showPresetMenu) {
        m_showPresetMenu = false;
        update();
        return;
    }
    if (m_editParams.isEmpty() || !m_pads) {
        return;
    }
    const QVector<int> visible =
        visibleParamIndicesForType(synthTypeFromId(synthIdOrDefault(m_pads, m_activePad)));
    if (visible.isEmpty()) {
        return;
    }
    int pos = visible.indexOf(m_selectedEditParam);
    if (pos < 0) {
        pos = 0;
        m_selectedEditParam = visible.front();
    }
    if (key == Qt::Key_Down) {
        pos = (pos + 1) % visible.size();
        m_selectedEditParam = visible[pos];
        update();
        return;
    }
    if (key == Qt::Key_Up) {
        pos = (pos - 1 + visible.size()) % visible.size();
        m_selectedEditParam = visible[pos];
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

    if (m_presetButtonRect.contains(pos)) {
        m_showPresetMenu = !m_showPresetMenu;
        update();
        return;
    }

    if (m_showPresetMenu) {
        if (!m_presetPanelRect.contains(pos)) {
            m_showPresetMenu = false;
            update();
            return;
        }

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
                const QString bank =
                    m_categories.value(qBound(0, m_selectedCategory, m_categories.size() - 1));
                const QString type = isFmBank(bank) ? QStringLiteral("SERUM") : QStringLiteral("DX7");
                const QString presetId = row.presetId;
                const QString payload =
                    (type == "SERUM" || bank.isEmpty()) ? presetId
                                                        : QString("%1/%2").arg(bank, presetId);
                m_pads->setSynth(m_activePad, QString("%1:%2").arg(type, payload));
                m_showPresetMenu = false;
                update();
                return;
            }
        }

        m_showPresetMenu = false;
        update();
        return;
    }

    for (int i = 0; i < m_editParams.size(); ++i) {
        if (m_editParams[i].rect.contains(pos)) {
            m_selectedEditParam = i;
            update();
            return;
        }
    }

    for (int i = 0; i < m_macroRects.size(); ++i) {
        if (m_macroRects[i].contains(pos)) {
            const int macroIndex = EditMacro1 + i;
            if (macroIndex >= 0 && macroIndex < m_editParams.size()) {
                m_selectedEditParam = macroIndex;
                update();
            }
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

    const int margin = Theme::px(18);
    const QRectF panel = rect().adjusted(margin, margin, -margin, -margin);
    p.setBrush(Theme::bg1());
    p.setPen(QPen(Theme::stroke(), 1.2));
    p.drawRoundedRect(panel, Theme::px(14), Theme::px(14));

    const QRectF header(panel.left() + Theme::px(12), panel.top() + Theme::px(8),
                        panel.width() - Theme::px(24), Theme::px(30));

    const float buttonW = Theme::pxF(96.0f);
    const float buttonH = header.height() - Theme::pxF(4.0f);
    m_presetButtonRect = QRectF(header.left(), header.top() + Theme::pxF(2.0f),
                                buttonW, buttonH);
    p.setBrush(m_showPresetMenu ? Theme::accentAlt() : Theme::bg2());
    p.setPen(QPen(Theme::stroke(), 1.0));
    p.drawRoundedRect(m_presetButtonRect, Theme::px(8), Theme::px(8));
    p.setPen(m_showPresetMenu ? Theme::bg0() : Theme::text());
    p.setFont(Theme::condensedFont(10, QFont::Bold));
    p.drawText(m_presetButtonRect, Qt::AlignCenter, "PRESETS");

    const QString id = synthIdOrDefault(m_pads, m_activePad);
    const QString bankName = synthBank(id);
    const QString programName = synthProgram(id);
    const QString synthType = synthTypeFromId(id);

    QRectF presetNameRect = header;
    presetNameRect.setLeft(m_presetButtonRect.right() + Theme::pxF(10.0f));
    p.setPen(Theme::text());
    p.setFont(Theme::baseFont(12, QFont::DemiBold));
    QString displayPreset = programName;
    if (!bankName.isEmpty() && !isFmBank(bankName)) {
        displayPreset = QString("%1 / %2").arg(bankName, programName);
    }
    if (displayPreset.isEmpty()) {
        displayPreset = "INIT";
    }
    p.drawText(presetNameRect, Qt::AlignLeft | Qt::AlignVCenter, displayPreset);

    p.setPen(Theme::textMuted());
    p.setFont(Theme::baseFont(9, QFont::DemiBold));
    p.drawText(header, Qt::AlignRight | Qt::AlignVCenter,
               QString("PAD %1  %2").arg(m_activePad + 1).arg(synthType));

    const QString synthTypeUpper = synthType.trimmed().toUpper();
    const bool isDx7 = (synthTypeUpper == "DX7");

    const float gap = Theme::pxF(12.0f);
    QRectF content(panel.left() + Theme::px(12), header.bottom() + Theme::px(10),
                   panel.width() - Theme::px(24),
                   panel.bottom() - header.bottom() - Theme::px(16));

    auto drawPanel = [&](const QRectF &r, const QString &label) {
        p.setBrush(Theme::bg2());
        p.setPen(QPen(Theme::stroke(), 1.0));
        p.drawRoundedRect(r, Theme::px(10), Theme::px(10));
        p.setPen(Theme::textMuted());
        p.setFont(Theme::baseFont(9, QFont::DemiBold));
        p.drawText(QRectF(r.left() + Theme::px(8), r.top() + Theme::px(4),
                          r.width() - Theme::px(16), Theme::px(14)),
                   Qt::AlignLeft | Qt::AlignVCenter, label);
    };

    auto drawWave = [&](const QRectF &r, const QColor &color, auto fn) {
        const int steps = Theme::liteMode() ? 36 : 72;
        QPainterPath path;
        for (int i = 0; i <= steps; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(steps);
            float value = fn(t);
            value = std::max(-1.0f, std::min(1.0f, value));
            const float x = r.left() + t * r.width();
            const float y = r.center().y() - value * r.height() * 0.35f;
            if (i == 0) {
                path.moveTo(x, y);
            } else {
                path.lineTo(x, y);
            }
        }
        p.setPen(QPen(color, Theme::pxF(1.6f)));
        p.drawPath(path);
    };

    const PadBank::SynthParams sp = m_pads ? m_pads->synthParams(m_activePad)
                                           : PadBank::SynthParams();
    auto formatValue = [&](int type) {
        switch (type) {
            case EditFm:
                return QString("%1%").arg(qRound(clamp01(sp.fmAmount) * 100.0f));
            case EditRatio:
                return QString::number(sp.ratio, 'f', 2);
            case EditFeedback:
                return QString("%1%").arg(qRound(clamp01(sp.feedback) * 100.0f));
            case EditCutoff:
                return QString("%1%").arg(qRound(clamp01(sp.cutoff) * 100.0f));
            case EditResonance:
                return QString("%1%").arg(qRound(clamp01(sp.resonance) * 100.0f));
            case EditAttack: {
                const float sec = 0.005f + clamp01(sp.attack) * 1.2f;
                return QString("%1 ms").arg(qRound(sec * 1000.0f));
            }
            case EditDecay: {
                const float sec = 0.01f + clamp01(sp.decay) * 1.2f;
                return QString("%1 ms").arg(qRound(sec * 1000.0f));
            }
            case EditSustain:
                return QString("%1%").arg(qRound(clamp01(sp.sustain) * 100.0f));
            case EditRelease: {
                const float sec = 0.02f + clamp01(sp.release) * 1.6f;
                return QString("%1 ms").arg(qRound(sec * 1000.0f));
            }
            case EditLfoRate: {
                const float hz = 0.1f + clamp01(sp.lfoRate) * 8.0f;
                return QString("%1 Hz").arg(hz, 0, 'f', 2);
            }
            case EditLfoDepth:
                return QString("%1%").arg(qRound(clamp01(sp.lfoDepth) * 100.0f));
            default:
                break;
        }
        return QString("0");
    };
    if (isDx7) {
        for (auto &param : m_editParams) {
            param.rect = QRectF();
        }
        m_macroRects.clear();

        QRectF adsrPanel = content;
        drawPanel(adsrPanel, "DX7 ADSR");
        QRectF inner = adsrPanel.adjusted(Theme::px(10), Theme::px(22), -Theme::px(10), -Theme::px(10));

        const float graphH = inner.height() * 0.55f;
        QRectF adsrWave = inner;
        adsrWave.setHeight(graphH);
        {
            const float a = 0.1f + clamp01(sp.attack) * 0.45f;
            const float d = 0.1f + clamp01(sp.decay) * 0.35f;
            const float r = 0.1f + clamp01(sp.release) * 0.4f;
            float total = a + d + r + 0.1f;
            float scale = 1.0f;
            if (total > 0.95f) {
                scale = 0.95f / (a + d + r);
            }
            const float aa = a * scale;
            const float dd = d * scale;
            const float rr = r * scale;
            float sustainLen = 1.0f - (aa + dd + rr);
            sustainLen = std::max(0.05f, sustainLen);
            const float s = clamp01(sp.sustain);

            const float x0 = adsrWave.left();
            const float x1 = x0 + adsrWave.width() * aa;
            const float x2 = x1 + adsrWave.width() * dd;
            const float x3 = x2 + adsrWave.width() * sustainLen;
            const float x4 = adsrWave.right();

            const float y0 = adsrWave.bottom();
            const float y1 = adsrWave.top();
            const float y2 = adsrWave.top() + (1.0f - s) * adsrWave.height();

            QPainterPath env;
            env.moveTo(x0, y0);
            env.lineTo(x1, y1);
            env.lineTo(x2, y2);
            env.lineTo(x3, y2);
            env.lineTo(x4, y0);
            p.setPen(QPen(Theme::accent(), Theme::pxF(1.6f)));
            p.drawPath(env);
        }

        QRectF controlRect = inner;
        controlRect.setTop(adsrWave.bottom() + Theme::pxF(10.0f));
        controlRect.setHeight(inner.bottom() - controlRect.top());

        const QVector<int> adsrIndices = {EditAttack, EditDecay, EditSustain, EditRelease};
        const float colGap = Theme::pxF(10.0f);
        const float rowGap = Theme::pxF(8.0f);
        const float cellW = (controlRect.width() - colGap) / 2.0f;
        const float cellH = (controlRect.height() - rowGap) / 2.0f;
        int idx = 0;
        for (int row = 0; row < 2; ++row) {
            for (int col = 0; col < 2; ++col) {
                if (idx >= adsrIndices.size()) {
                    break;
                }
                const int paramIndex = adsrIndices[idx++];
                if (paramIndex < 0 || paramIndex >= m_editParams.size()) {
                    continue;
                }
                EditParam &param = m_editParams[paramIndex];
                QRectF r(controlRect.left() + col * (cellW + colGap),
                         controlRect.top() + row * (cellH + rowGap),
                         cellW, cellH);
                param.rect = r;
                const bool selected = (paramIndex == m_selectedEditParam);
                p.setBrush(selected ? Theme::accentAlt() : Theme::bg3());
                p.setPen(QPen(Theme::stroke(), 1.0));
                p.drawRoundedRect(r, Theme::px(6), Theme::px(6));
                p.setPen(selected ? Theme::bg0() : Theme::text());
                p.setFont(Theme::baseFont(10, QFont::DemiBold));
                p.drawText(r.adjusted(Theme::px(8), 0, -Theme::px(8), 0),
                           Qt::AlignLeft | Qt::AlignVCenter, param.label);
                p.setPen(selected ? Theme::bg0() : Theme::textMuted());
                p.drawText(r.adjusted(Theme::px(8), 0, -Theme::px(8), 0),
                           Qt::AlignRight | Qt::AlignVCenter, formatValue(param.type));
            }
        }
    } else {
        const float topH = content.height() * 0.42f;
        QRectF oscBlock(content.left(), content.top(), content.width() * 0.58f, topH);
        QRectF rightBlock(oscBlock.right() + gap, content.top(),
                          content.right() - oscBlock.right() - gap, topH);

        const float ratio = std::max(0.1f, sp.ratio);
        const float fm = clamp01(sp.fmAmount);
        const float feedback = clamp01(sp.feedback);

        const float oscGap = Theme::pxF(10.0f);
        QRectF modRect = oscBlock;
        modRect.setHeight((oscBlock.height() - oscGap) * 0.5f);
        QRectF fmRect = modRect;
        fmRect.moveTop(modRect.bottom() + oscGap);

        drawPanel(modRect, "OP2 MOD");
        QRectF modWaveRect = modRect.adjusted(Theme::px(8), Theme::px(20), -Theme::px(8), -Theme::px(8));
        drawWave(modWaveRect, Theme::accentAlt(), [ratio](float t) {
            const float phase = kTwoPi * t * ratio;
            return std::sin(phase);
        });

        drawPanel(fmRect, "OP1 FM");
        QRectF fmWaveRect = fmRect.adjusted(Theme::px(8), Theme::px(20), -Theme::px(8), -Theme::px(8));
        drawWave(fmWaveRect, Theme::accent(), [ratio, fm, feedback](float t) {
            const float phaseM = kTwoPi * t * ratio;
            const float mod = std::sin(phaseM + feedback * std::sin(phaseM));
            const float phaseC = kTwoPi * t;
            return std::sin(phaseC + fm * mod);
        });

        QRectF adsrRect = rightBlock;
        adsrRect.setHeight((rightBlock.height() - oscGap) * 0.55f);
        QRectF lfoRect = adsrRect;
        lfoRect.moveTop(adsrRect.bottom() + oscGap);
        lfoRect.setHeight(rightBlock.bottom() - lfoRect.top());

        drawPanel(adsrRect, "ADSR");
        QRectF adsrWave = adsrRect.adjusted(Theme::px(8), Theme::px(20), -Theme::px(8), -Theme::px(8));
        {
            const float a = 0.1f + clamp01(sp.attack) * 0.45f;
            const float d = 0.1f + clamp01(sp.decay) * 0.35f;
            const float r = 0.1f + clamp01(sp.release) * 0.4f;
            float total = a + d + r + 0.1f;
            float scale = 1.0f;
            if (total > 0.95f) {
                scale = 0.95f / (a + d + r);
            }
            const float aa = a * scale;
            const float dd = d * scale;
            const float rr = r * scale;
            float sustainLen = 1.0f - (aa + dd + rr);
            sustainLen = std::max(0.05f, sustainLen);
            const float s = clamp01(sp.sustain);

            const float x0 = adsrWave.left();
            const float x1 = x0 + adsrWave.width() * aa;
            const float x2 = x1 + adsrWave.width() * dd;
            const float x3 = x2 + adsrWave.width() * sustainLen;
            const float x4 = adsrWave.right();

            const float y0 = adsrWave.bottom();
            const float y1 = adsrWave.top();
            const float y2 = adsrWave.top() + (1.0f - s) * adsrWave.height();

            QPainterPath env;
            env.moveTo(x0, y0);
            env.lineTo(x1, y1);
            env.lineTo(x2, y2);
            env.lineTo(x3, y2);
            env.lineTo(x4, y0);
            p.setPen(QPen(Theme::accent(), Theme::pxF(1.6f)));
            p.drawPath(env);
        }

        drawPanel(lfoRect, "LFO");
        QRectF lfoWave = lfoRect.adjusted(Theme::px(8), Theme::px(20), -Theme::px(8), -Theme::px(8));
        {
            const float rate = clamp01(sp.lfoRate);
            const float depth = clamp01(sp.lfoDepth);
            const float cycles = 1.0f + rate * 3.0f;
            drawWave(lfoWave, Theme::accentAlt(), [cycles, depth](float t) {
                const float phase = kTwoPi * t * cycles;
                return std::sin(phase) * (0.25f + depth * 0.6f);
            });
        }

        QRectF bottomRect(content.left(), oscBlock.bottom() + gap,
                          content.width(), content.bottom() - oscBlock.bottom() - gap);

        const float controlW = bottomRect.width() * 0.68f;
        QRectF controlRect(bottomRect.left(), bottomRect.top(), controlW, bottomRect.height());
        QRectF macroRect(controlRect.right() + gap, bottomRect.top(),
                         bottomRect.right() - controlRect.right() - gap, bottomRect.height());

        p.setBrush(Theme::bg2());
        p.setPen(QPen(Theme::stroke(), 1.0));
        p.drawRoundedRect(controlRect, Theme::px(10), Theme::px(10));

        p.setPen(Theme::textMuted());
        p.setFont(Theme::baseFont(9, QFont::DemiBold));
        p.drawText(QRectF(controlRect.left() + Theme::px(10), controlRect.top() + Theme::px(6),
                          controlRect.width() - Theme::px(20), Theme::px(16)),
                   Qt::AlignLeft | Qt::AlignVCenter, "CONTROLS");

        const float headerH = Theme::pxF(22.0f);
        const float availableH =
            std::max(Theme::pxF(10.0f), static_cast<float>(controlRect.height() - headerH - Theme::pxF(8.0f)));
        const int rows = (kMainParamCount + 1) / 2;
        const float rowH = std::min(Theme::pxF(28.0f), availableH / rows);
        const float colGap = Theme::pxF(10.0f);
        const float colW = (controlRect.width() - colGap - Theme::pxF(20.0f)) / 2.0f;

        float y = controlRect.top() + headerH + Theme::pxF(4.0f);
        int paramIndex = 0;
        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < 2; ++col) {
                if (paramIndex >= kMainParamCount || paramIndex >= m_editParams.size()) {
                    break;
                }
                EditParam &param = m_editParams[paramIndex];
                QRectF r(controlRect.left() + Theme::pxF(10.0f) + col * (colW + colGap),
                         y, colW, rowH - Theme::pxF(4.0f));
                param.rect = r;
                const bool selected = (paramIndex == m_selectedEditParam);
                p.setBrush(selected ? Theme::accentAlt() : Theme::bg3());
                p.setPen(QPen(Theme::stroke(), 1.0));
                p.drawRoundedRect(r, Theme::px(6), Theme::px(6));
                p.setPen(selected ? Theme::bg0() : Theme::text());
                p.setFont(Theme::baseFont(9, QFont::DemiBold));
                p.drawText(r.adjusted(Theme::px(6), 0, -Theme::px(6), 0),
                           Qt::AlignLeft | Qt::AlignVCenter, param.label);
                p.setPen(selected ? Theme::bg0() : Theme::textMuted());
                p.drawText(r.adjusted(Theme::px(6), 0, -Theme::px(6), 0),
                           Qt::AlignRight | Qt::AlignVCenter, formatValue(param.type));
                paramIndex++;
            }
            y += rowH;
        }

        for (int i = kMainParamCount; i < m_editParams.size(); ++i) {
            m_editParams[i].rect = QRectF();
        }

        p.setBrush(Theme::bg2());
        p.setPen(QPen(Theme::stroke(), 1.0));
        p.drawRoundedRect(macroRect, Theme::px(10), Theme::px(10));

        p.setPen(Theme::textMuted());
        p.setFont(Theme::baseFont(9, QFont::DemiBold));
        p.drawText(QRectF(macroRect.left() + Theme::px(10), macroRect.top() + Theme::px(6),
                          macroRect.width() - Theme::px(20), Theme::px(16)),
                   Qt::AlignLeft | Qt::AlignVCenter, "MACROS");

        m_macroRects.clear();
        const int macroRows = 4;
        const int macroCols = 2;
        const float macroGap = Theme::pxF(8.0f);
        const float macroHeader = Theme::pxF(22.0f);
        const float macroAvailableH =
            std::max(Theme::pxF(10.0f), static_cast<float>(macroRect.height() - macroHeader - Theme::pxF(8.0f)));
        const float macroH = (macroAvailableH - macroGap * (macroRows - 1)) / macroRows;
        const float macroW = (macroRect.width() - Theme::pxF(20.0f) - macroGap) / macroCols;

        float my = macroRect.top() + macroHeader + Theme::pxF(2.0f);
        for (int row = 0; row < macroRows; ++row) {
            float mx = macroRect.left() + Theme::pxF(10.0f);
            for (int col = 0; col < macroCols; ++col) {
                const int macroIndex = row * macroCols + col;
                if (macroIndex >= 8) {
                    break;
                }
                QRectF r(mx, my, macroW, macroH);
                m_macroRects.push_back(r);
                const int paramType = EditMacro1 + macroIndex;
                const bool selected = (m_selectedEditParam == paramType);
                p.setBrush(selected ? Theme::accentAlt() : Theme::bg3());
                p.setPen(QPen(Theme::stroke(), 1.0));
                p.drawRoundedRect(r, Theme::px(6), Theme::px(6));

                const float value = clamp01(sp.macros[static_cast<size_t>(macroIndex)]);
                const float fillW = r.width() * value;
                QRectF fillRect(r.left(), r.top(), fillW, r.height());
                p.setBrush(selected ? Theme::bg0() : Theme::accent());
                p.setPen(Qt::NoPen);
                p.drawRoundedRect(fillRect, Theme::px(6), Theme::px(6));

                p.setPen(selected ? Theme::bg0() : Theme::text());
                p.setFont(Theme::baseFont(9, QFont::DemiBold));
                p.drawText(r.adjusted(Theme::px(6), 0, -Theme::px(6), 0),
                           Qt::AlignLeft | Qt::AlignVCenter, QString("M%1").arg(macroIndex + 1));
                p.setPen(selected ? Theme::bg0() : Theme::textMuted());
                p.drawText(r.adjusted(Theme::px(6), 0, -Theme::px(6), 0),
                           Qt::AlignRight | Qt::AlignVCenter,
                           QString("%1%").arg(qRound(value * 100.0f)));

                mx += macroW + macroGap;
            }
            my += macroH + macroGap;
        }
    }

    if (m_showPresetMenu) {
        const float menuW = panel.width() * 0.52f;
        const float menuH = panel.height() * 0.6f;
        m_presetPanelRect = QRectF(panel.left() + Theme::px(12), header.bottom() + Theme::px(8),
                                   menuW, menuH);
        p.setBrush(Theme::bg2());
        p.setPen(QPen(Theme::stroke(), 1.2));
        p.drawRoundedRect(m_presetPanelRect, Theme::px(12), Theme::px(12));

        const bool showBanks = m_categories.size() > 1;
        const float bankW = showBanks ? m_presetPanelRect.width() * 0.35f : 0.0f;
        QRectF bankRect = m_presetPanelRect.adjusted(Theme::px(10), Theme::px(10), -Theme::px(10), -Theme::px(10));
        QRectF presetRect = bankRect;
        if (showBanks) {
            presetRect.setLeft(bankRect.left() + bankW + Theme::pxF(10.0f));
            bankRect.setWidth(bankW);
        }

        m_categoryRects.clear();
        if (showBanks) {
            const float rowH = Theme::pxF(28.0f);
            float by = bankRect.top();
            for (int i = 0; i < m_categories.size(); ++i) {
                QRectF r(bankRect.left(), by, bankRect.width(), rowH - Theme::pxF(4.0f));
                m_categoryRects.push_back(r);
                const bool active = (i == m_selectedCategory);
                p.setBrush(active ? Theme::accentAlt() : Theme::bg3());
                p.setPen(QPen(Theme::stroke(), 1.0));
                p.drawRoundedRect(r, Theme::px(6), Theme::px(6));
                p.setPen(active ? Theme::bg0() : Theme::text());
                p.setFont(Theme::baseFont(9, QFont::DemiBold));
                p.drawText(r, Qt::AlignCenter, m_categories[i]);
                by += rowH;
                if (by > bankRect.bottom() - Theme::pxF(6.0f)) {
                    break;
                }
            }
        }

        m_presetRows.clear();
        const float rowH = Theme::pxF(28.0f);
        float py = presetRect.top();
        for (const QString &item : m_fluidPresets) {
            PresetRow row;
            row.header = false;
            row.label = item;
            row.presetId = item;
            row.rect = QRectF(presetRect.left(), py, presetRect.width(), rowH - Theme::pxF(4.0f));
            m_presetRows.push_back(row);
            py += rowH;
            if (py > presetRect.bottom() - Theme::pxF(6.0f)) {
                break;
            }
        }

        for (const PresetRow &row : m_presetRows) {
            const bool bankMatch =
                QString::compare(m_categories.value(qBound(0, m_selectedCategory, m_categories.size() - 1)),
                                 bankName, Qt::CaseInsensitive) == 0;
            const bool active = bankMatch &&
                                QString::compare(programName, row.presetId, Qt::CaseInsensitive) == 0;
            p.setBrush(active ? Theme::accentAlt() : Theme::bg3());
            p.setPen(QPen(Theme::stroke(), 1.0));
            p.drawRoundedRect(row.rect, Theme::px(6), Theme::px(6));
            p.setPen(active ? Theme::bg0() : Theme::text());
            p.setFont(Theme::baseFont(9, QFont::DemiBold));
            p.drawText(row.rect.adjusted(Theme::px(6), 0, -Theme::px(4), 0),
                       Qt::AlignLeft | Qt::AlignVCenter, row.label);
        }
    } else {
        m_presetPanelRect = QRectF();
    }
}

float SynthPageWidget::currentEditValue(const EditParam &param) const {
    if (!m_pads) {
        return 0.0f;
    }
    const PadBank::SynthParams sp = m_pads->synthParams(m_activePad);
    switch (param.type) {
        case EditFm:
            return sp.fmAmount;
        case EditRatio:
            return sp.ratio;
        case EditFeedback:
            return sp.feedback;
        case EditCutoff:
            return sp.cutoff;
        case EditResonance:
            return sp.resonance;
        case EditAttack:
            return sp.attack;
        case EditDecay:
            return sp.decay;
        case EditSustain:
            return sp.sustain;
        case EditRelease:
            return sp.release;
        case EditLfoRate:
            return sp.lfoRate;
        case EditLfoDepth:
            return sp.lfoDepth;
        case EditMacro1:
            return sp.macros[0];
        case EditMacro2:
            return sp.macros[1];
        case EditMacro3:
            return sp.macros[2];
        case EditMacro4:
            return sp.macros[3];
        case EditMacro5:
            return sp.macros[4];
        case EditMacro6:
            return sp.macros[5];
        case EditMacro7:
            return sp.macros[6];
        case EditMacro8:
            return sp.macros[7];
        default:
            break;
    }
    return 0.0f;
}

void SynthPageWidget::adjustEditParam(int delta) {
    if (!m_pads || m_editParams.isEmpty()) {
        return;
    }
    const int pad = m_activePad;
    const EditParam &param = m_editParams[m_selectedEditParam];
    PadBank::SynthParams sp = m_pads->synthParams(pad);

    auto step = [delta](float base, float amount) {
        return base + amount * static_cast<float>(delta);
    };

    switch (param.type) {
        case EditFm:
            sp.fmAmount = clamp01(step(sp.fmAmount, 0.02f));
            m_pads->setSynthFm(pad, sp.fmAmount, sp.ratio, sp.feedback);
            break;
        case EditRatio:
            sp.ratio = qBound(0.1f, step(sp.ratio, 0.1f), 8.0f);
            m_pads->setSynthFm(pad, sp.fmAmount, sp.ratio, sp.feedback);
            break;
        case EditFeedback:
            sp.feedback = clamp01(step(sp.feedback, 0.02f));
            m_pads->setSynthFm(pad, sp.fmAmount, sp.ratio, sp.feedback);
            break;
        case EditCutoff:
            sp.cutoff = clamp01(step(sp.cutoff, 0.02f));
            m_pads->setSynthFilter(pad, sp.cutoff, sp.resonance);
            break;
        case EditResonance:
            sp.resonance = clamp01(step(sp.resonance, 0.02f));
            m_pads->setSynthFilter(pad, sp.cutoff, sp.resonance);
            break;
        case EditAttack:
            sp.attack = clamp01(step(sp.attack, 0.02f));
            m_pads->setSynthAdsr(pad, sp.attack, sp.decay, sp.sustain, sp.release);
            break;
        case EditDecay:
            sp.decay = clamp01(step(sp.decay, 0.02f));
            m_pads->setSynthAdsr(pad, sp.attack, sp.decay, sp.sustain, sp.release);
            break;
        case EditSustain:
            sp.sustain = clamp01(step(sp.sustain, 0.02f));
            m_pads->setSynthAdsr(pad, sp.attack, sp.decay, sp.sustain, sp.release);
            break;
        case EditRelease:
            sp.release = clamp01(step(sp.release, 0.02f));
            m_pads->setSynthAdsr(pad, sp.attack, sp.decay, sp.sustain, sp.release);
            break;
        case EditLfoRate:
            sp.lfoRate = clamp01(step(sp.lfoRate, 0.02f));
            m_pads->setSynthLfo(pad, sp.lfoRate, sp.lfoDepth);
            break;
        case EditLfoDepth:
            sp.lfoDepth = clamp01(step(sp.lfoDepth, 0.02f));
            m_pads->setSynthLfo(pad, sp.lfoRate, sp.lfoDepth);
            break;
        case EditMacro1:
        case EditMacro2:
        case EditMacro3:
        case EditMacro4:
        case EditMacro5:
        case EditMacro6:
        case EditMacro7:
        case EditMacro8: {
            const int macroIndex = param.type - EditMacro1;
            if (macroIndex >= 0 && macroIndex < 8) {
                float value = clamp01(step(sp.macros[static_cast<size_t>(macroIndex)], 0.02f));
                m_pads->setSynthMacro(pad, macroIndex, value);
            }
            break;
        }
        default:
            break;
    }
    update();
}
