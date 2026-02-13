#include "SynthPageWidget.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

#include "PadBank.h"
#include "Theme.h"

namespace {
enum EditParamType {
    EditOsc1Wave = 0,
    EditOsc1Voices = 1,
    EditOsc1Detune = 2,
    EditOsc1Gain = 3,
    EditOsc1Pan = 4,
    EditOsc2Wave = 5,
    EditOsc2Voices = 6,
    EditOsc2Detune = 7,
    EditOsc2Gain = 8,
    EditOsc2Pan = 9,
    EditCutoff = 10,
    EditResonance = 11,
    EditFilterType = 12,
    EditAttack = 13,
    EditDecay = 14,
    EditSustain = 15,
    EditRelease = 16,
    EditLfoRate = 17,
    EditLfoDepth = 18
};

constexpr float kTwoPi = 6.28318530717958647692f;

QVector<int> visibleParamIndicesForType(const QString &type) {
    const QString upper = type.trimmed().toUpper();
    QVector<int> indices;
    if (upper == "DX7") {
        indices << EditAttack << EditDecay << EditSustain << EditRelease;
        return indices;
    }
    indices << EditOsc1Wave << EditOsc1Voices << EditOsc1Detune << EditOsc1Gain << EditOsc1Pan;
    indices << EditOsc2Wave << EditOsc2Voices << EditOsc2Detune << EditOsc2Gain << EditOsc2Pan;
    indices << EditCutoff << EditResonance << EditFilterType;
    indices << EditAttack << EditDecay << EditSustain << EditRelease;
    indices << EditLfoRate << EditLfoDepth;
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
        {"WAVE", EditOsc1Wave},
        {"VOICES", EditOsc1Voices},
        {"DETUNE", EditOsc1Detune},
        {"VOL", EditOsc1Gain},
        {"PAN", EditOsc1Pan},
        {"WAVE", EditOsc2Wave},
        {"VOICES", EditOsc2Voices},
        {"DETUNE", EditOsc2Detune},
        {"VOL", EditOsc2Gain},
        {"PAN", EditOsc2Pan},
        {"CUTOFF", EditCutoff},
        {"RESO", EditResonance},
        {"FILTER", EditFilterType},
        {"ATTACK", EditAttack},
        {"DECAY", EditDecay},
        {"SUSTAIN", EditSustain},
        {"RELEASE", EditRelease},
        {"LFO RATE", EditLfoRate},
        {"LFO DEPTH", EditLfoDepth},
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
    if (syncSelection) {
        m_presetScroll = 0;
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
    if (m_showPresetMenu) {
        if (key == Qt::Key_Down) {
            m_presetScroll += 1;
            update();
            return;
        }
        if (key == Qt::Key_Up) {
            m_presetScroll = std::max(0, m_presetScroll - 1);
            update();
            return;
        }
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
                m_presetScroll = 0;
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

    for (int i = 0; i < m_filterPresetRects.size(); ++i) {
        if (m_filterPresetRects[i].contains(pos) && m_pads) {
            m_pads->setSynthFilterType(m_activePad, i);
            m_selectedEditParam = EditFilterType;
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

void SynthPageWidget::wheelEvent(QWheelEvent *event) {
    if (!m_showPresetMenu) {
        QWidget::wheelEvent(event);
        return;
    }
    const QPointF pos = event->position();
    if (!m_presetPanelRect.contains(pos)) {
        QWidget::wheelEvent(event);
        return;
    }
    const int delta = event->angleDelta().y();
    if (delta < 0) {
        m_presetScroll += 1;
    } else if (delta > 0) {
        m_presetScroll = std::max(0, m_presetScroll - 1);
    }
    update();
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
    const QStringList waves = PadBank::serumWaves();
    const QStringList filterPresets = {"LOW", "HIGH", "BAND", "LOW+HIGH", "PEAK",
                                       "LOW SHELF", "HIGH SHELF", "ALLPASS", "BYPASS", "LOW+MID"};

    auto formatValue = [&](int type) {
        switch (type) {
            case EditOsc1Wave:
                return waves.value(sp.osc1Wave, "WAVE");
            case EditOsc1Voices:
                return QString::number(sp.osc1Voices);
            case EditOsc1Detune:
                return QString("%1%").arg(qRound(clamp01(sp.osc1Detune) * 100.0f));
            case EditOsc1Gain:
                return QString("%1%").arg(qRound(clamp01(sp.osc1Gain) * 100.0f));
            case EditOsc1Pan:
                return QString::number(sp.osc1Pan, 'f', 2);
            case EditOsc2Wave:
                return waves.value(sp.osc2Wave, "WAVE");
            case EditOsc2Voices:
                return QString::number(sp.osc2Voices);
            case EditOsc2Detune:
                return QString("%1%").arg(qRound(clamp01(sp.osc2Detune) * 100.0f));
            case EditOsc2Gain:
                return QString("%1%").arg(qRound(clamp01(sp.osc2Gain) * 100.0f));
            case EditOsc2Pan:
                return QString::number(sp.osc2Pan, 'f', 2);
            case EditCutoff:
                return QString("%1%").arg(qRound(clamp01(sp.cutoff) * 100.0f));
            case EditResonance:
                return QString("%1%").arg(qRound(clamp01(sp.resonance) * 100.0f));
            case EditFilterType:
                return filterPresets.value(sp.filterType, "FILTER");
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
        m_filterPresetRects.clear();

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
        for (auto &param : m_editParams) {
            param.rect = QRectF();
        }
        m_filterPresetRects.clear();

        const float topH = content.height() * 0.34f;
        const float midH = content.height() * 0.28f;
        const float bottomH = content.height() - topH - midH - gap * 2.0f;

        QRectF oscRow(content.left(), content.top(), content.width(), topH);
        QRectF modRow(content.left(), oscRow.bottom() + gap, content.width(), midH);
        QRectF filterRow(content.left(), modRow.bottom() + gap, content.width(), bottomH);

        const float oscGap = Theme::pxF(12.0f);
        QRectF osc1Rect(oscRow.left(), oscRow.top(),
                        oscRow.width() * 0.5f - oscGap * 0.5f, oscRow.height());
        QRectF osc2Rect(osc1Rect.right() + oscGap, oscRow.top(),
                        oscRow.right() - osc1Rect.right() - oscGap, oscRow.height());

        auto drawOscPanel = [&](const QRectF &r, const QString &label, int waveIndex,
                                int waveParam, int voicesParam, int detuneParam,
                                int gainParam, int panParam) {
            drawPanel(r, label);
            QRectF waveRect = r.adjusted(Theme::px(8), Theme::px(22), -Theme::px(8), -Theme::px(8));
            const float waveH = waveRect.height() * 0.55f;
            QRectF waveArea = waveRect;
            waveArea.setHeight(waveH);

            EditParam &waveParamRef = m_editParams[waveParam];
            waveParamRef.rect = QRectF(waveArea.left(), waveArea.top(), waveArea.width(), Theme::px(18));

            const QString waveName = waves.value(waveIndex, "WAVE");
            const bool waveSelected = (waveParam == m_selectedEditParam);
            p.setBrush(waveSelected ? Theme::accentAlt() : Theme::bg3());
            p.setPen(QPen(Theme::stroke(), 1.0));
            p.drawRoundedRect(waveParamRef.rect, Theme::px(6), Theme::px(6));
            p.setPen(waveSelected ? Theme::bg0() : Theme::text());
            p.setFont(Theme::baseFont(9, QFont::DemiBold));
            p.drawText(waveParamRef.rect.adjusted(Theme::px(6), 0, -Theme::px(6), 0),
                       Qt::AlignLeft | Qt::AlignVCenter, waveName);

            QRectF waveGraph = waveArea.adjusted(0, Theme::px(18), 0, 0);
            drawWave(waveGraph, Theme::accent(), [waveIndex](float t) {
                const float phase = kTwoPi * t;
                switch (waveIndex) {
                    case 0:
                        return std::sin(phase);
                    case 1:
                        return 2.0f * (t - 0.5f);
                    case 2:
                        return std::sin(phase) >= 0.0f ? 1.0f : -1.0f;
                    case 3:
                        return 1.0f - 4.0f * std::fabs(t - 0.5f);
                    case 4:
                        return std::sin(phase) * 0.3f;
                    case 5:
                        return (t < 0.3f) ? 1.0f : -1.0f;
                    case 6:
                        return std::sin(phase) * 0.8f;
                    case 7:
                        return std::sin(phase) + 0.5f * std::sin(phase * 2.0f);
                    case 8:
                        return std::sin(phase) + 0.5f * std::sin(phase * 3.0f);
                    case 9:
                        return std::sin(phase) + 0.7f * std::sin(phase * 5.0f);
                    default:
                        return std::sin(phase);
                }
            });

            QRectF paramArea = waveRect;
            paramArea.setTop(waveArea.bottom() + Theme::pxF(6.0f));
            const float colGap = Theme::pxF(8.0f);
            const float rowGap = Theme::pxF(6.0f);
            const float cellW = (paramArea.width() - colGap) / 2.0f;
            const float cellH = (paramArea.height() - rowGap) / 2.0f;

            auto drawParamCell = [&](const QRectF &cell, int paramType) {
                EditParam &param = m_editParams[paramType];
                param.rect = cell;
                const bool selected = (paramType == m_selectedEditParam);
                p.setBrush(selected ? Theme::accentAlt() : Theme::bg3());
                p.setPen(QPen(Theme::stroke(), 1.0));
                p.drawRoundedRect(cell, Theme::px(6), Theme::px(6));
                p.setPen(selected ? Theme::bg0() : Theme::text());
                p.setFont(Theme::baseFont(8, QFont::DemiBold));
                p.drawText(cell.adjusted(Theme::px(6), 0, -Theme::px(6), 0),
                           Qt::AlignLeft | Qt::AlignVCenter, param.label);
                p.setPen(selected ? Theme::bg0() : Theme::textMuted());
                p.drawText(cell.adjusted(Theme::px(6), 0, -Theme::px(6), 0),
                           Qt::AlignRight | Qt::AlignVCenter, formatValue(param.type));
            };

            drawParamCell(QRectF(paramArea.left(), paramArea.top(), cellW, cellH),
                          voicesParam);
            drawParamCell(QRectF(paramArea.left() + cellW + colGap, paramArea.top(), cellW, cellH),
                          detuneParam);
            drawParamCell(QRectF(paramArea.left(), paramArea.top() + cellH + rowGap, cellW, cellH),
                          gainParam);
            drawParamCell(QRectF(paramArea.left() + cellW + colGap,
                                 paramArea.top() + cellH + rowGap, cellW, cellH),
                          panParam);
        };

        drawOscPanel(osc1Rect, "OSC 1", sp.osc1Wave,
                     EditOsc1Wave, EditOsc1Voices, EditOsc1Detune, EditOsc1Gain, EditOsc1Pan);
        drawOscPanel(osc2Rect, "OSC 2", sp.osc2Wave,
                     EditOsc2Wave, EditOsc2Voices, EditOsc2Detune, EditOsc2Gain, EditOsc2Pan);

        QRectF adsrRect(modRow.left(), modRow.top(),
                        modRow.width() * 0.55f - gap * 0.5f, modRow.height());
        QRectF lfoRect(adsrRect.right() + gap, modRow.top(),
                       modRow.right() - adsrRect.right() - gap, modRow.height());

        drawPanel(adsrRect, "ADSR");
        QRectF adsrInner = adsrRect.adjusted(Theme::px(8), Theme::px(20), -Theme::px(8), -Theme::px(8));
        QRectF adsrWave = adsrInner;
        adsrWave.setHeight(adsrInner.height() * 0.6f);
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

        QRectF adsrParams = adsrInner;
        adsrParams.setTop(adsrWave.bottom() + Theme::pxF(6.0f));
        const float adsrGap = Theme::pxF(6.0f);
        const float adsrW = (adsrParams.width() - adsrGap) / 2.0f;
        const float adsrH = (adsrParams.height() - adsrGap) / 2.0f;
        auto drawSmallParam = [&](const QRectF &cell, int paramType) {
            EditParam &param = m_editParams[paramType];
            param.rect = cell;
            const bool selected = (paramType == m_selectedEditParam);
            p.setBrush(selected ? Theme::accentAlt() : Theme::bg3());
            p.setPen(QPen(Theme::stroke(), 1.0));
            p.drawRoundedRect(cell, Theme::px(6), Theme::px(6));
            p.setPen(selected ? Theme::bg0() : Theme::text());
            p.setFont(Theme::baseFont(8, QFont::DemiBold));
            p.drawText(cell.adjusted(Theme::px(6), 0, -Theme::px(6), 0),
                       Qt::AlignLeft | Qt::AlignVCenter, param.label);
            p.setPen(selected ? Theme::bg0() : Theme::textMuted());
            p.drawText(cell.adjusted(Theme::px(6), 0, -Theme::px(6), 0),
                       Qt::AlignRight | Qt::AlignVCenter, formatValue(param.type));
        };

        drawSmallParam(QRectF(adsrParams.left(), adsrParams.top(), adsrW, adsrH), EditAttack);
        drawSmallParam(QRectF(adsrParams.left() + adsrW + adsrGap, adsrParams.top(), adsrW, adsrH),
                       EditDecay);
        drawSmallParam(QRectF(adsrParams.left(), adsrParams.top() + adsrH + adsrGap, adsrW, adsrH),
                       EditSustain);
        drawSmallParam(QRectF(adsrParams.left() + adsrW + adsrGap,
                              adsrParams.top() + adsrH + adsrGap, adsrW, adsrH),
                       EditRelease);

        drawPanel(lfoRect, "LFO");
        QRectF lfoInner = lfoRect.adjusted(Theme::px(8), Theme::px(20), -Theme::px(8), -Theme::px(8));
        QRectF lfoWave = lfoInner;
        lfoWave.setHeight(lfoInner.height() * 0.6f);
        {
            const float rate = clamp01(sp.lfoRate);
            const float depth = clamp01(sp.lfoDepth);
            const float cycles = 1.0f + rate * 3.0f;
            drawWave(lfoWave, Theme::accentAlt(), [cycles, depth](float t) {
                const float phase = kTwoPi * t * cycles;
                return std::sin(phase) * (0.25f + depth * 0.6f);
            });
        }

        QRectF lfoParams = lfoInner;
        lfoParams.setTop(lfoWave.bottom() + Theme::pxF(6.0f));
        const float lfoGap = Theme::pxF(6.0f);
        const float lfoW = (lfoParams.width() - lfoGap) / 2.0f;
        const float lfoH = lfoParams.height();
        drawSmallParam(QRectF(lfoParams.left(), lfoParams.top(), lfoW, lfoH), EditLfoRate);
        drawSmallParam(QRectF(lfoParams.left() + lfoW + lfoGap, lfoParams.top(), lfoW, lfoH),
                       EditLfoDepth);

        drawPanel(filterRow, "FILTER");
        QRectF filterInner = filterRow.adjusted(Theme::px(8), Theme::px(20), -Theme::px(8), -Theme::px(8));
        QRectF filterVis = filterInner;
        filterVis.setHeight(filterInner.height() * 0.4f);

        // Simple filter visual.
        {
            QPainterPath curve;
            const float left = filterVis.left();
            const float right = filterVis.right();
            const float midY = filterVis.center().y();
            const float topY = filterVis.top();
            const float bottomY = filterVis.bottom();
            switch (sp.filterType) {
                case 0: // low
                    curve.moveTo(left, topY);
                    curve.lineTo(right, bottomY);
                    break;
                case 1: // high
                    curve.moveTo(left, bottomY);
                    curve.lineTo(right, topY);
                    break;
                case 2: // band
                    curve.moveTo(left, bottomY);
                    curve.lineTo(filterVis.center().x(), topY);
                    curve.lineTo(right, bottomY);
                    break;
                case 3: // notch
                    curve.moveTo(left, topY);
                    curve.lineTo(filterVis.center().x(), bottomY);
                    curve.lineTo(right, topY);
                    break;
                default:
                    curve.moveTo(left, midY);
                    curve.lineTo(right, midY);
                    break;
            }
            p.setPen(QPen(Theme::accent(), Theme::pxF(1.6f)));
            p.drawPath(curve);
        }

        QRectF presetArea = filterInner;
        presetArea.setTop(filterVis.bottom() + Theme::pxF(6.0f));
        presetArea.setHeight(filterInner.height() * 0.4f);

        const int presetCols = 5;
        const int presetRows = 2;
        const float presetGap = Theme::pxF(6.0f);
        const float cellW = (presetArea.width() - presetGap * (presetCols - 1)) / presetCols;
        const float cellH = (presetArea.height() - presetGap) / presetRows;

        m_filterPresetRects.clear();
        for (int row = 0; row < presetRows; ++row) {
            for (int col = 0; col < presetCols; ++col) {
                const int idx = row * presetCols + col;
                if (idx >= filterPresets.size()) {
                    break;
                }
                QRectF r(presetArea.left() + col * (cellW + presetGap),
                         presetArea.top() + row * (cellH + presetGap),
                         cellW, cellH);
                m_filterPresetRects.push_back(r);
                const bool selected = (sp.filterType == idx);
                p.setBrush(selected ? Theme::accentAlt() : Theme::bg3());
                p.setPen(QPen(Theme::stroke(), 1.0));
                p.drawRoundedRect(r, Theme::px(6), Theme::px(6));
                p.setPen(selected ? Theme::bg0() : Theme::text());
                p.setFont(Theme::baseFont(8, QFont::DemiBold));
                p.drawText(r, Qt::AlignCenter, filterPresets[idx]);
            }
        }

        QRectF filterParams = filterInner;
        filterParams.setTop(presetArea.bottom() + Theme::pxF(6.0f));
        filterParams.setHeight(filterInner.bottom() - filterParams.top());
        const float filterGap = Theme::pxF(8.0f);
        const float filterW = (filterParams.width() - filterGap) / 2.0f;
        drawSmallParam(QRectF(filterParams.left(), filterParams.top(), filterW, filterParams.height()),
                       EditCutoff);
        drawSmallParam(QRectF(filterParams.left() + filterW + filterGap, filterParams.top(),
                              filterW, filterParams.height()),
                       EditResonance);
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
        const int maxVisible =
            std::max(1, static_cast<int>(std::floor((presetRect.height() - Theme::pxF(4.0f)) / rowH)));
        const int maxScroll = std::max(0, m_fluidPresets.size() - maxVisible);
        m_presetScroll = qBound(0, m_presetScroll, maxScroll);

        float py = presetRect.top();
        int drawn = 0;
        for (int idx = m_presetScroll; idx < m_fluidPresets.size(); ++idx) {
            const QString &item = m_fluidPresets[idx];
            PresetRow row;
            row.header = false;
            row.label = item;
            row.presetId = item;
            row.rect = QRectF(presetRect.left(), py, presetRect.width(), rowH - Theme::pxF(4.0f));
            m_presetRows.push_back(row);
            py += rowH;
            drawn++;
            if (drawn >= maxVisible) {
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

        if (maxScroll > 0) {
            p.setPen(Theme::textMuted());
            p.setFont(Theme::baseFont(8, QFont::DemiBold));
            const QString marker = QString("%1/%2").arg(m_presetScroll + 1).arg(maxScroll + 1);
            p.drawText(QRectF(presetRect.left(), presetRect.bottom() - Theme::px(14),
                              presetRect.width(), Theme::px(12)),
                       Qt::AlignRight | Qt::AlignVCenter, marker);
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
        case EditOsc1Wave:
            return static_cast<float>(sp.osc1Wave);
        case EditOsc1Voices:
            return static_cast<float>(sp.osc1Voices);
        case EditOsc1Detune:
            return sp.osc1Detune;
        case EditOsc1Gain:
            return sp.osc1Gain;
        case EditOsc1Pan:
            return sp.osc1Pan;
        case EditOsc2Wave:
            return static_cast<float>(sp.osc2Wave);
        case EditOsc2Voices:
            return static_cast<float>(sp.osc2Voices);
        case EditOsc2Detune:
            return sp.osc2Detune;
        case EditOsc2Gain:
            return sp.osc2Gain;
        case EditOsc2Pan:
            return sp.osc2Pan;
        case EditCutoff:
            return sp.cutoff;
        case EditResonance:
            return sp.resonance;
        case EditFilterType:
            return static_cast<float>(sp.filterType);
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
    const QString type = synthTypeFromId(synthIdOrDefault(m_pads, m_activePad)).trimmed().toUpper();
    const bool isDx7 = (type == "DX7");
    if (isDx7 && !(param.type == EditAttack || param.type == EditDecay ||
                   param.type == EditSustain || param.type == EditRelease)) {
        return;
    }
    const int waveCount = PadBank::serumWaves().size();
    const int filterCount = 10;

    auto step = [delta](float base, float amount) {
        return base + amount * static_cast<float>(delta);
    };

    switch (param.type) {
        case EditOsc1Wave:
            sp.osc1Wave = (sp.osc1Wave + delta + waveCount) % qMax(1, waveCount);
            m_pads->setSynthOsc(pad, 0, sp.osc1Wave, sp.osc1Voices, sp.osc1Detune,
                                sp.osc1Gain, sp.osc1Pan);
            break;
        case EditOsc1Voices:
            sp.osc1Voices = qBound(1, sp.osc1Voices + delta, 8);
            m_pads->setSynthOsc(pad, 0, sp.osc1Wave, sp.osc1Voices, sp.osc1Detune,
                                sp.osc1Gain, sp.osc1Pan);
            break;
        case EditOsc1Detune:
            sp.osc1Detune = clamp01(step(sp.osc1Detune, 0.05f));
            m_pads->setSynthOsc(pad, 0, sp.osc1Wave, sp.osc1Voices, sp.osc1Detune,
                                sp.osc1Gain, sp.osc1Pan);
            break;
        case EditOsc1Gain:
            sp.osc1Gain = clamp01(step(sp.osc1Gain, 0.05f));
            m_pads->setSynthOsc(pad, 0, sp.osc1Wave, sp.osc1Voices, sp.osc1Detune,
                                sp.osc1Gain, sp.osc1Pan);
            break;
        case EditOsc1Pan:
            sp.osc1Pan = qBound(-1.0f, step(sp.osc1Pan, 0.1f), 1.0f);
            m_pads->setSynthOsc(pad, 0, sp.osc1Wave, sp.osc1Voices, sp.osc1Detune,
                                sp.osc1Gain, sp.osc1Pan);
            break;
        case EditOsc2Wave:
            sp.osc2Wave = (sp.osc2Wave + delta + waveCount) % qMax(1, waveCount);
            m_pads->setSynthOsc(pad, 1, sp.osc2Wave, sp.osc2Voices, sp.osc2Detune,
                                sp.osc2Gain, sp.osc2Pan);
            break;
        case EditOsc2Voices:
            sp.osc2Voices = qBound(1, sp.osc2Voices + delta, 8);
            m_pads->setSynthOsc(pad, 1, sp.osc2Wave, sp.osc2Voices, sp.osc2Detune,
                                sp.osc2Gain, sp.osc2Pan);
            break;
        case EditOsc2Detune:
            sp.osc2Detune = clamp01(step(sp.osc2Detune, 0.05f));
            m_pads->setSynthOsc(pad, 1, sp.osc2Wave, sp.osc2Voices, sp.osc2Detune,
                                sp.osc2Gain, sp.osc2Pan);
            break;
        case EditOsc2Gain:
            sp.osc2Gain = clamp01(step(sp.osc2Gain, 0.05f));
            m_pads->setSynthOsc(pad, 1, sp.osc2Wave, sp.osc2Voices, sp.osc2Detune,
                                sp.osc2Gain, sp.osc2Pan);
            break;
        case EditOsc2Pan:
            sp.osc2Pan = qBound(-1.0f, step(sp.osc2Pan, 0.1f), 1.0f);
            m_pads->setSynthOsc(pad, 1, sp.osc2Wave, sp.osc2Voices, sp.osc2Detune,
                                sp.osc2Gain, sp.osc2Pan);
            break;
        case EditCutoff:
            sp.cutoff = clamp01(step(sp.cutoff, 0.02f));
            m_pads->setSynthFilter(pad, sp.cutoff, sp.resonance);
            break;
        case EditResonance:
            sp.resonance = clamp01(step(sp.resonance, 0.02f));
            m_pads->setSynthFilter(pad, sp.cutoff, sp.resonance);
            break;
        case EditFilterType:
            sp.filterType = (sp.filterType + delta + filterCount) % filterCount;
            m_pads->setSynthFilterType(pad, sp.filterType);
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
        default:
            break;
    }
    update();
}
