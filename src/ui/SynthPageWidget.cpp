#include "SynthPageWidget.h"

#include <QKeyEvent>
#include <QDateTime>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <QHash>

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
    EditLfoDepth = 18,
    EditOctave = 19,
    EditCustom1 = 20,
    EditCustom2 = 21,
    EditCustom3 = 22,
    EditCustom4 = 23,
    EditFilterEnv = 24,
    EditLfoShape = 25,
    EditLfoSync = 26,
    EditLfoTarget = 27
};

constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kAdsrMaxSeconds = 2.37842f;

bool isSimpleType(const QString &type) {
    const QString upper = type.trimmed().toUpper();
    return upper == "SIMPLE" || upper == "VITALYA" || upper == "VITAL" || upper == "SERUM";
}

QString canonicalType(const QString &type) {
    QString upper = type.trimmed().toUpper();
    upper.remove(' ');
    upper.remove('_');
    upper.remove('-');
    return upper;
}

bool isCustomEngineType(const QString &type) {
    const QString t = canonicalType(type);
    return t == "CLUSTER" || t == "DIGITAL" || t == "DNA" || t == "DRWAVE" ||
           t == "DSYNTH" || t == "FM" || t == "PULSE" || t == "PHASE" ||
           t == "RING" || t == "STRING" || t == "SAW" || t == "VOLTAGE";
}

enum class CustomTarget {
    Osc1Wave,
    Osc2Wave,
    FmAmount,
    Ratio,
    Feedback,
    Osc1Detune,
    Osc2Detune,
    Osc2Gain,
    FilterEnv
};

struct CustomControl {
    int paramType = EditCustom1;
    CustomTarget target = CustomTarget::FmAmount;
    QString label;
};

QVector<CustomControl> customControlsForType(const QString &type) {
    const QString t = canonicalType(type);
    QVector<CustomControl> list;
    auto add = [&](int paramType, CustomTarget target, const QString &label) {
        CustomControl c;
        c.paramType = paramType;
        c.target = target;
        c.label = label;
        list << c;
    };

    if (t == "CLUSTER") {
        add(EditCustom1, CustomTarget::Osc1Detune, "SPREAD");
        add(EditCustom2, CustomTarget::Osc2Detune, "DETUNE");
        add(EditCustom3, CustomTarget::Osc1Wave, "WAVE");
        add(EditCustom4, CustomTarget::FmAmount, "MOTION");
        return list;
    }
    if (t == "DIGITAL") {
        add(EditCustom1, CustomTarget::Osc1Wave, "WAVE");
        add(EditCustom2, CustomTarget::FmAmount, "INDEX");
        add(EditCustom3, CustomTarget::Osc1Detune, "DETUNE");
        add(EditCustom4, CustomTarget::Feedback, "DRIVE");
        return list;
    }
    if (t == "DNA") {
        add(EditCustom1, CustomTarget::Osc1Wave, "GENE A");
        add(EditCustom2, CustomTarget::Osc2Wave, "GENE B");
        add(EditCustom3, CustomTarget::FmAmount, "MIX");
        add(EditCustom4, CustomTarget::Feedback, "MUTATE");
        return list;
    }
    if (t == "DRWAVE") {
        add(EditCustom1, CustomTarget::Osc1Wave, "WAVE");
        add(EditCustom2, CustomTarget::FmAmount, "BEND");
        add(EditCustom3, CustomTarget::Osc1Detune, "DETUNE");
        add(EditCustom4, CustomTarget::Feedback, "DRIVE");
        return list;
    }
    if (t == "DSYNTH") {
        add(EditCustom1, CustomTarget::Osc1Wave, "WAVE");
        add(EditCustom2, CustomTarget::Ratio, "PITCH");
        add(EditCustom3, CustomTarget::FmAmount, "X-MOD");
        add(EditCustom4, CustomTarget::Osc2Gain, "MIX");
        return list;
    }
    if (t == "FM") {
        add(EditCustom1, CustomTarget::Osc1Detune, "C RATIO");
        add(EditCustom2, CustomTarget::Ratio, "M RATIO");
        add(EditCustom3, CustomTarget::FmAmount, "INDEX");
        add(EditCustom4, CustomTarget::Feedback, "FEEDBK");
        return list;
    }
    if (t == "PULSE") {
        add(EditCustom1, CustomTarget::FmAmount, "WIDTH");
        add(EditCustom2, CustomTarget::Osc1Detune, "DETUNE");
        add(EditCustom3, CustomTarget::Osc2Gain, "SUB");
        add(EditCustom4, CustomTarget::Feedback, "PWM");
        return list;
    }
    if (t == "PHASE") {
        add(EditCustom1, CustomTarget::FmAmount, "OFFSET");
        add(EditCustom2, CustomTarget::Osc1Detune, "SPREAD");
        add(EditCustom3, CustomTarget::Ratio, "AMOUNT");
        add(EditCustom4, CustomTarget::Feedback, "FEEDBK");
        return list;
    }
    if (t == "RING") {
        add(EditCustom1, CustomTarget::FmAmount, "BALANCE");
        add(EditCustom2, CustomTarget::Ratio, "RATIO");
        add(EditCustom3, CustomTarget::Osc1Detune, "DETUNE");
        add(EditCustom4, CustomTarget::Feedback, "DRIVE");
        return list;
    }
    if (t == "STRING") {
        add(EditCustom1, CustomTarget::Ratio, "TENSION");
        add(EditCustom2, CustomTarget::FmAmount, "DAMP");
        add(EditCustom3, CustomTarget::Osc2Gain, "NOISE");
        add(EditCustom4, CustomTarget::Feedback, "BODY");
        return list;
    }
    if (t == "SAW") {
        add(EditCustom1, CustomTarget::Osc1Detune, "DETUNE");
        add(EditCustom2, CustomTarget::Osc2Detune, "SPREAD");
        add(EditCustom3, CustomTarget::Osc2Gain, "SUB");
        add(EditCustom4, CustomTarget::Feedback, "NOISE");
        return list;
    }
    if (t == "VOLTAGE") {
        add(EditCustom1, CustomTarget::FmAmount, "MIX");
        add(EditCustom2, CustomTarget::Osc1Detune, "DETUNE");
        add(EditCustom3, CustomTarget::FilterEnv, "SHAPE");
        add(EditCustom4, CustomTarget::Feedback, "DRIVE");
        return list;
    }
    return list;
}

QVector<int> visibleParamIndicesForType(const QString &type) {
    const QString upper = type.trimmed().toUpper();
    QVector<int> indices;
    if (upper == "DX7") {
        indices << EditAttack << EditDecay << EditSustain << EditRelease;
        return indices;
    }
    if (isCustomEngineType(upper)) {
        const QVector<CustomControl> controls = customControlsForType(upper);
        for (const auto &c : controls) {
            indices << c.paramType;
        }
        return indices;
    }
    if (isSimpleType(upper)) {
        indices << EditOsc1Wave << EditOctave << EditOsc1Voices << EditOsc1Detune << EditOsc1Gain
                << EditOsc2Wave << EditOsc2Voices << EditOsc2Detune << EditOsc2Gain;
        return indices;
    }
    indices << EditOsc1Wave << EditOsc1Voices << EditOsc1Detune << EditOsc1Gain << EditOsc1Pan;
    indices << EditOsc2Wave << EditOsc2Voices << EditOsc2Detune << EditOsc2Gain << EditOsc2Pan;
    indices << EditCutoff << EditResonance << EditFilterType;
    indices << EditAttack << EditDecay << EditSustain << EditRelease;
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
    if (isCustomEngineType(type)) {
        return type;
    }
    if (isSimpleType(type)) {
        return QStringLiteral("SIMPLE");
    }
    for (const QString &bank : banks) {
        const QString upper = bank.trimmed().toUpper();
        if (!isSimpleType(upper)) {
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
    if (isSimpleType(type) || isCustomEngineType(type)) {
        return type;
    }
    const QString preset = synthPreset(id);
    const int slash = preset.indexOf('/');
    if (slash >= 0) {
        return preset.left(slash).trimmed();
    }
    const QStringList banks = PadBank::synthBanks();
    for (const QString &bank : banks) {
        const QString upper = bank.trimmed().toUpper();
        if (!isSimpleType(upper)) {
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
        const QString type = id.left(colon).trimmed().toUpper();
        return isSimpleType(type) ? QStringLiteral("SIMPLE") : type;
    }
    return defaultSynthType();
}

bool isFmBank(const QString &bank) {
    const QString upper = bank.trimmed().toUpper();
    return isSimpleType(upper) || isCustomEngineType(upper);
}

QString classifyPresetType(const QString &name) {
    const QString upper = name.toUpper();
    auto hasAny = [&](const QStringList &keys) {
        for (const QString &key : keys) {
            if (upper.contains(key)) {
                return true;
            }
        }
        return false;
    };

    if (hasAny({"BASS", "SUB", "808", "LOW", "REESE", "ACID"})) {
        return "BASS";
    }
    if (hasAny({"LEAD", "SOLO", "SAW", "SYNC", "RAVE"})) {
        return "LEAD";
    }
    if (hasAny({"PAD", "ATM", "AMBI", "WARM", "WIDE"})) {
        return "PAD";
    }
    if (hasAny({"PLUCK", "PICK", "HARP", "ZITHER"})) {
        return "PLUCK";
    }
    if (hasAny({"KEY", "PIANO", "EP", "EPIANO", "CLAV", "MALLET"})) {
        return "KEYS";
    }
    if (hasAny({"ARP", "ARPEG", "SEQ", "SEQUENCE"})) {
        return "ARP";
    }
    if (hasAny({"FX", "SFX", "NOISE", "SWEEP", "RISE", "FALL", "HIT", "IMPACT", "WHOOSH"})) {
        return "FX";
    }
    if (hasAny({"DRUM", "KICK", "SNARE", "HAT", "CLAP", "TOM", "PERC"})) {
        return "DRUM";
    }
    if (hasAny({"VOC", "VOICE", "VOX", "CHOIR"})) {
        return "VOCAL";
    }
    if (hasAny({"BRASS", "TRUMP", "TROMB", "HORN"})) {
        return "BRASS";
    }
    if (hasAny({"STRING", "VIOL", "CELLO"})) {
        return "STRINGS";
    }
    if (hasAny({"BELL", "CHIME", "GLASS"})) {
        return "BELL";
    }
    if (hasAny({"ORGAN", "HAMMOND", "B3"})) {
        return "ORGAN";
    }
    if (hasAny({"GTR", "GUITAR"})) {
        return "GUITAR";
    }
    return "OTHER";
}

QString modTargetLabel(int targetIndex) {
    switch (targetIndex) {
        case 1:
            return "OSC1 DETUNE";
        case 2:
            return "OSC1 VOL";
        case 3:
            return "OSC1 PAN";
        case 4:
            return "OSC2 DETUNE";
        case 5:
            return "OSC2 VOL";
        case 6:
            return "OSC2 PAN";
        case 7:
            return "CUTOFF";
        case 8:
            return "RESO";
        case 9:
            return "F ENV";
        case 10:
            return "FM";
        case 11:
            return "RATIO";
        case 12:
            return "FEEDBACK";
        case 13:
            return "C1";
        case 14:
            return "C2";
        case 15:
            return "C3";
        case 16:
            return "C4";
        default:
            return "TARGET";
    }
}

QStringList filterPresetNames() {
    return {"DUAL CUT", "LOW CUT", "HIGH CUT", "BAND", "NOTCH", "PEAK",
            "LOW+MID", "AIR"};
}

void applyFilterPreset(PadBank::SynthParams::FilterModule &module) {
    switch (module.preset) {
        case 0: // dual cut
            module.type = 4;
            if (module.highCut <= module.lowCut) {
                module.lowCut = 0.15f;
                module.highCut = 0.85f;
            }
            break;
        case 1: // low cut
            module.type = 1;
            module.lowCut = std::max(module.lowCut, 0.15f);
            module.highCut = 1.0f;
            break;
        case 2: // high cut
            module.type = 0;
            module.lowCut = 0.0f;
            module.highCut = std::min(module.highCut, 0.75f);
            break;
        case 3: // band
            module.type = 2;
            module.lowCut = 0.25f;
            module.highCut = 0.75f;
            break;
        case 4: // notch
            module.type = 3;
            module.lowCut = 0.35f;
            module.highCut = 0.65f;
            break;
        case 5: // peak
            module.type = 7;
            module.lowCut = 0.4f;
            module.highCut = 0.6f;
            break;
        case 6: // low+mid
            module.type = 10;
            module.lowCut = 0.0f;
            module.highCut = 0.7f;
            break;
        case 7: // air
            module.type = 6;
            module.lowCut = 0.0f;
            module.highCut = 1.0f;
            break;
        default:
            break;
    }
    module.lowCut = clamp01(module.lowCut);
    module.highCut = clamp01(std::max(module.lowCut, module.highCut));
    module.mix = clamp01(module.mix);
    module.resonance = clamp01(module.resonance);
    module.slope = clamp01(module.slope);
    module.drive = clamp01(module.drive);
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
        {"OCT", EditOctave},
        {"C1", EditCustom1},
        {"C2", EditCustom2},
        {"C3", EditCustom3},
        {"C4", EditCustom4},
        {"F ENV", EditFilterEnv},
        {"LFO SHAPE", EditLfoShape},
        {"LFO SYNC", EditLfoSync},
        {"LFO TGT", EditLfoTarget},
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
    m_allPresets.clear();
    m_categories.clear();

    const QString id = synthIdOrDefault(m_pads, m_activePad);
    const QString type = synthTypeFromId(id).trimmed().toUpper();
    QStringList banks;
    if (m_pads) {
        banks = PadBank::synthBanks();
    }
    if (isCustomEngineType(type)) {
        banks = {type};
    } else if (isSimpleType(type)) {
        banks = {QStringLiteral("SIMPLE")};
    } else {
        QStringList filtered;
        for (const QString &bank : banks) {
            const QString upper = bank.trimmed().toUpper();
            if (!isSimpleType(upper)) {
                filtered << bank;
            }
        }
        if (!filtered.isEmpty()) {
            banks = filtered;
        }
    }
    if (banks.isEmpty()) {
        banks << (isSimpleType(type)
                      ? "SIMPLE"
                      : "INTERNAL");
    }

    for (const QString &bank : banks) {
        const QStringList presets = m_pads ? PadBank::synthPresetsForBank(bank) : QStringList();
        for (const QString &preset : presets) {
            PresetEntry entry;
            entry.preset = preset;
            entry.bank = bank;
            entry.category = classifyPresetType(preset);
            m_allPresets.push_back(entry);
        }
    }
    if (m_allPresets.isEmpty()) {
        PresetEntry entry;
        entry.preset = "INIT";
        entry.bank = isSimpleType(type)
                         ? "SIMPLE"
                         : "INTERNAL";
        entry.category = "OTHER";
        m_allPresets.push_back(entry);
    }

    QHash<QString, int> nameCounts;
    for (const auto &entry : m_allPresets) {
        nameCounts[entry.preset.toUpper()] += 1;
    }
    for (auto &entry : m_allPresets) {
        const int count = nameCounts.value(entry.preset.toUpper(), 0);
        entry.label = (count > 1) ? QString("%1  [%2]").arg(entry.preset, entry.bank)
                                  : entry.preset;
    }

    const QStringList order = {"BASS", "LEAD", "PAD",    "PLUCK",  "KEYS", "ARP",
                               "FX",   "DRUM", "VOCAL",  "BRASS",  "STRINGS",
                               "BELL", "ORGAN", "GUITAR", "OTHER"};
    for (const QString &cat : order) {
        bool has = false;
        for (const auto &entry : m_allPresets) {
            if (entry.category == cat) {
                has = true;
                break;
            }
        }
        if (has) {
            m_categories << cat;
        }
    }
    if (m_categories.isEmpty()) {
        m_categories << "OTHER";
    }

    if (syncSelection) {
        const QString bank = synthBank(id);
        const QString program = synthProgram(id);
        QString cat;
        for (const auto &entry : m_allPresets) {
            if (QString::compare(entry.bank, bank, Qt::CaseInsensitive) == 0 &&
                QString::compare(entry.preset, program, Qt::CaseInsensitive) == 0) {
                cat = entry.category;
                break;
            }
        }
        if (cat.isEmpty()) {
            cat = classifyPresetType(program);
        }
        int idx = m_categories.indexOf(cat);
        if (idx < 0) {
            idx = 0;
        }
        m_selectedCategory = idx;
        m_presetScroll = 0;
    }
    if (m_selectedCategory < 0 || m_selectedCategory >= m_categories.size()) {
        m_selectedCategory = 0;
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
    const QString type = synthTypeFromId(synthIdOrDefault(m_pads, m_activePad));
    const QString typeUpper = type.trimmed().toUpper();
    const bool presetsAllowed = (typeUpper == "DX7" || isCustomEngineType(typeUpper));
    auto closeBind = [&]() {
        m_bindSource = BindSource::None;
        m_bindSlot = -1;
        m_bindReturnMode = EditorMode::None;
    };
    auto toggleEditor = [&](EditorMode mode) {
        if (m_editorMode == mode) {
            m_editorMode = EditorMode::None;
        } else {
            m_editorMode = mode;
        }
        m_showPresetMenu = false;
        closeBind();
        update();
    };

    if (key == Qt::Key_L) {
        toggleEditor(EditorMode::Lfo);
        return;
    }
    if (key == Qt::Key_A) {
        toggleEditor(EditorMode::Env);
        return;
    }
    if (key == Qt::Key_F) {
        toggleEditor(EditorMode::Filter);
        return;
    }
    if (key == Qt::Key_P) {
        if (presetsAllowed) {
            m_editorMode = EditorMode::None;
            closeBind();
            m_showPresetMenu = !m_showPresetMenu;
            update();
        }
        return;
    }
    if (key == Qt::Key_Escape) {
        if (m_editorMode != EditorMode::None) {
            m_editorMode = EditorMode::None;
            update();
            return;
        }
        if (m_bindSource != BindSource::None) {
            closeBind();
            update();
            return;
        }
    }
    if (key == Qt::Key_Escape && m_showPresetMenu) {
        m_showPresetMenu = false;
        update();
        return;
    }
    if (m_editorMode != EditorMode::None) {
        int *scroll = nullptr;
        if (m_editorMode == EditorMode::Lfo) {
            scroll = &m_lfoScroll;
        } else if (m_editorMode == EditorMode::Env) {
            scroll = &m_envScroll;
        } else if (m_editorMode == EditorMode::Filter) {
            scroll = &m_filterScroll;
        }
        if (scroll) {
            if (key == Qt::Key_Left) {
                *scroll = std::max(0, *scroll - 1);
                update();
                return;
            }
            if (key == Qt::Key_Right) {
                *scroll += 1;
                update();
                return;
            }
        }
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
    const QString type = synthTypeFromId(synthIdOrDefault(m_pads, m_activePad));
    const QString typeUpper = type.trimmed().toUpper();
    const bool presetsAllowed = (typeUpper == "DX7" || isCustomEngineType(typeUpper));

    auto deltaFromClick = [&](const QRectF &r) {
        return pos.x() < r.center().x() ? -1 : 1;
    };

    if (m_editorMode != EditorMode::None) {
        if (!m_editorContentRect.contains(pos)) {
            m_editorMode = EditorMode::None;
            update();
            return;
        }
        if (m_editorLeftRect.contains(pos)) {
            if (m_editorMode == EditorMode::Lfo) {
                m_lfoScroll = std::max(0, m_lfoScroll - 1);
            } else if (m_editorMode == EditorMode::Env) {
                m_envScroll = std::max(0, m_envScroll - 1);
            } else if (m_editorMode == EditorMode::Filter) {
                m_filterScroll = std::max(0, m_filterScroll - 1);
            }
            update();
            return;
        }
        if (m_editorRightRect.contains(pos)) {
            if (m_editorMode == EditorMode::Lfo) {
                ++m_lfoScroll;
            } else if (m_editorMode == EditorMode::Env) {
                ++m_envScroll;
            } else if (m_editorMode == EditorMode::Filter) {
                ++m_filterScroll;
            }
            update();
            return;
        }
        if (m_editorAddRect.contains(pos) && m_pads) {
            PadBank::SynthParams sp = m_pads->synthParams(m_activePad);
            if (m_editorMode == EditorMode::Lfo) {
                for (int i = 0; i < PadBank::kLfoModuleCount; ++i) {
                    auto module = sp.lfoModules[static_cast<size_t>(i)];
                    if (!module.enabled) {
                        module.enabled = true;
                        module.kind = 0;
                        module.shape = 0;
                        module.morph = 0.0f;
                        module.rate = 0.2f;
                        module.depth = 0.5f;
                        module.sync = 1;
                        module.syncIndex = 3;
                        module.steps = 16;
                        module.pattern.fill(0.5f);
                        m_pads->setSynthLfoModule(m_activePad, i, module);
                        m_activeLfoSlot = i;
                        break;
                    }
                }
            } else if (m_editorMode == EditorMode::Env) {
                for (int i = 0; i < PadBank::kEnvModuleCount; ++i) {
                    auto module = sp.envModules[static_cast<size_t>(i)];
                    if (!module.enabled) {
                        module.enabled = true;
                        module.attack = 0.0f;
                        module.decay = 0.2f;
                        module.sustain = 1.0f;
                        module.release = 0.2f;
                        m_pads->setSynthEnvModule(m_activePad, i, module);
                        m_activeEnvSlot = i;
                        break;
                    }
                }
            } else if (m_editorMode == EditorMode::Filter) {
                for (int i = 0; i < PadBank::kFilterModuleCount; ++i) {
                    auto module = sp.filterModules[static_cast<size_t>(i)];
                    if (!module.enabled) {
                        module.enabled = true;
                        module.preset = 0;
                        module.type = 4;
                        module.lowCut = 0.0f;
                        module.highCut = 1.0f;
                        module.resonance = 0.0f;
                        module.slope = 0.0f;
                        module.drive = 0.0f;
                        module.mix = 1.0f;
                        m_pads->setSynthFilterModule(m_activePad, i, module);
                        m_activeFilterSlot = i;
                        break;
                    }
                }
            }
            update();
            return;
        }
        for (int hitIndex = m_editorHits.size() - 1; hitIndex >= 0; --hitIndex) {
            const EditorHit &hit = m_editorHits[hitIndex];
            if (!hit.rect.contains(pos) || !m_pads) {
                continue;
            }
            PadBank::SynthParams sp = m_pads->synthParams(m_activePad);
            if (hit.id == "lfo-select") {
                m_activeLfoSlot = hit.slot;
                update();
                return;
            }
            if (hit.id == "env-select") {
                m_activeEnvSlot = hit.slot;
                update();
                return;
            }
            if (hit.id == "filter-select") {
                m_activeFilterSlot = hit.slot;
                update();
                return;
            }
            if (hit.id == "lfo-remove") {
                auto module = sp.lfoModules[static_cast<size_t>(hit.slot)];
                module = {};
                m_pads->setSynthLfoModule(m_activePad, hit.slot, module);
                m_activeLfoSlot = std::max(0, std::min(m_activeLfoSlot, PadBank::kLfoModuleCount - 1));
                update();
                return;
            }
            if (hit.id == "env-remove") {
                auto module = sp.envModules[static_cast<size_t>(hit.slot)];
                module = {};
                m_pads->setSynthEnvModule(m_activePad, hit.slot, module);
                update();
                return;
            }
            if (hit.id == "filter-remove") {
                auto module = sp.filterModules[static_cast<size_t>(hit.slot)];
                module = {};
                m_pads->setSynthFilterModule(m_activePad, hit.slot, module);
                update();
                return;
            }
            if (hit.id == "lfo-bind") {
                m_bindSource = BindSource::Lfo;
                m_bindSlot = hit.slot;
                m_bindReturnMode = m_editorMode;
                m_editorMode = EditorMode::None;
                update();
                return;
            }
            if (hit.id == "env-bind") {
                m_bindSource = BindSource::Env;
                m_bindSlot = hit.slot;
                m_bindReturnMode = m_editorMode;
                m_editorMode = EditorMode::None;
                update();
                return;
            }
            if (hit.id.startsWith("lfo-")) {
                auto module = sp.lfoModules[static_cast<size_t>(hit.slot)];
                const int delta = deltaFromClick(hit.rect);
                if (hit.id == "lfo-kind") {
                    module.kind = hit.value;
                    if (module.kind == 1 && module.steps <= 0) {
                        module.steps = 16;
                    }
                } else if (hit.id == "lfo-shape") {
                    module.morph = qBound(0.0f, module.morph + delta * 0.1f, 1.0f);
                } else if (hit.id == "lfo-rate") {
                    module.rate = qBound(0.0f, module.rate + delta * 0.05f, 1.0f);
                } else if (hit.id == "lfo-depth") {
                    module.depth = qBound(0.0f, module.depth + delta * 0.05f, 1.0f);
                } else if (hit.id == "lfo-sync") {
                    module.sync = module.sync ? 0 : 1;
                } else if (hit.id == "lfo-steps") {
                    static const int stepOptions[] = {4, 8, 12, 16, 24, 32};
                    int current = 3;
                    for (int i = 0; i < 6; ++i) {
                        if (module.steps == stepOptions[i]) {
                            current = i;
                            break;
                        }
                    }
                    current = qBound(0, current + delta, 5);
                    module.steps = stepOptions[current];
                } else if (hit.id == "lfo-step") {
                    const int step = qBound(0, hit.value, PadBank::kLfoPatternSteps - 1);
                    module.pattern[static_cast<size_t>(step)] =
                        module.pattern[static_cast<size_t>(step)] > 0.5f ? 0.15f : 0.85f;
                }
                m_pads->setSynthLfoModule(m_activePad, hit.slot, module);
                update();
                return;
            }
            if (hit.id.startsWith("env-")) {
                auto module = sp.envModules[static_cast<size_t>(hit.slot)];
                const int delta = deltaFromClick(hit.rect);
                auto stepParam = [&](float &value, float amount) {
                    value = qBound(0.0f, value + delta * amount, 1.0f);
                };
                if (hit.id == "env-attack") {
                    stepParam(module.attack, 0.04f);
                } else if (hit.id == "env-decay") {
                    stepParam(module.decay, 0.04f);
                } else if (hit.id == "env-sustain") {
                    stepParam(module.sustain, 0.04f);
                } else if (hit.id == "env-release") {
                    stepParam(module.release, 0.04f);
                }
                module.enabled = true;
                m_pads->setSynthEnvModule(m_activePad, hit.slot, module);
                update();
                return;
            }
            if (hit.id.startsWith("filter-")) {
                auto module = sp.filterModules[static_cast<size_t>(hit.slot)];
                const int delta = deltaFromClick(hit.rect);
                auto stepParam = [&](float &value, float amount) {
                    value = qBound(0.0f, value + delta * amount, 1.0f);
                };
                if (hit.id == "filter-preset") {
                    const int count = filterPresetNames().size();
                    module.preset = (module.preset + delta + count) % count;
                    applyFilterPreset(module);
                } else if (hit.id == "filter-low") {
                    stepParam(module.lowCut, 0.04f);
                    module.lowCut = std::min(module.lowCut, module.highCut);
                } else if (hit.id == "filter-high") {
                    stepParam(module.highCut, 0.04f);
                    module.highCut = std::max(module.highCut, module.lowCut);
                } else if (hit.id == "filter-reso") {
                    stepParam(module.resonance, 0.05f);
                } else if (hit.id == "filter-slope") {
                    stepParam(module.slope, 0.05f);
                } else if (hit.id == "filter-drive") {
                    stepParam(module.drive, 0.05f);
                } else if (hit.id == "filter-mix") {
                    stepParam(module.mix, 0.05f);
                }
                module.enabled = true;
                m_pads->setSynthFilterModule(m_activePad, hit.slot, module);
                update();
                return;
            }
        }
        return;
    }

    if (m_bindSource != BindSource::None) {
        for (int i = 0; i < m_editParams.size(); ++i) {
            if (!m_editParams[i].rect.contains(pos) || !m_pads) {
                continue;
            }
            const int targetIndex = modTargetForParam(i, typeUpper);
            if (targetIndex <= 0 || targetIndex >= PadBank::kModTargetCount) {
                continue;
            }
            PadBank::SynthParams sp = m_pads->synthParams(m_activePad);
            if (m_bindSource == BindSource::Lfo && m_bindSlot >= 0 &&
                m_bindSlot < PadBank::kLfoModuleCount) {
                auto module = sp.lfoModules[static_cast<size_t>(m_bindSlot)];
                module.enabled = true;
                module.assign[static_cast<size_t>(targetIndex)] = 0.65f;
                m_pads->setSynthLfoModule(m_activePad, m_bindSlot, module);
            } else if (m_bindSource == BindSource::Env && m_bindSlot >= 0 &&
                       m_bindSlot < PadBank::kEnvModuleCount) {
                auto module = sp.envModules[static_cast<size_t>(m_bindSlot)];
                module.enabled = true;
                module.assign[static_cast<size_t>(targetIndex)] = 1.0f;
                m_pads->setSynthEnvModule(m_activePad, m_bindSlot, module);
            }
            m_editorMode = m_bindReturnMode;
            m_bindSource = BindSource::None;
            m_bindSlot = -1;
            m_bindReturnMode = EditorMode::None;
            update();
            return;
        }
        return;
    }

    if (m_assignMenuOpen) {
        if (!m_assignRect.contains(pos)) {
            m_assignMenuOpen = false;
            update();
            return;
        }
        if (!m_pads || m_assignParamType < 0) {
            m_assignMenuOpen = false;
            update();
            return;
        }
        const int targetIndex = modTargetForParam(m_assignParamType, typeUpper);
        if (targetIndex <= 0 || targetIndex >= PadBank::kModTargetCount) {
            m_assignMenuOpen = false;
            update();
            return;
        }
        PadBank::SynthParams sp = m_pads->synthParams(m_activePad);
        float lfoAmt = sp.lfoAssign[static_cast<size_t>(targetIndex)];
        float envAmt = sp.envAssign[static_cast<size_t>(targetIndex)];
        const float defaultAmt = 0.6f;
        if (m_assignLfoRect.contains(pos)) {
            lfoAmt = (lfoAmt > 0.001f) ? 0.0f : defaultAmt;
        } else if (m_assignEnvRect.contains(pos)) {
            envAmt = (envAmt > 0.001f) ? 0.0f : defaultAmt;
        } else {
            m_assignMenuOpen = false;
            update();
            return;
        }
        m_pads->setSynthModAssign(m_activePad, static_cast<PadBank::ModTarget>(targetIndex),
                                  lfoAmt, envAmt);
        m_assignMenuOpen = false;
        update();
        return;
    }

    if (m_busRect.contains(pos) && m_pads) {
        const int nextBus = (m_pads->fxBus(m_activePad) + 1) % 6;
        m_pads->setFxBus(m_activePad, nextBus);
        update();
        return;
    }

    if (m_presetButtonRect.contains(pos)) {
        if (presetsAllowed) {
            m_showPresetMenu = !m_showPresetMenu;
            update();
        }
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
                const QString bank = row.bank;
                const QString bankUpper = bank.trimmed().toUpper();
                QString type = QStringLiteral("DX7");
                if (isCustomEngineType(bankUpper)) {
                    type = bankUpper;
                } else if (isFmBank(bank)) {
                    type = QStringLiteral("SIMPLE");
                }
                const QString presetId = row.presetId;
                QString payload = presetId;
                if (type == "DX7" && !bank.isEmpty()) {
                    payload = QString("%1/%2").arg(bank, presetId);
                }
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
    if (!m_holdActive) {
        return;
    }
    const QPointF pos = event->position();
    const float dx = pos.x() - m_holdPos.x();
    const float dy = pos.y() - m_holdPos.y();
    const float dist2 = dx * dx + dy * dy;
    if (dist2 > Theme::pxF(16.0f) * Theme::pxF(16.0f)) {
        m_holdTimer.stop();
        m_holdActive = false;
    }
}

void SynthPageWidget::mouseReleaseEvent(QMouseEvent *) {
    if (m_holdActive) {
        m_holdTimer.stop();
        m_holdActive = false;
    }
}

void SynthPageWidget::wheelEvent(QWheelEvent *event) {
    if (m_editorMode != EditorMode::None) {
        const int delta = event->angleDelta().y();
        int *scroll = nullptr;
        if (m_editorMode == EditorMode::Lfo) {
            scroll = &m_lfoScroll;
        } else if (m_editorMode == EditorMode::Env) {
            scroll = &m_envScroll;
        } else if (m_editorMode == EditorMode::Filter) {
            scroll = &m_filterScroll;
        }
        if (scroll) {
            if (delta < 0) {
                ++(*scroll);
            } else if (delta > 0) {
                *scroll = std::max(0, *scroll - 1);
            }
            update();
            return;
        }
    }
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
    m_editorHits.clear();
    m_editorContentRect = QRectF();
    m_editorLeftRect = QRectF();
    m_editorRightRect = QRectF();
    m_editorAddRect = QRectF();

    const int margin = Theme::px(18);
    const QRectF panel = rect().adjusted(margin, margin, -margin, -margin);
    p.setBrush(Theme::bg1());
    p.setPen(QPen(Theme::stroke(), 1.2));
    p.drawRoundedRect(panel, Theme::px(14), Theme::px(14));

    const QRectF header(panel.left() + Theme::px(12), panel.top() + Theme::px(8),
                        panel.width() - Theme::px(24), Theme::px(30));

    const QString id = synthIdOrDefault(m_pads, m_activePad);
    const QString synthType = synthTypeFromId(id);
    const bool presetsAllowed = (synthType.trimmed().toUpper() == "DX7");

    const float buttonW = Theme::pxF(96.0f);
    const float buttonH = header.height() - Theme::pxF(4.0f);
    m_presetButtonRect = QRectF(header.left(), header.top() + Theme::pxF(2.0f),
                                buttonW, buttonH);
    if (!presetsAllowed) {
        m_showPresetMenu = false;
    }
    p.setBrush(m_showPresetMenu ? Theme::accentAlt() : Theme::bg2());
    p.setPen(QPen(Theme::stroke(), 1.0));
    p.drawRoundedRect(m_presetButtonRect, Theme::px(8), Theme::px(8));
    p.setPen(m_showPresetMenu ? Theme::bg0() : Theme::textMuted());
    p.setFont(Theme::condensedFont(10, QFont::Bold));
    p.drawText(m_presetButtonRect, Qt::AlignCenter, presetsAllowed ? "PRESETS" : "INIT");

    const QString bankName = synthBank(id);
    const QString programName = synthProgram(id);

    const float busW = Theme::pxF(90.0f);
    m_busRect = QRectF(header.right() - busW, header.top() + Theme::pxF(2.0f),
                       busW, buttonH);
    QRectF padInfoRect = header;
    padInfoRect.setRight(m_busRect.left() - Theme::pxF(8.0f));

    QRectF presetNameRect = header;
    presetNameRect.setLeft(m_presetButtonRect.right() + Theme::pxF(10.0f));
    presetNameRect.setRight(padInfoRect.left() - Theme::pxF(8.0f));
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

    const int bus = m_pads ? m_pads->fxBus(m_activePad) : 0;
    p.setBrush(Theme::bg2());
    p.setPen(QPen(Theme::stroke(), 1.0));
    p.drawRoundedRect(m_busRect, Theme::px(6), Theme::px(6));
    p.setPen(Theme::accentAlt());
    p.setFont(Theme::baseFont(9, QFont::DemiBold));
    p.drawText(m_busRect, Qt::AlignCenter, QString("BUS %1").arg(PadBank::fxBusLabel(bus)));

    p.setPen(Theme::textMuted());
    p.setFont(Theme::baseFont(9, QFont::DemiBold));
    p.drawText(padInfoRect, Qt::AlignRight | Qt::AlignVCenter,
               QString("PAD %1  %2").arg(m_activePad + 1).arg(synthType));

    const QString synthTypeUpper = synthType.trimmed().toUpper();
    const bool isDx7 = (synthTypeUpper == "DX7");
    const bool isSimple = isSimpleType(synthTypeUpper);

    const float gap = Theme::pxF(12.0f);
    QRectF content(panel.left() + Theme::px(12), header.bottom() + Theme::px(10),
                   panel.width() - Theme::px(24),
                   panel.bottom() - header.bottom() - Theme::px(16));
    if (m_modMenuOpen) {
        const float menuW = content.width() * 0.32f;
        m_modMenuRect = QRectF(content.left(), content.top(), menuW, content.height());
        content.setLeft(m_modMenuRect.right() + gap);
    } else {
        m_modMenuRect = QRectF();
    }

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

    auto drawKnobMarker = [&](const QRectF &cell, const QColor &color) {
        const float size = Theme::pxF(10.0f);
        QRectF dot(cell.left() + Theme::pxF(8.0f),
                   cell.top() + Theme::pxF(8.0f),
                   size, size);
        p.setBrush(color);
        p.setPen(QPen(Theme::stroke(), 1.0));
        p.drawEllipse(dot);
        p.setPen(QPen(Theme::text(), Theme::pxF(1.2f)));
        p.drawLine(QPointF(dot.center().x(), dot.center().y()),
                   QPointF(dot.center().x(), dot.top() - Theme::pxF(3.0f)));
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
    const QStringList filterPresets = {"LOW", "HIGH", "BAND", "NOTCH", "PEAK",
                                       "LOW SHELF", "HIGH SHELF", "ALLPASS", "BYPASS", "LOW+MID"};
    const QStringList lfoShapes = {"SINE", "TRI", "SQUARE", "SAW", "RAND"};
    const QStringList lfoTargets = {"FILTER", "AMP"};
    const QStringList lfoSyncLabels = {"1/1", "1/2", "1/4", "1/8",
                                       "1/16", "1/32", "1/4T", "1/8T"};
    const QColor knobColors[4] = {QColor(74, 163, 255),
                                  QColor(99, 210, 96),
                                  QColor(236, 236, 236),
                                  QColor(255, 154, 60)};

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
            case EditOctave:
                return QString("%1").arg(sp.octave >= 0 ? QString("+%1").arg(sp.octave) : QString::number(sp.octave));
            case EditCutoff:
                return QString("%1%").arg(qRound(clamp01(sp.cutoff) * 100.0f));
            case EditResonance:
                return QString("%1%").arg(qRound(clamp01(sp.resonance) * 100.0f));
            case EditFilterType:
                return filterPresets.value(sp.filterType, "FILTER");
            case EditFilterEnv:
                return QString("%1%").arg(qRound(clamp01(sp.filterEnv) * 100.0f));
            case EditAttack: {
                const float sec = (0.005f + clamp01(sp.attack) * 1.2f);
                return QString("%1 ms").arg(qRound(sec * 1000.0f));
            }
            case EditDecay: {
                const float sec = (0.01f + clamp01(sp.decay) * 1.2f);
                return QString("%1 ms").arg(qRound(sec * 1000.0f));
            }
            case EditSustain:
                return QString("%1%").arg(qRound(clamp01(sp.sustain) * 100.0f));
            case EditRelease: {
                const float sec = (0.02f + clamp01(sp.release) * 1.6f);
                return QString("%1 ms").arg(qRound(sec * 1000.0f));
            }
            case EditLfoRate: {
                const float hz = 0.1f + clamp01(sp.lfoRate) * 8.0f;
                return QString("%1 Hz").arg(hz, 0, 'f', 2);
            }
            case EditLfoDepth:
                return QString("%1%").arg(qRound(clamp01(sp.lfoDepth) * 100.0f));
            case EditLfoShape:
                return lfoShapes.value(sp.lfoShape, "SINE");
            case EditLfoSync:
                if (sp.lfoSync <= 0) {
                    return QStringLiteral("FREE");
                }
                return lfoSyncLabels.value(sp.lfoSyncIndex, "1/4");
            case EditLfoTarget:
                return lfoTargets.value(sp.lfoTarget, "FILTER");
            default:
                break;
        }
        return QString("0");
    };

    auto customTargetValue = [&](CustomTarget target) {
        switch (target) {
            case CustomTarget::Osc1Wave:
                return waves.value(sp.osc1Wave, "WAVE");
            case CustomTarget::Osc2Wave:
                return waves.value(sp.osc2Wave, "WAVE");
            case CustomTarget::FmAmount:
                return QString("%1%").arg(qRound(clamp01(sp.fmAmount) * 100.0f));
            case CustomTarget::Ratio:
                return QString("x%1").arg(sp.ratio, 0, 'f', 2);
            case CustomTarget::Feedback:
                return QString("%1%").arg(qRound(clamp01(sp.feedback) * 100.0f));
            case CustomTarget::Osc1Detune:
                return QString("%1%").arg(qRound(clamp01(sp.osc1Detune) * 100.0f));
            case CustomTarget::Osc2Detune:
                return QString("%1%").arg(qRound(clamp01(sp.osc2Detune) * 100.0f));
            case CustomTarget::Osc2Gain:
                return QString("%1%").arg(qRound(clamp01(sp.osc2Gain) * 100.0f));
            case CustomTarget::FilterEnv:
                return QString("%1%").arg(qRound(clamp01(sp.filterEnv) * 100.0f));
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
            const float a = isSimple ? clamp01(sp.attack) : (0.1f + clamp01(sp.attack) * 0.45f);
            const float d = isSimple ? clamp01(sp.decay) : (0.1f + clamp01(sp.decay) * 0.35f);
            const float r = isSimple ? clamp01(sp.release) : (0.1f + clamp01(sp.release) * 0.4f);
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
    } else if (isCustomEngineType(synthTypeUpper)) {
        for (auto &param : m_editParams) {
            param.rect = QRectF();
        }
        m_filterPresetRects.clear();

        QRectF panelRect = content;
        drawPanel(panelRect, QString("T1 ENGINE - %1").arg(synthTypeUpper));
        QRectF paramsRect = panelRect.adjusted(Theme::px(10), Theme::px(28),
                                               -Theme::px(10), -Theme::px(10));

        const QVector<CustomControl> controls = customControlsForType(synthTypeUpper);
        const float gap = Theme::pxF(10.0f);
        if (synthTypeUpper == "CLUSTER") {
            const float knobH = paramsRect.height() * 0.28f;
            QRectF knobRow(paramsRect.left(), paramsRect.top(), paramsRect.width(), knobH);
            QRectF visualRect(paramsRect.left(), knobRow.bottom() + gap,
                              paramsRect.width(), paramsRect.bottom() - knobRow.bottom() - gap);

            const float knobGap = Theme::pxF(8.0f);
            const float knobW = (knobRow.width() - knobGap * 3.0f) / 4.0f;
            for (int i = 0; i < controls.size() && i < 4; ++i) {
                QRectF r(knobRow.left() + i * (knobW + knobGap), knobRow.top(),
                         knobW, knobRow.height());
                const CustomControl &ctrl = controls[i];
                EditParam &param = m_editParams[ctrl.paramType];
                param.rect = r;
                const bool selected = (ctrl.paramType == m_selectedEditParam);
                p.setBrush(selected ? Theme::accentAlt() : Theme::bg3());
                p.setPen(QPen(Theme::stroke(), 1.0));
                p.drawRoundedRect(r, Theme::px(8), Theme::px(8));
                drawKnobMarker(r, knobColors[i % 4]);
                p.setPen(selected ? Theme::bg0() : Theme::text());
                p.setFont(Theme::baseFont(9, QFont::DemiBold));
                p.drawText(r.adjusted(Theme::px(8), 0, -Theme::px(8), 0),
                           Qt::AlignLeft | Qt::AlignVCenter, ctrl.label);
                p.setPen(selected ? Theme::bg0() : Theme::textMuted());
                p.drawText(r.adjusted(Theme::px(8), 0, -Theme::px(8), 0),
                           Qt::AlignRight | Qt::AlignVCenter,
                           customTargetValue(ctrl.target));
            }

            p.setBrush(Theme::bg3());
            p.setPen(QPen(Theme::stroke(), 1.0));
            p.drawRoundedRect(visualRect, Theme::px(10), Theme::px(10));
            QRectF vis = visualRect.adjusted(Theme::px(10), Theme::px(10),
                                             -Theme::px(10), -Theme::px(10));
            const int lines = 6;
            const float spread = 0.06f + sp.osc1Detune * 0.4f;
            const float detune = 0.04f + sp.osc2Detune * 0.3f;
            const float motion = sp.fmAmount * 1.5f;
            const QColor colors[] = {Theme::accent(), Theme::accentAlt(),
                                     Theme::warn(), Theme::textMuted(),
                                     Theme::text(), Theme::danger()};
            for (int i = 0; i < lines; ++i) {
                const float center = (lines > 1) ? (static_cast<float>(i) / (lines - 1)) * 2.0f - 1.0f : 0.0f;
                const float freq = 1.0f + center * spread;
                const float phase = center * detune + motion;
                const QColor c = Theme::withAlpha(colors[i % 6], 180);
                drawWave(vis, c, [freq, phase](float t) {
                    return std::sin(kTwoPi * t * freq + phase);
                });
            }
        } else {
            const float cellW = (paramsRect.width() - gap) / 2.0f;
            const float cellH = (paramsRect.height() - gap) / 2.0f;
            auto cellRect = [&](int col, int row) {
                return QRectF(paramsRect.left() + col * (cellW + gap),
                              paramsRect.top() + row * (cellH + gap),
                              cellW, cellH);
            };

            for (int i = 0; i < controls.size() && i < 4; ++i) {
                const int row = i / 2;
                const int col = i % 2;
                const QRectF r = cellRect(col, row);
                const CustomControl &ctrl = controls[i];
                EditParam &param = m_editParams[ctrl.paramType];
                param.rect = r;
                const bool selected = (ctrl.paramType == m_selectedEditParam);
                p.setBrush(selected ? Theme::accentAlt() : Theme::bg3());
                p.setPen(QPen(Theme::stroke(), 1.0));
                p.drawRoundedRect(r, Theme::px(8), Theme::px(8));
                drawKnobMarker(r, knobColors[i % 4]);
                p.setPen(selected ? Theme::bg0() : Theme::text());
                p.setFont(Theme::baseFont(11, QFont::DemiBold));
                p.drawText(r.adjusted(Theme::px(10), 0, -Theme::px(10), 0),
                           Qt::AlignLeft | Qt::AlignVCenter, ctrl.label);
                p.setPen(selected ? Theme::bg0() : Theme::textMuted());
                p.drawText(r.adjusted(Theme::px(10), 0, -Theme::px(10), 0),
                           Qt::AlignRight | Qt::AlignVCenter,
                           customTargetValue(ctrl.target));
            }
        }
    } else {
        for (auto &param : m_editParams) {
            param.rect = QRectF();
        }
        m_filterPresetRects.clear();
        if (isSimple) {
            const float artW = content.width() * 0.38f;
            QRectF leftRect(content.left(), content.top(),
                            content.width() - artW - gap, content.height());
            QRectF artRect(leftRect.right() + gap, content.top(), artW, content.height());

            drawPanel(leftRect, "OSC 1");
            QRectF paramsRect = leftRect.adjusted(Theme::px(10), Theme::px(28), -Theme::px(10),
                                                  -Theme::px(10));
            const float paramGap = Theme::pxF(8.0f);
            const int cols = 3;
            const int rows = 3;
            const float cellW = (paramsRect.width() - paramGap * (cols - 1)) / cols;
            const float cellH = (paramsRect.height() - paramGap * (rows - 1)) / rows;

            auto drawParamCell = [&](const QRectF &cell, int paramType) {
                EditParam &param = m_editParams[paramType];
                param.rect = cell;
                const bool selected = (paramType == m_selectedEditParam);
                p.setBrush(selected ? Theme::accentAlt() : Theme::bg3());
                p.setPen(QPen(Theme::stroke(), 1.0));
                p.drawRoundedRect(cell, Theme::px(6), Theme::px(6));
                QString label = param.label;
                if (paramType == EditOsc2Wave) label = "WAVE2";
                if (paramType == EditOsc2Voices) label = "VOI2";
                if (paramType == EditOsc2Detune) label = "DET2";
                if (paramType == EditOsc2Gain) label = "VOL2";
                p.setPen(selected ? Theme::bg0() : Theme::text());
                p.setFont(Theme::baseFont(10, QFont::DemiBold));
                p.drawText(cell.adjusted(Theme::px(8), 0, -Theme::px(8), 0),
                           Qt::AlignLeft | Qt::AlignVCenter, label);
                p.setPen(selected ? Theme::bg0() : Theme::textMuted());
                p.drawText(cell.adjusted(Theme::px(8), 0, -Theme::px(8), 0),
                           Qt::AlignRight | Qt::AlignVCenter, formatValue(param.type));
            };

            auto cellRect = [&](int col, int row) {
                return QRectF(paramsRect.left() + col * (cellW + paramGap),
                              paramsRect.top() + row * (cellH + paramGap),
                              cellW, cellH);
            };

            drawParamCell(cellRect(0, 0), EditOsc1Wave);
            drawParamCell(cellRect(1, 0), EditOsc1Voices);
            drawParamCell(cellRect(2, 0), EditOsc1Detune);
            drawParamCell(cellRect(0, 1), EditOsc1Gain);
            drawParamCell(cellRect(1, 1), EditOctave);
            drawParamCell(cellRect(2, 1), EditOsc2Wave);
            drawParamCell(cellRect(0, 2), EditOsc2Voices);
            drawParamCell(cellRect(1, 2), EditOsc2Detune);
            drawParamCell(cellRect(2, 2), EditOsc2Gain);

            drawPanel(artRect, "PHOTO SLOT");
            QRectF artInner = artRect.adjusted(Theme::px(8), Theme::px(20), -Theme::px(8), -Theme::px(8));
            const QPixmap &art = Theme::leftBgPixmap();
            if (!art.isNull()) {
                const qreal sx = artInner.width() / art.width();
                const qreal sy = artInner.height() / art.height();
                const qreal scale = qMin(sx, sy);
                const QSizeF targetSize(art.width() * scale, art.height() * scale);
                const QRectF target(QPointF(artInner.center().x() - targetSize.width() * 0.5,
                                            artInner.center().y() - targetSize.height() * 0.5),
                                    targetSize);
                p.save();
                p.setOpacity(0.75);
                p.drawPixmap(target, art, QRectF(0, 0, art.width(), art.height()));
                p.restore();
            } else {
                p.setPen(QPen(Theme::accent(), 1.0, Qt::DashLine));
                p.drawRoundedRect(artInner, Theme::px(6), Theme::px(6));
                p.setPen(Theme::textMuted());
                p.setFont(Theme::baseFont(8, QFont::DemiBold));
                p.drawText(artInner.adjusted(Theme::px(6), Theme::px(6), -Theme::px(6), -Theme::px(6)),
                           Qt::AlignLeft | Qt::AlignTop,
                           "PLACE IMAGE:\nassets/bg_left.png\nor set GROOVEBOX_BG_LEFT");
            }
        } else {

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
        QRectF filterRect(adsrRect.right() + gap, modRow.top(),
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

        drawPanel(filterRect, "FILTER");
        QRectF filterInner = filterRect.adjusted(Theme::px(8), Theme::px(20), -Theme::px(8), -Theme::px(8));
        QRectF filterVis = filterInner;
        filterVis.setHeight(filterInner.height() * 0.55f);

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

        QRectF filterParams = filterInner;
        filterParams.setTop(filterVis.bottom() + Theme::pxF(6.0f));
        const float filterGap = Theme::pxF(8.0f);
        const float filterW = (filterParams.width() - filterGap) / 2.0f;
        drawSmallParam(QRectF(filterParams.left(), filterParams.top(), filterW, filterParams.height()),
                       EditCutoff);
        drawSmallParam(QRectF(filterParams.left() + filterW + filterGap, filterParams.top(),
                              filterW, filterParams.height()),
                       EditResonance);

        drawPanel(filterRow, "FILTER PRESETS");
        QRectF presetArea = filterRow.adjusted(Theme::px(8), Theme::px(22), -Theme::px(8), -Theme::px(8));

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
        }
    }

    if (m_modMenuOpen && m_modMenuRect.isValid()) {
        const QRectF menu = m_modMenuRect.adjusted(Theme::px(4), Theme::px(4),
                                                   -Theme::px(4), -Theme::px(4));
        p.setBrush(Theme::bg1());
        p.setPen(QPen(Theme::stroke(), 1.2));
        p.drawRoundedRect(menu, Theme::px(12), Theme::px(12));

        const float sectionGap = Theme::pxF(10.0f);
        const float sectionH = (menu.height() - sectionGap * 2.0f) / 3.0f;
        QRectF lfoRect(menu.left(), menu.top(), menu.width(), sectionH);
        QRectF filterRect(menu.left(), lfoRect.bottom() + sectionGap, menu.width(), sectionH);
        QRectF adsrRect(menu.left(), filterRect.bottom() + sectionGap, menu.width(),
                        menu.bottom() - filterRect.bottom() - sectionGap);

        auto drawMiniParam = [&](const QRectF &cell, int paramType) {
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

        // LFO panel.
        drawPanel(lfoRect, "T4 LFO");
        QRectF lfoInner = lfoRect.adjusted(Theme::px(8), Theme::px(20), -Theme::px(8), -Theme::px(8));
        QRectF lfoWave = lfoInner;
        lfoWave.setHeight(lfoInner.height() * 0.45f);
        auto lfoShapeValue = [](int shape, float t) {
            t = std::max(0.0f, std::min(1.0f, t));
            switch (shape) {
                case 1: // triangle
                    return 4.0f * std::fabs(t - 0.5f) - 1.0f;
                case 2: // square
                    return t < 0.5f ? 1.0f : -1.0f;
                case 3: // saw
                    return 2.0f * t - 1.0f;
                case 4: { // random hold
                    const float step = (t < 0.33f) ? -0.4f : (t < 0.66f ? 0.7f : -0.1f);
                    return step;
                }
                default:
                    return std::sin(kTwoPi * t);
            }
        };
        drawWave(lfoWave, Theme::accent(), [&](float t) {
            return lfoShapeValue(sp.lfoShape, t);
        });
        QRectF lfoParams = lfoInner;
        lfoParams.setTop(lfoWave.bottom() + Theme::pxF(6.0f));
        const float lfoGap = Theme::pxF(6.0f);
        const float lfoCellW = (lfoParams.width() - lfoGap) / 2.0f;
        const float lfoCellH = (lfoParams.height() - lfoGap * 2.0f) / 3.0f;
        const QVector<int> lfoParamList = {EditLfoShape, EditLfoRate, EditLfoDepth,
                                           EditLfoSync, EditLfoTarget};
        int lfoIdx = 0;
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 2; ++col) {
                if (lfoIdx >= lfoParamList.size()) {
                    break;
                }
                const QRectF cell(lfoParams.left() + col * (lfoCellW + lfoGap),
                                  lfoParams.top() + row * (lfoCellH + lfoGap),
                                  lfoCellW, lfoCellH);
                drawMiniParam(cell, lfoParamList[lfoIdx++]);
            }
        }

        // Filter panel.
        drawPanel(filterRect, "T3 FX (FILTER)");
        QRectF filterInner = filterRect.adjusted(Theme::px(8), Theme::px(20),
                                                 -Theme::px(8), -Theme::px(8));
        QRectF filterVis = filterInner;
        filterVis.setHeight(filterInner.height() * 0.45f);
        {
            QPainterPath curve;
            const float left = filterVis.left();
            const float right = filterVis.right();
            const float midY = filterVis.center().y();
            const float topY = filterVis.top();
            const float bottomY = filterVis.bottom();
            switch (sp.filterType) {
                case 0:
                    curve.moveTo(left, topY);
                    curve.lineTo(right, bottomY);
                    break;
                case 1:
                    curve.moveTo(left, bottomY);
                    curve.lineTo(right, topY);
                    break;
                case 2:
                    curve.moveTo(left, bottomY);
                    curve.lineTo(filterVis.center().x(), topY);
                    curve.lineTo(right, bottomY);
                    break;
                case 3:
                    curve.moveTo(left, topY);
                    curve.lineTo(filterVis.center().x(), bottomY);
                    curve.lineTo(right, topY);
                    break;
                default:
                    curve.moveTo(left, midY);
                    curve.lineTo(right, midY);
                    break;
            }
            p.setPen(QPen(Theme::accent(), Theme::pxF(1.4f)));
            p.drawPath(curve);
        }
        QRectF filterParams = filterInner;
        filterParams.setTop(filterVis.bottom() + Theme::pxF(6.0f));
        const float filterGap = Theme::pxF(6.0f);
        const float filterW = (filterParams.width() - filterGap) / 2.0f;
        const float filterH = (filterParams.height() - filterGap) / 2.0f;
        drawMiniParam(QRectF(filterParams.left(), filterParams.top(), filterW, filterH), EditCutoff);
        drawMiniParam(QRectF(filterParams.left() + filterW + filterGap, filterParams.top(),
                             filterW, filterH), EditResonance);
        drawMiniParam(QRectF(filterParams.left(), filterParams.top() + filterH + filterGap,
                             filterW, filterH), EditFilterEnv);
        drawMiniParam(QRectF(filterParams.left() + filterW + filterGap,
                             filterParams.top() + filterH + filterGap,
                             filterW, filterH), EditFilterType);

        // ADSR panel.
        drawPanel(adsrRect, "T2 ENVELOPE");
        QRectF adsrInner = adsrRect.adjusted(Theme::px(8), Theme::px(20),
                                             -Theme::px(8), -Theme::px(8));
        QRectF adsrWave = adsrInner;
        adsrWave.setHeight(adsrInner.height() * 0.5f);
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
            p.setPen(QPen(Theme::accent(), Theme::pxF(1.4f)));
            p.drawPath(env);
        }
        QRectF adsrParams = adsrInner;
        adsrParams.setTop(adsrWave.bottom() + Theme::pxF(6.0f));
        const float adsrGap = Theme::pxF(6.0f);
        const float adsrW = (adsrParams.width() - adsrGap) / 2.0f;
        const float adsrH = (adsrParams.height() - adsrGap) / 2.0f;
        drawMiniParam(QRectF(adsrParams.left(), adsrParams.top(), adsrW, adsrH), EditAttack);
        drawMiniParam(QRectF(adsrParams.left() + adsrW + adsrGap, adsrParams.top(),
                             adsrW, adsrH), EditDecay);
        drawMiniParam(QRectF(adsrParams.left(), adsrParams.top() + adsrH + adsrGap,
                             adsrW, adsrH), EditSustain);
        drawMiniParam(QRectF(adsrParams.left() + adsrW + adsrGap,
                             adsrParams.top() + adsrH + adsrGap,
                             adsrW, adsrH), EditRelease);
    }

    if (m_showPresetMenu) {
        m_presetPanelRect = panel.adjusted(Theme::px(6), Theme::px(6), -Theme::px(6), -Theme::px(6));
        p.setBrush(Theme::bg2());
        p.setPen(QPen(Theme::stroke(), 1.4));
        p.drawRoundedRect(m_presetPanelRect, Theme::px(14), Theme::px(14));

        const QRectF titleRect(m_presetPanelRect.left() + Theme::px(12),
                               m_presetPanelRect.top() + Theme::px(8),
                               m_presetPanelRect.width() - Theme::px(24), Theme::px(22));
        p.setPen(Theme::accent());
        p.setFont(Theme::condensedFont(12, QFont::Bold));
        p.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, "PRESET BROWSER");
        p.setPen(Theme::textMuted());
        p.setFont(Theme::baseFont(9, QFont::DemiBold));
        p.drawText(titleRect, Qt::AlignRight | Qt::AlignVCenter, "P / ESC to close");

        const QRectF contentRect(m_presetPanelRect.left() + Theme::px(10),
                                 titleRect.bottom() + Theme::px(8),
                                 m_presetPanelRect.width() - Theme::px(20),
                                 m_presetPanelRect.height() - Theme::px(20) - titleRect.height());

        const bool showBanks = m_categories.size() > 1;
        const float bankW = showBanks ? contentRect.width() * 0.28f : 0.0f;
        QRectF bankRect = contentRect;
        QRectF presetRect = contentRect;
        if (showBanks) {
            bankRect.setWidth(bankW);
            presetRect.setLeft(bankRect.right() + Theme::pxF(10.0f));
        }

        m_categoryRects.clear();
        if (showBanks) {
            p.setPen(Theme::textMuted());
            p.setFont(Theme::baseFont(9, QFont::DemiBold));
            p.drawText(QRectF(bankRect.left(), bankRect.top() - Theme::px(18),
                              bankRect.width(), Theme::px(16)),
                       Qt::AlignLeft | Qt::AlignVCenter, "LIBRARIES");
            const float rowH = Theme::pxF(30.0f);
            float by = bankRect.top();
            for (int i = 0; i < m_categories.size(); ++i) {
                QRectF r(bankRect.left(), by, bankRect.width(), rowH - Theme::pxF(4.0f));
                m_categoryRects.push_back(r);
                const bool active = (i == m_selectedCategory);
                p.setBrush(active ? Theme::accentAlt() : Theme::bg3());
                p.setPen(QPen(Theme::stroke(), 1.0));
                p.drawRoundedRect(r, Theme::px(6), Theme::px(6));
                p.setPen(active ? Theme::bg0() : Theme::text());
                p.setFont(Theme::baseFont(10, QFont::DemiBold));
                p.drawText(r.adjusted(Theme::px(6), 0, -Theme::px(4), 0),
                           Qt::AlignLeft | Qt::AlignVCenter, m_categories[i]);
                by += rowH;
                if (by > bankRect.bottom() - Theme::pxF(6.0f)) {
                    break;
                }
            }
        }

        m_presetRows.clear();
        const float rowH = Theme::pxF(30.0f);
        const QString selectedCat =
            m_categories.value(qBound(0, m_selectedCategory, m_categories.size() - 1));
        QVector<PresetEntry> filtered;
        filtered.reserve(m_allPresets.size());
        for (const auto &entry : m_allPresets) {
            if (entry.category == selectedCat) {
                filtered.push_back(entry);
            }
        }
        const int maxVisible = std::max(
            1, static_cast<int>(std::floor((presetRect.height() - Theme::pxF(4.0f)) / rowH)));
        const int maxScroll = std::max(0, filtered.size() - maxVisible);
        m_presetScroll = qBound(0, m_presetScroll, maxScroll);

        float py = presetRect.top();
        int drawn = 0;
        for (int idx = m_presetScroll; idx < filtered.size(); ++idx) {
            const PresetEntry &item = filtered[idx];
            PresetRow row;
            row.header = false;
            row.label = item.label;
            row.presetId = item.preset;
            row.bank = item.bank;
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
                QString::compare(row.bank, bankName, Qt::CaseInsensitive) == 0 ||
                (isFmBank(row.bank) && isFmBank(bankName));
            const bool active =
                bankMatch && QString::compare(programName, row.presetId, Qt::CaseInsensitive) == 0;
            p.setBrush(active ? Theme::accentAlt() : Theme::bg3());
            p.setPen(QPen(Theme::stroke(), 1.0));
            p.drawRoundedRect(row.rect, Theme::px(6), Theme::px(6));
            p.setPen(active ? Theme::bg0() : Theme::text());
            p.setFont(Theme::baseFont(10, QFont::DemiBold));
            p.drawText(row.rect.adjusted(Theme::px(8), 0, -Theme::px(6), 0),
                       Qt::AlignLeft | Qt::AlignVCenter, row.label);
        }

        if (maxScroll > 0) {
            p.setPen(Theme::textMuted());
            p.setFont(Theme::baseFont(9, QFont::DemiBold));
            const QString marker = QString("%1/%2").arg(m_presetScroll + 1).arg(maxScroll + 1);
            p.drawText(QRectF(presetRect.left(), presetRect.bottom() - Theme::px(16),
                              presetRect.width(), Theme::px(14)),
                       Qt::AlignRight | Qt::AlignVCenter, marker);
        }
    } else {
        m_presetPanelRect = QRectF();
    }

    if (m_assignMenuOpen && m_assignParamType >= 0) {
        const int targetIndex = modTargetForParam(m_assignParamType, synthTypeUpper);
        if (targetIndex > 0 && targetIndex < PadBank::kModTargetCount) {
            const float w = Theme::pxF(180.0f);
            const float h = Theme::pxF(60.0f);
            QRectF r(m_holdPos.x() - w * 0.5f, m_holdPos.y() - h * 0.5f, w, h);
            const float minX = Theme::pxF(8.0f);
            const float minY = Theme::pxF(8.0f);
            if (r.left() < minX) r.moveLeft(minX);
            if (r.top() < minY) r.moveTop(minY);
            if (r.right() > rect().right() - minX) r.moveRight(rect().right() - minX);
            if (r.bottom() > rect().bottom() - minY) r.moveBottom(rect().bottom() - minY);
            m_assignRect = r;

            const float pad = Theme::pxF(6.0f);
            const float cellW = (r.width() - pad * 3.0f) / 2.0f;
            const float cellH = r.height() - pad * 2.0f;
            m_assignLfoRect = QRectF(r.left() + pad, r.top() + pad, cellW, cellH);
            m_assignEnvRect = QRectF(m_assignLfoRect.right() + pad, r.top() + pad, cellW, cellH);

            const bool lfoOn = sp.lfoAssign[static_cast<size_t>(targetIndex)] > 0.001f;
            const bool envOn = sp.envAssign[static_cast<size_t>(targetIndex)] > 0.001f;

            p.setBrush(Theme::bg2());
            p.setPen(QPen(Theme::stroke(), 1.0));
            p.drawRoundedRect(r, Theme::px(8), Theme::px(8));

            p.setBrush(lfoOn ? Theme::accentAlt() : Theme::bg3());
            p.setPen(QPen(Theme::stroke(), 1.0));
            p.drawRoundedRect(m_assignLfoRect, Theme::px(6), Theme::px(6));
            p.setPen(lfoOn ? Theme::bg0() : Theme::text());
            p.setFont(Theme::baseFont(9, QFont::DemiBold));
            p.drawText(m_assignLfoRect, Qt::AlignCenter, "LFO");

            p.setBrush(envOn ? Theme::accent() : Theme::bg3());
            p.setPen(QPen(Theme::stroke(), 1.0));
            p.drawRoundedRect(m_assignEnvRect, Theme::px(6), Theme::px(6));
            p.setPen(envOn ? Theme::bg0() : Theme::text());
            p.drawText(m_assignEnvRect, Qt::AlignCenter, "ADSR");
        }
    }

    if (m_bindSource != BindSource::None && m_editorMode == EditorMode::None) {
        const QRectF banner(panel.left() + Theme::pxF(16.0f), panel.top() + Theme::pxF(46.0f),
                            panel.width() - Theme::pxF(32.0f), Theme::pxF(34.0f));
        p.setBrush(Theme::accentAlt());
        p.setPen(QPen(Theme::stroke(), 1.0));
        p.drawRoundedRect(banner, Theme::px(8), Theme::px(8));
        p.setPen(Theme::bg0());
        p.setFont(Theme::baseFont(10, QFont::DemiBold));
        const QString source =
            (m_bindSource == BindSource::Lfo)
                ? QString("LFO %1").arg(m_bindSlot + 1)
                : QString("ADSR %1").arg(m_bindSlot + 1);
        p.drawText(banner, Qt::AlignCenter,
                   QString("BIND %1: click target parameter").arg(source));
    }

    if (m_editorMode != EditorMode::None) {
        const QRectF overlay = panel.adjusted(Theme::px(10), Theme::px(42),
                                              -Theme::px(10), -Theme::px(10));
        m_editorContentRect = overlay;
        p.save();
        p.setBrush(Theme::withAlpha(Theme::bg0(), 200));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(overlay, Theme::px(14), Theme::px(14));

        p.setBrush(Theme::bg1());
        p.setPen(QPen(Theme::stroke(), 1.2));
        p.drawRoundedRect(overlay, Theme::px(14), Theme::px(14));

        const QRectF head(overlay.left() + Theme::pxF(14.0f), overlay.top() + Theme::pxF(10.0f),
                          overlay.width() - Theme::pxF(28.0f), Theme::pxF(28.0f));
        QString title = "LFO";
        if (m_editorMode == EditorMode::Env) {
            title = "ADSR";
        } else if (m_editorMode == EditorMode::Filter) {
            title = "FILTER";
        }
        p.setPen(Theme::text());
        p.setFont(Theme::condensedFont(14, QFont::Bold));
        p.drawText(head, Qt::AlignLeft | Qt::AlignVCenter, title);
        p.setPen(Theme::textMuted());
        p.setFont(Theme::baseFont(9, QFont::DemiBold));
        p.drawText(head, Qt::AlignRight | Qt::AlignVCenter, "ESC close");

        QRectF body = overlay.adjusted(Theme::px(14), Theme::px(44), -Theme::px(14), -Theme::px(14));
        const float navW = Theme::pxF(28.0f);
        const float gapCard = Theme::pxF(12.0f);
        const float cardW = std::min(body.width() - navW * 2.0f - gapCard * 2.0f, Theme::pxF(340.0f));
        const float cardH = body.height();
        const int visibleCount = std::max(1, static_cast<int>((body.width() - navW * 2.0f + gapCard) /
                                                              (cardW + gapCard)));

        m_editorLeftRect = QRectF(body.left(), body.center().y() - Theme::pxF(24.0f),
                                  navW, Theme::pxF(48.0f));
        m_editorRightRect = QRectF(body.right() - navW, body.center().y() - Theme::pxF(24.0f),
                                   navW, Theme::pxF(48.0f));
        auto drawNav = [&](const QRectF &r, const QString &txt, bool active) {
            p.setBrush(active ? Theme::bg3() : Theme::withAlpha(Theme::bg2(), 120));
            p.setPen(QPen(Theme::stroke(), 1.0));
            p.drawRoundedRect(r, Theme::px(8), Theme::px(8));
            p.setPen(active ? Theme::text() : Theme::textMuted());
            p.setFont(Theme::condensedFont(13, QFont::Bold));
            p.drawText(r, Qt::AlignCenter, txt);
        };

        auto pushEditorHit = [&](const QRectF &r, const QString &id, int slot, int value = 0) {
            EditorHit hit;
            hit.rect = r;
            hit.id = id;
            hit.slot = slot;
            hit.value = value;
            m_editorHits.push_back(hit);
        };

        auto drawBox = [&](const QRectF &r, const QString &label, const QString &value,
                           const QString &id, int slot, bool active = false, int hitValue = 0) {
            p.setBrush(active ? Theme::accentAlt() : Theme::bg2());
            p.setPen(QPen(Theme::stroke(), 1.0));
            p.drawRoundedRect(r, Theme::px(8), Theme::px(8));
            p.setPen(active ? Theme::bg0() : Theme::textMuted());
            p.setFont(Theme::baseFont(8, QFont::DemiBold));
            p.drawText(QRectF(r.left() + Theme::pxF(8.0f), r.top() + Theme::pxF(4.0f),
                              r.width() - Theme::pxF(16.0f), Theme::pxF(12.0f)),
                       Qt::AlignLeft | Qt::AlignVCenter, label);
            p.setPen(active ? Theme::bg0() : Theme::text());
            p.setFont(Theme::baseFont(10, QFont::DemiBold));
            p.drawText(QRectF(r.left() + Theme::pxF(8.0f), r.center().y() - Theme::pxF(10.0f),
                              r.width() - Theme::pxF(16.0f), Theme::pxF(20.0f)),
                       Qt::AlignCenter, value);
            p.setPen(active ? Theme::bg0() : Theme::textMuted());
            p.setFont(Theme::baseFont(7, QFont::DemiBold));
            p.drawText(QRectF(r.left() + Theme::pxF(6.0f), r.bottom() - Theme::pxF(14.0f),
                              r.width() - Theme::pxF(12.0f), Theme::pxF(10.0f)),
                       Qt::AlignCenter, "- / +");
            pushEditorHit(r, id, slot, hitValue);
        };

        auto drawAssignments = [&](const QRectF &r, const std::array<float, PadBank::kModTargetCount> &assign) {
            QStringList labels;
            for (int i = 1; i < PadBank::kModTargetCount; ++i) {
                if (assign[static_cast<size_t>(i)] > 0.001f) {
                    labels << modTargetLabel(i);
                }
            }
            p.setPen(Theme::textMuted());
            p.setFont(Theme::baseFont(7, QFont::DemiBold));
            p.drawText(r, Qt::AlignLeft | Qt::AlignTop,
                       labels.isEmpty() ? "NO TARGETS" : labels.join("  "));
        };

        if (m_editorMode == EditorMode::Lfo) {
            QVector<int> moduleSlots;
            for (int i = 0; i < PadBank::kLfoModuleCount; ++i) {
                if (sp.lfoModules[static_cast<size_t>(i)].enabled) {
                    moduleSlots.push_back(i);
                }
            }
            bool hasFree = moduleSlots.size() < PadBank::kLfoModuleCount;
            m_lfoScroll = qBound(0, m_lfoScroll, std::max(0, moduleSlots.size() - visibleCount));
            drawNav(m_editorLeftRect, "<", m_lfoScroll > 0);
            drawNav(m_editorRightRect, ">", m_lfoScroll + visibleCount < moduleSlots.size());
            float x = m_editorLeftRect.right() + gapCard;
            int drawn = 0;
            for (int i = m_lfoScroll; i < moduleSlots.size() && drawn < visibleCount; ++i, ++drawn) {
                const int slot = moduleSlots[i];
                const auto &module = sp.lfoModules[static_cast<size_t>(slot)];
                const QRectF card(x, body.top(), cardW, cardH);
                x += cardW + gapCard;
                const bool activeCard = (slot == m_activeLfoSlot);
                p.setBrush(activeCard ? Theme::withAlpha(Theme::accentAlt(), 52) : Theme::bg2());
                p.setPen(QPen(activeCard ? Theme::accentAlt() : Theme::stroke(), activeCard ? 1.6 : 1.0));
                p.drawRoundedRect(card, Theme::px(12), Theme::px(12));
                pushEditorHit(card, "lfo-select", slot);

                QRectF titleRect(card.left() + Theme::pxF(12.0f), card.top() + Theme::pxF(10.0f),
                                 card.width() - Theme::pxF(24.0f), Theme::pxF(20.0f));
                p.setPen(Theme::text());
                p.setFont(Theme::condensedFont(12, QFont::Bold));
                p.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                           QString("LFO %1").arg(slot + 1));

                QRectF bindRect(card.right() - Theme::pxF(74.0f), card.top() + Theme::pxF(8.0f),
                                Theme::pxF(42.0f), Theme::pxF(20.0f));
                p.setBrush(Theme::bg3());
                p.setPen(QPen(Theme::stroke(), 1.0));
                p.drawRoundedRect(bindRect, Theme::px(6), Theme::px(6));
                p.setPen(Theme::text());
                p.setFont(Theme::baseFont(8, QFont::DemiBold));
                p.drawText(bindRect, Qt::AlignCenter, "BIND");
                pushEditorHit(bindRect, "lfo-bind", slot);

                QRectF closeRect(card.right() - Theme::pxF(24.0f), card.top() + Theme::pxF(8.0f),
                                 Theme::pxF(16.0f), Theme::pxF(20.0f));
                p.setPen(Theme::textMuted());
                p.drawText(closeRect, Qt::AlignCenter, "x");
                pushEditorHit(closeRect, "lfo-remove", slot);

                QRectF kindA(card.left() + Theme::pxF(12.0f), card.top() + Theme::pxF(38.0f),
                             Theme::pxF(76.0f), Theme::pxF(22.0f));
                QRectF kindB(kindA.right() + Theme::pxF(8.0f), kindA.top(),
                             Theme::pxF(76.0f), kindA.height());
                p.setBrush(module.kind == 0 ? Theme::accent() : Theme::bg3());
                p.setPen(QPen(Theme::stroke(), 1.0));
                p.drawRoundedRect(kindA, Theme::px(7), Theme::px(7));
                p.setBrush(module.kind == 1 ? Theme::accentAlt() : Theme::bg3());
                p.drawRoundedRect(kindB, Theme::px(7), Theme::px(7));
                p.setPen(Theme::bg0());
                p.drawText(kindA, Qt::AlignCenter, "SMOOTH");
                p.drawText(kindB, Qt::AlignCenter, "TRANCE");
                pushEditorHit(kindA, "lfo-kind", slot, 0);
                pushEditorHit(kindB, "lfo-kind", slot, 1);

                if (module.kind == 1) {
                    QRectF ringRect(card.left() + Theme::pxF(24.0f), card.top() + Theme::pxF(74.0f),
                                    card.width() - Theme::pxF(48.0f), card.height() * 0.42f);
                    const QPointF center = ringRect.center();
                    const float outer = std::min(ringRect.width(), ringRect.height()) * 0.42f;
                    const float inner = outer * 0.63f;
                    const int steps = std::max(1, std::min(PadBank::kLfoPatternSteps, module.steps));
                    for (int step = 0; step < steps; ++step) {
                        const float a0 = -90.0f + 360.0f * (step / static_cast<float>(steps));
                        const float a1 = -90.0f + 360.0f * ((step + 1) / static_cast<float>(steps));
                        const float level = clamp01(module.pattern[static_cast<size_t>(step)]);
                        QPainterPath seg;
                        seg.arcMoveTo(QRectF(center.x() - outer, center.y() - outer, outer * 2, outer * 2), a0);
                        seg.arcTo(QRectF(center.x() - outer, center.y() - outer, outer * 2, outer * 2),
                                  a0, a1 - a0);
                        seg.arcTo(QRectF(center.x() - inner, center.y() - inner, inner * 2, inner * 2),
                                  a1, a0 - a1);
                        seg.closeSubpath();
                        p.setBrush(level > 0.5f ? Theme::accent() : Theme::bg3());
                        p.setPen(QPen(Theme::stroke(), 1.0));
                        p.drawPath(seg);
                        const float mid = (a0 + a1) * 0.5f * static_cast<float>(M_PI) / 180.0f;
                        const float rr = (outer + inner) * 0.5f;
                        QRectF hitRect(center.x() + std::cos(mid) * rr - Theme::pxF(12.0f),
                                       center.y() + std::sin(mid) * rr - Theme::pxF(12.0f),
                                       Theme::pxF(24.0f), Theme::pxF(24.0f));
                        pushEditorHit(hitRect, "lfo-step", slot, step);
                    }
                    const double nowSec =
                        QDateTime::currentMSecsSinceEpoch() / 1000.0;
                    const float markerAng =
                        -90.0f + 360.0f *
                                     std::fmod(static_cast<float>(nowSec * (0.2 + module.rate * 2.0f)),
                                               1.0f);
                    const float markerRad = markerAng * static_cast<float>(M_PI) / 180.0f;
                    const QPointF marker(center.x() + std::cos(markerRad) * outer,
                                         center.y() + std::sin(markerRad) * outer);
                    p.setBrush(Theme::warn());
                    p.setPen(Qt::NoPen);
                    p.drawEllipse(marker, Theme::pxF(5.0f), Theme::pxF(5.0f));

                    const float stepsY = ringRect.bottom() + Theme::pxF(8.0f);
                    drawBox(QRectF(card.left() + Theme::pxF(14.0f), stepsY, Theme::pxF(96.0f), Theme::pxF(48.0f)),
                            "STEPS", QString::number(module.steps), "lfo-steps", slot);
                    drawBox(QRectF(card.left() + Theme::pxF(120.0f), stepsY, Theme::pxF(96.0f), Theme::pxF(48.0f)),
                            "DEPTH", QString("%1%").arg(qRound(module.depth * 100.0f)),
                            "lfo-depth", slot);
                    drawBox(QRectF(card.left() + Theme::pxF(226.0f), stepsY, Theme::pxF(96.0f), Theme::pxF(48.0f)),
                            "SYNC", QString("1/%1").arg(module.sync ? (1 << std::min(5, module.syncIndex)) : 4),
                            "lfo-sync", slot, module.sync != 0);
                } else {
                    QRectF waveRect(card.left() + Theme::pxF(18.0f), card.top() + Theme::pxF(74.0f),
                                    card.width() - Theme::pxF(36.0f), card.height() * 0.34f);
                    p.setBrush(Theme::bg3());
                    p.setPen(QPen(Theme::stroke(), 1.0));
                    p.drawRoundedRect(waveRect, Theme::px(10), Theme::px(10));
                    QRectF graph = waveRect.adjusted(Theme::pxF(10.0f), Theme::pxF(10.0f),
                                                    -Theme::pxF(10.0f), -Theme::pxF(10.0f));
                    drawWave(graph, Theme::accent(), [&](float t) {
                        const float sine = std::sin(kTwoPi * t);
                        const float square = (t < 0.5f) ? 1.0f : -1.0f;
                        return sine * (1.0f - module.morph) + square * module.morph;
                    });
                    const float rowY = waveRect.bottom() + Theme::pxF(12.0f);
                    drawBox(QRectF(card.left() + Theme::pxF(14.0f), rowY, Theme::pxF(96.0f), Theme::pxF(48.0f)),
                            "SHAPE", QString("%1%").arg(qRound(module.morph * 100.0f)),
                            "lfo-shape", slot);
                    drawBox(QRectF(card.left() + Theme::pxF(120.0f), rowY, Theme::pxF(96.0f), Theme::pxF(48.0f)),
                            "RATE", QString("%1").arg(module.sync ? QString("SYNC") :
                                                      QString("%1Hz").arg(0.05f + module.rate * 10.0f, 0, 'f', 2)),
                            "lfo-rate", slot);
                    drawBox(QRectF(card.left() + Theme::pxF(226.0f), rowY, Theme::pxF(96.0f), Theme::pxF(48.0f)),
                            "DEPTH", QString("%1%").arg(qRound(module.depth * 100.0f)),
                            "lfo-depth", slot);
                    drawBox(QRectF(card.left() + Theme::pxF(14.0f), rowY + Theme::pxF(58.0f),
                                   Theme::pxF(96.0f), Theme::pxF(48.0f)),
                            "SYNC", module.sync ? "ON" : "OFF", "lfo-sync", slot, module.sync != 0);
                }

                QRectF assignRect(card.left() + Theme::pxF(14.0f), card.bottom() - Theme::pxF(52.0f),
                                  card.width() - Theme::pxF(28.0f), Theme::pxF(38.0f));
                drawAssignments(assignRect, module.assign);
            }
            if (hasFree && drawn < visibleCount) {
                m_editorAddRect = QRectF(x, body.top(), cardW, cardH);
                p.setBrush(Theme::withAlpha(Theme::bg2(), 160));
                p.setPen(QPen(Theme::stroke(), 1.0, Qt::DashLine));
                p.drawRoundedRect(m_editorAddRect, Theme::px(12), Theme::px(12));
                p.setPen(Theme::accent());
                p.setFont(Theme::condensedFont(28, QFont::Bold));
                p.drawText(m_editorAddRect, Qt::AlignCenter, "+");
                p.setFont(Theme::baseFont(10, QFont::DemiBold));
                p.drawText(m_editorAddRect.adjusted(0, Theme::px(42), 0, 0),
                           Qt::AlignHCenter | Qt::AlignTop, "ADD LFO");
            }
        } else if (m_editorMode == EditorMode::Env) {
            QVector<int> moduleSlots;
            for (int i = 0; i < PadBank::kEnvModuleCount; ++i) {
                if (sp.envModules[static_cast<size_t>(i)].enabled) {
                    moduleSlots.push_back(i);
                }
            }
            bool hasFree = moduleSlots.size() < PadBank::kEnvModuleCount;
            m_envScroll = qBound(0, m_envScroll, std::max(0, moduleSlots.size() - visibleCount));
            drawNav(m_editorLeftRect, "<", m_envScroll > 0);
            drawNav(m_editorRightRect, ">", m_envScroll + visibleCount < moduleSlots.size());
            float x = m_editorLeftRect.right() + gapCard;
            int drawn = 0;
            for (int i = m_envScroll; i < moduleSlots.size() && drawn < visibleCount; ++i, ++drawn) {
                const int slot = moduleSlots[i];
                const auto &module = sp.envModules[static_cast<size_t>(slot)];
                const QRectF card(x, body.top(), cardW, cardH);
                x += cardW + gapCard;
                const bool activeCard = (slot == m_activeEnvSlot);
                p.setBrush(activeCard ? Theme::withAlpha(Theme::accent(), 42) : Theme::bg2());
                p.setPen(QPen(activeCard ? Theme::accent() : Theme::stroke(), activeCard ? 1.6 : 1.0));
                p.drawRoundedRect(card, Theme::px(12), Theme::px(12));
                pushEditorHit(card, "env-select", slot);

                QRectF titleRect(card.left() + Theme::pxF(12.0f), card.top() + Theme::pxF(10.0f),
                                 card.width() - Theme::pxF(24.0f), Theme::pxF(20.0f));
                p.setPen(Theme::text());
                p.setFont(Theme::condensedFont(12, QFont::Bold));
                p.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                           QString("ADSR %1").arg(slot + 1));

                QRectF bindRect(card.right() - Theme::pxF(74.0f), card.top() + Theme::pxF(8.0f),
                                Theme::pxF(42.0f), Theme::pxF(20.0f));
                p.setBrush(Theme::bg3());
                p.setPen(QPen(Theme::stroke(), 1.0));
                p.drawRoundedRect(bindRect, Theme::px(6), Theme::px(6));
                p.setPen(Theme::text());
                p.setFont(Theme::baseFont(8, QFont::DemiBold));
                p.drawText(bindRect, Qt::AlignCenter, "BIND");
                pushEditorHit(bindRect, "env-bind", slot);

                QRectF closeRect(card.right() - Theme::pxF(24.0f), card.top() + Theme::pxF(8.0f),
                                 Theme::pxF(16.0f), Theme::pxF(20.0f));
                p.setPen(Theme::textMuted());
                p.drawText(closeRect, Qt::AlignCenter, "x");
                pushEditorHit(closeRect, "env-remove", slot);

                QRectF graph(card.left() + Theme::pxF(18.0f), card.top() + Theme::pxF(54.0f),
                             card.width() - Theme::pxF(36.0f), card.height() * 0.35f);
                p.setBrush(Theme::bg3());
                p.setPen(QPen(Theme::stroke(), 1.0));
                p.drawRoundedRect(graph, Theme::px(10), Theme::px(10));
                const QRectF pathRect = graph.adjusted(Theme::pxF(12.0f), Theme::pxF(12.0f),
                                                       -Theme::pxF(12.0f), -Theme::pxF(12.0f));
                const float a = clamp01(module.attack);
                const float d = clamp01(module.decay);
                const float s = clamp01(module.sustain);
                const float r = clamp01(module.release);
                const float sum = std::max(0.2f, a + d + r + 0.15f);
                const float ax = pathRect.left() + pathRect.width() * (a / sum);
                const float dx = ax + pathRect.width() * (d / sum);
                const float rx = pathRect.right() - pathRect.width() * (r / sum);
                QPainterPath envPath;
                envPath.moveTo(pathRect.left(), pathRect.bottom());
                envPath.lineTo(ax, pathRect.top());
                envPath.lineTo(dx, pathRect.top() + (1.0f - s) * pathRect.height());
                envPath.lineTo(rx, pathRect.top() + (1.0f - s) * pathRect.height());
                envPath.lineTo(pathRect.right(), pathRect.bottom());
                p.setPen(QPen(Theme::accent(), Theme::pxF(1.8f)));
                p.drawPath(envPath);

                const float rowY = graph.bottom() + Theme::pxF(14.0f);
                drawBox(QRectF(card.left() + Theme::pxF(14.0f), rowY, Theme::pxF(72.0f), Theme::pxF(48.0f)),
                        "A", QString("%1ms").arg(qRound(module.attack * 2000.0f)), "env-attack", slot);
                drawBox(QRectF(card.left() + Theme::pxF(92.0f), rowY, Theme::pxF(72.0f), Theme::pxF(48.0f)),
                        "D", QString("%1ms").arg(qRound(module.decay * 2000.0f)), "env-decay", slot);
                drawBox(QRectF(card.left() + Theme::pxF(170.0f), rowY, Theme::pxF(72.0f), Theme::pxF(48.0f)),
                        "S", QString("%1%").arg(qRound(module.sustain * 100.0f)), "env-sustain", slot);
                drawBox(QRectF(card.left() + Theme::pxF(248.0f), rowY, Theme::pxF(72.0f), Theme::pxF(48.0f)),
                        "R", QString("%1ms").arg(qRound(module.release * 2000.0f)), "env-release", slot);

                QRectF assignRect(card.left() + Theme::pxF(14.0f), card.bottom() - Theme::pxF(52.0f),
                                  card.width() - Theme::pxF(28.0f), Theme::pxF(38.0f));
                drawAssignments(assignRect, module.assign);
            }
            if (hasFree && drawn < visibleCount) {
                m_editorAddRect = QRectF(x, body.top(), cardW, cardH);
                p.setBrush(Theme::withAlpha(Theme::bg2(), 160));
                p.setPen(QPen(Theme::stroke(), 1.0, Qt::DashLine));
                p.drawRoundedRect(m_editorAddRect, Theme::px(12), Theme::px(12));
                p.setPen(Theme::accent());
                p.setFont(Theme::condensedFont(28, QFont::Bold));
                p.drawText(m_editorAddRect, Qt::AlignCenter, "+");
                p.setFont(Theme::baseFont(10, QFont::DemiBold));
                p.drawText(m_editorAddRect.adjusted(0, Theme::px(42), 0, 0),
                           Qt::AlignHCenter | Qt::AlignTop, "ADD ADSR");
            }
        } else if (m_editorMode == EditorMode::Filter) {
            const QStringList presets = filterPresetNames();
            QVector<int> moduleSlots;
            for (int i = 0; i < PadBank::kFilterModuleCount; ++i) {
                if (sp.filterModules[static_cast<size_t>(i)].enabled) {
                    moduleSlots.push_back(i);
                }
            }
            bool hasFree = moduleSlots.size() < PadBank::kFilterModuleCount;
            m_filterScroll = qBound(0, m_filterScroll, std::max(0, moduleSlots.size() - visibleCount));
            drawNav(m_editorLeftRect, "<", m_filterScroll > 0);
            drawNav(m_editorRightRect, ">", m_filterScroll + visibleCount < moduleSlots.size());
            float x = m_editorLeftRect.right() + gapCard;
            int drawn = 0;
            for (int i = m_filterScroll; i < moduleSlots.size() && drawn < visibleCount; ++i, ++drawn) {
                const int slot = moduleSlots[i];
                const auto &module = sp.filterModules[static_cast<size_t>(slot)];
                const QRectF card(x, body.top(), cardW, cardH);
                x += cardW + gapCard;
                const bool activeCard = (slot == m_activeFilterSlot);
                p.setBrush(activeCard ? Theme::withAlpha(Theme::warn(), 38) : Theme::bg2());
                p.setPen(QPen(activeCard ? Theme::warn() : Theme::stroke(), activeCard ? 1.6 : 1.0));
                p.drawRoundedRect(card, Theme::px(12), Theme::px(12));
                pushEditorHit(card, "filter-select", slot);

                QRectF titleRect(card.left() + Theme::pxF(12.0f), card.top() + Theme::pxF(10.0f),
                                 card.width() - Theme::pxF(24.0f), Theme::pxF(20.0f));
                p.setPen(Theme::text());
                p.setFont(Theme::condensedFont(12, QFont::Bold));
                p.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                           QString("FILTER %1").arg(slot + 1));
                QRectF closeRect(card.right() - Theme::pxF(24.0f), card.top() + Theme::pxF(8.0f),
                                 Theme::pxF(16.0f), Theme::pxF(20.0f));
                p.setPen(Theme::textMuted());
                p.drawText(closeRect, Qt::AlignCenter, "x");
                pushEditorHit(closeRect, "filter-remove", slot);

                QRectF presetRect(card.left() + Theme::pxF(14.0f), card.top() + Theme::pxF(42.0f),
                                  card.width() - Theme::pxF(28.0f), Theme::pxF(44.0f));
                drawBox(presetRect, "PRESET", presets.value(module.preset, presets.first()),
                        "filter-preset", slot, activeCard);

                QRectF graph(card.left() + Theme::pxF(18.0f), presetRect.bottom() + Theme::pxF(10.0f),
                             card.width() - Theme::pxF(36.0f), card.height() * 0.32f);
                p.setBrush(Theme::bg3());
                p.setPen(QPen(Theme::stroke(), 1.0));
                p.drawRoundedRect(graph, Theme::px(10), Theme::px(10));
                const QRectF g = graph.adjusted(Theme::pxF(10.0f), Theme::pxF(10.0f),
                                                -Theme::pxF(10.0f), -Theme::pxF(10.0f));
                QPainterPath response;
                response.moveTo(g.left(), g.center().y());
                response.lineTo(g.left() + g.width() * module.lowCut, g.center().y());
                const float lowX = g.left() + g.width() * module.lowCut;
                const float highX = g.left() + g.width() * module.highCut;
                response.lineTo(lowX, g.bottom());
                response.lineTo(highX, g.top());
                response.lineTo(g.right(), g.top());
                p.setPen(QPen(Theme::accent(), Theme::pxF(1.6f)));
                p.drawPath(response);

                const float rowY = graph.bottom() + Theme::pxF(12.0f);
                drawBox(QRectF(card.left() + Theme::pxF(14.0f), rowY, Theme::pxF(96.0f), Theme::pxF(48.0f)),
                        "LOW", QString("%1%").arg(qRound(module.lowCut * 100.0f)), "filter-low", slot);
                drawBox(QRectF(card.left() + Theme::pxF(120.0f), rowY, Theme::pxF(96.0f), Theme::pxF(48.0f)),
                        "HIGH", QString("%1%").arg(qRound(module.highCut * 100.0f)), "filter-high", slot);
                drawBox(QRectF(card.left() + Theme::pxF(226.0f), rowY, Theme::pxF(96.0f), Theme::pxF(48.0f)),
                        "RESO", QString("%1%").arg(qRound(module.resonance * 100.0f)), "filter-reso", slot);
                drawBox(QRectF(card.left() + Theme::pxF(14.0f), rowY + Theme::pxF(58.0f), Theme::pxF(96.0f), Theme::pxF(48.0f)),
                        "SLOPE", QString("%1%").arg(qRound(module.slope * 100.0f)), "filter-slope", slot);
                drawBox(QRectF(card.left() + Theme::pxF(120.0f), rowY + Theme::pxF(58.0f), Theme::pxF(96.0f), Theme::pxF(48.0f)),
                        "DRIVE", QString("%1%").arg(qRound(module.drive * 100.0f)), "filter-drive", slot);
                drawBox(QRectF(card.left() + Theme::pxF(226.0f), rowY + Theme::pxF(58.0f), Theme::pxF(96.0f), Theme::pxF(48.0f)),
                        "MIX", QString("%1%").arg(qRound(module.mix * 100.0f)), "filter-mix", slot);
            }
            if (hasFree && drawn < visibleCount) {
                m_editorAddRect = QRectF(x, body.top(), cardW, cardH);
                p.setBrush(Theme::withAlpha(Theme::bg2(), 160));
                p.setPen(QPen(Theme::stroke(), 1.0, Qt::DashLine));
                p.drawRoundedRect(m_editorAddRect, Theme::px(12), Theme::px(12));
                p.setPen(Theme::accent());
                p.setFont(Theme::condensedFont(28, QFont::Bold));
                p.drawText(m_editorAddRect, Qt::AlignCenter, "+");
                p.setFont(Theme::baseFont(10, QFont::DemiBold));
                p.drawText(m_editorAddRect.adjusted(0, Theme::px(42), 0, 0),
                           Qt::AlignHCenter | Qt::AlignTop, "ADD FILTER");
            }
        }
        p.restore();
    }
}

float SynthPageWidget::currentEditValue(const EditParam &param) const {
    if (!m_pads) {
        return 0.0f;
    }
    const PadBank::SynthParams sp = m_pads->synthParams(m_activePad);
    const QString type = synthTypeFromId(synthIdOrDefault(m_pads, m_activePad)).trimmed().toUpper();
    if (isCustomEngineType(type) &&
        (param.type >= EditCustom1 && param.type <= EditCustom4)) {
        const QVector<CustomControl> controls = customControlsForType(type);
        for (const auto &ctrl : controls) {
            if (ctrl.paramType == param.type) {
                switch (ctrl.target) {
                    case CustomTarget::Osc1Wave:
                        return static_cast<float>(sp.osc1Wave);
                    case CustomTarget::Osc2Wave:
                        return static_cast<float>(sp.osc2Wave);
                    case CustomTarget::FmAmount:
                        return sp.fmAmount;
                    case CustomTarget::Ratio:
                        return sp.ratio;
                    case CustomTarget::Feedback:
                        return sp.feedback;
                    case CustomTarget::Osc1Detune:
                        return sp.osc1Detune;
                    case CustomTarget::Osc2Detune:
                        return sp.osc2Detune;
                    case CustomTarget::Osc2Gain:
                        return sp.osc2Gain;
                    case CustomTarget::FilterEnv:
                        return sp.filterEnv;
                }
            }
        }
    }
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
        case EditOctave:
            return static_cast<float>(sp.octave);
        case EditCutoff:
            return sp.cutoff;
        case EditResonance:
            return sp.resonance;
        case EditFilterType:
            return static_cast<float>(sp.filterType);
        case EditFilterEnv:
            return sp.filterEnv;
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
        case EditLfoShape:
            return static_cast<float>(sp.lfoShape);
        case EditLfoSync:
            return static_cast<float>(sp.lfoSyncIndex);
        case EditLfoTarget:
            return static_cast<float>(sp.lfoTarget);
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
    const bool isSimple = isSimpleType(type);
    const bool isCustom = isCustomEngineType(type);
    const bool isModParam = (param.type == EditCutoff || param.type == EditResonance ||
                             param.type == EditFilterType || param.type == EditFilterEnv ||
                             param.type == EditAttack || param.type == EditDecay ||
                             param.type == EditSustain || param.type == EditRelease ||
                             param.type == EditLfoRate || param.type == EditLfoDepth ||
                             param.type == EditLfoShape || param.type == EditLfoSync ||
                             param.type == EditLfoTarget);
    if (isDx7 && !(param.type == EditAttack || param.type == EditDecay ||
                   param.type == EditSustain || param.type == EditRelease)) {
        return;
    }
    if (isCustom) {
        if (!(param.type >= EditCustom1 && param.type <= EditCustom4) && !isModParam) {
            return;
        }
    } else if (isSimple && !(param.type == EditOsc1Wave || param.type == EditOctave ||
                     param.type == EditOsc1Voices || param.type == EditOsc1Detune ||
                     param.type == EditOsc1Gain || param.type == EditOsc2Wave ||
                     param.type == EditOsc2Voices || param.type == EditOsc2Detune ||
                     param.type == EditOsc2Gain || isModParam)) {
        return;
    }
    const int waveCount = PadBank::serumWaves().size();
    const int filterCount = 10;

    auto step = [delta](float base, float amount) {
        return base + amount * static_cast<float>(delta);
    };

    if (isCustom) {
        const QVector<CustomControl> controls = customControlsForType(type);
        CustomTarget target = CustomTarget::FmAmount;
        bool found = false;
        for (const auto &ctrl : controls) {
            if (ctrl.paramType == param.type) {
                target = ctrl.target;
                found = true;
                break;
            }
        }
        if (!found) {
            return;
        }
        switch (target) {
            case CustomTarget::Osc1Wave:
                sp.osc1Wave = (sp.osc1Wave + delta + waveCount) % qMax(1, waveCount);
                m_pads->setSynthOsc(pad, 0, sp.osc1Wave, sp.osc1Voices, sp.osc1Detune,
                                    sp.osc1Gain, sp.osc1Pan);
                break;
            case CustomTarget::Osc2Wave:
                sp.osc2Wave = (sp.osc2Wave + delta + waveCount) % qMax(1, waveCount);
                m_pads->setSynthOsc(pad, 1, sp.osc2Wave, sp.osc2Voices, sp.osc2Detune,
                                    sp.osc2Gain, sp.osc2Pan);
                break;
            case CustomTarget::FmAmount:
                sp.fmAmount = clamp01(step(sp.fmAmount, 0.05f));
                m_pads->setSynthFm(pad, sp.fmAmount, sp.ratio, sp.feedback);
                break;
            case CustomTarget::Ratio:
                sp.ratio = qBound(0.1f, sp.ratio + delta * 0.1f, 8.0f);
                m_pads->setSynthFm(pad, sp.fmAmount, sp.ratio, sp.feedback);
                break;
            case CustomTarget::Feedback:
                sp.feedback = clamp01(step(sp.feedback, 0.05f));
                m_pads->setSynthFm(pad, sp.fmAmount, sp.ratio, sp.feedback);
                break;
            case CustomTarget::Osc1Detune:
                sp.osc1Detune = clamp01(step(sp.osc1Detune, 0.05f));
                m_pads->setSynthOsc(pad, 0, sp.osc1Wave, sp.osc1Voices, sp.osc1Detune,
                                    sp.osc1Gain, sp.osc1Pan);
                break;
            case CustomTarget::Osc2Detune:
                sp.osc2Detune = clamp01(step(sp.osc2Detune, 0.05f));
                m_pads->setSynthOsc(pad, 1, sp.osc2Wave, sp.osc2Voices, sp.osc2Detune,
                                    sp.osc2Gain, sp.osc2Pan);
                break;
            case CustomTarget::Osc2Gain:
                sp.osc2Gain = clamp01(step(sp.osc2Gain, 0.05f));
                m_pads->setSynthOsc(pad, 1, sp.osc2Wave, sp.osc2Voices, sp.osc2Detune,
                                    sp.osc2Gain, sp.osc2Pan);
                break;
            case CustomTarget::FilterEnv:
                sp.filterEnv = clamp01(step(sp.filterEnv, 0.05f));
                m_pads->setSynthFilterEnv(pad, sp.filterEnv);
                break;
        }
        update();
        return;
    }

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
        case EditOctave:
            sp.octave = qBound(-2, sp.octave + delta, 2);
            m_pads->setSynthOctave(pad, sp.octave);
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
        case EditFilterEnv:
            sp.filterEnv = clamp01(step(sp.filterEnv, 0.05f));
            m_pads->setSynthFilterEnv(pad, sp.filterEnv);
            break;
        case EditAttack:
            sp.attack = clamp01(step(sp.attack, isSimple ? 0.01f : 0.02f));
            m_pads->setSynthAdsr(pad, sp.attack, sp.decay, sp.sustain, sp.release);
            break;
        case EditDecay:
            sp.decay = clamp01(step(sp.decay, isSimple ? 0.01f : 0.02f));
            m_pads->setSynthAdsr(pad, sp.attack, sp.decay, sp.sustain, sp.release);
            break;
        case EditSustain:
            sp.sustain = clamp01(step(sp.sustain, isSimple ? 0.01f : 0.02f));
            m_pads->setSynthAdsr(pad, sp.attack, sp.decay, sp.sustain, sp.release);
            break;
        case EditRelease:
            sp.release = clamp01(step(sp.release, isSimple ? 0.01f : 0.02f));
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
        case EditLfoShape:
            sp.lfoShape = (sp.lfoShape + delta + 5) % 5;
            m_pads->setSynthLfoShape(pad, sp.lfoShape);
            break;
        case EditLfoSync: {
            const int syncCount = 8;
            int slot = sp.lfoSync > 0 ? sp.lfoSyncIndex + 1 : 0;
            slot = qBound(0, slot + delta, syncCount);
            if (slot <= 0) {
                sp.lfoSync = 0;
                sp.lfoSyncIndex = 0;
            } else {
                sp.lfoSync = 1;
                sp.lfoSyncIndex = slot - 1;
            }
            m_pads->setSynthLfoSync(pad, sp.lfoSync, sp.lfoSyncIndex);
            break;
        }
        case EditLfoTarget:
            sp.lfoTarget = (sp.lfoTarget + delta + 2) % 2;
            m_pads->setSynthLfoTarget(pad, sp.lfoTarget);
            break;
        default:
            break;
    }
    update();
}

int SynthPageWidget::modTargetForParam(int paramType, const QString &synthType) const {
    Q_UNUSED(synthType);
    using MT = PadBank::ModTarget;
    switch (paramType) {
        case EditOsc1Detune:
            return static_cast<int>(MT::Osc1Detune);
        case EditOsc1Gain:
            return static_cast<int>(MT::Osc1Gain);
        case EditOsc1Pan:
            return static_cast<int>(MT::Osc1Pan);
        case EditOsc2Detune:
            return static_cast<int>(MT::Osc2Detune);
        case EditOsc2Gain:
            return static_cast<int>(MT::Osc2Gain);
        case EditOsc2Pan:
            return static_cast<int>(MT::Osc2Pan);
        case EditCutoff:
            return static_cast<int>(MT::Cutoff);
        case EditResonance:
            return static_cast<int>(MT::Resonance);
        case EditFilterEnv:
            return static_cast<int>(MT::FilterEnv);
        case EditCustom1:
            return static_cast<int>(MT::Custom1);
        case EditCustom2:
            return static_cast<int>(MT::Custom2);
        case EditCustom3:
            return static_cast<int>(MT::Custom3);
        case EditCustom4:
            return static_cast<int>(MT::Custom4);
        default:
            break;
    }
    return static_cast<int>(MT::None);
}
