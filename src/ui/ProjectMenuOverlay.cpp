#include "ProjectMenuOverlay.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QSaveFile>
#include <QtGlobal>

#include "PadBank.h"
#include "Theme.h"
#include "ui/FxPageWidget.h"
#include "ui/SeqPageWidget.h"

namespace {
QString makeTimestampName(const QString &prefix) {
    const QDateTime now = QDateTime::currentDateTime();
    return QString("%1_%2").arg(prefix, now.toString("yyyyMMdd_HHmmss"));
}

QJsonObject padParamsToJson(const PadBank::PadParams &p) {
    QJsonObject obj;
    obj["volume"] = p.volume;
    obj["pan"] = p.pan;
    obj["pitch"] = p.pitch;
    obj["stretch"] = p.stretchIndex;
    obj["stretchMode"] = p.stretchMode;
    obj["start"] = p.start;
    obj["end"] = p.end;
    obj["sliceCount"] = p.sliceCountIndex;
    obj["sliceIndex"] = p.sliceIndex;
    obj["loop"] = p.loop;
    obj["fxBus"] = p.fxBus;
    obj["normalize"] = p.normalize;
    return obj;
}

PadBank::PadParams padParamsFromJson(const QJsonObject &obj) {
    PadBank::PadParams p;
    p.volume = static_cast<float>(obj.value("volume").toDouble(p.volume));
    p.pan = static_cast<float>(obj.value("pan").toDouble(p.pan));
    p.pitch = static_cast<float>(obj.value("pitch").toDouble(p.pitch));
    p.stretchIndex = obj.value("stretch").toInt(p.stretchIndex);
    p.stretchMode = obj.value("stretchMode").toInt(p.stretchMode);
    p.start = static_cast<float>(obj.value("start").toDouble(p.start));
    p.end = static_cast<float>(obj.value("end").toDouble(p.end));
    p.sliceCountIndex = obj.value("sliceCount").toInt(p.sliceCountIndex);
    p.sliceIndex = obj.value("sliceIndex").toInt(p.sliceIndex);
    p.loop = obj.value("loop").toBool(p.loop);
    p.fxBus = obj.value("fxBus").toInt(p.fxBus);
    p.normalize = obj.value("normalize").toBool(p.normalize);
    return p;
}

QJsonObject synthParamsToJson(const PadBank::SynthParams &s) {
    QJsonObject obj;
    obj["attack"] = s.attack;
    obj["decay"] = s.decay;
    obj["sustain"] = s.sustain;
    obj["release"] = s.release;
    obj["wave"] = s.wave;
    obj["voices"] = s.voices;
    obj["detune"] = s.detune;
    obj["octave"] = s.octave;
    obj["fmAmount"] = s.fmAmount;
    obj["ratio"] = s.ratio;
    obj["feedback"] = s.feedback;
    obj["cutoff"] = s.cutoff;
    obj["resonance"] = s.resonance;
    obj["filterType"] = s.filterType;
    obj["lfoRate"] = s.lfoRate;
    obj["lfoDepth"] = s.lfoDepth;
    obj["osc1Wave"] = s.osc1Wave;
    obj["osc2Wave"] = s.osc2Wave;
    obj["osc1Voices"] = s.osc1Voices;
    obj["osc2Voices"] = s.osc2Voices;
    obj["osc1Detune"] = s.osc1Detune;
    obj["osc2Detune"] = s.osc2Detune;
    obj["osc1Gain"] = s.osc1Gain;
    obj["osc2Gain"] = s.osc2Gain;
    obj["osc1Pan"] = s.osc1Pan;
    obj["osc2Pan"] = s.osc2Pan;
    QJsonArray macros;
    for (float m : s.macros) {
        macros.append(m);
    }
    obj["macros"] = macros;
    return obj;
}

PadBank::SynthParams synthParamsFromJson(const QJsonObject &obj) {
    PadBank::SynthParams s;
    s.attack = static_cast<float>(obj.value("attack").toDouble(s.attack));
    s.decay = static_cast<float>(obj.value("decay").toDouble(s.decay));
    s.sustain = static_cast<float>(obj.value("sustain").toDouble(s.sustain));
    s.release = static_cast<float>(obj.value("release").toDouble(s.release));
    s.wave = obj.value("wave").toInt(s.wave);
    s.voices = obj.value("voices").toInt(s.voices);
    s.detune = static_cast<float>(obj.value("detune").toDouble(s.detune));
    s.octave = obj.value("octave").toInt(s.octave);
    s.fmAmount = static_cast<float>(obj.value("fmAmount").toDouble(s.fmAmount));
    s.ratio = static_cast<float>(obj.value("ratio").toDouble(s.ratio));
    s.feedback = static_cast<float>(obj.value("feedback").toDouble(s.feedback));
    s.cutoff = static_cast<float>(obj.value("cutoff").toDouble(s.cutoff));
    s.resonance = static_cast<float>(obj.value("resonance").toDouble(s.resonance));
    s.filterType = obj.value("filterType").toInt(s.filterType);
    s.lfoRate = static_cast<float>(obj.value("lfoRate").toDouble(s.lfoRate));
    s.lfoDepth = static_cast<float>(obj.value("lfoDepth").toDouble(s.lfoDepth));
    s.osc1Wave = obj.value("osc1Wave").toInt(s.osc1Wave);
    s.osc2Wave = obj.value("osc2Wave").toInt(s.osc2Wave);
    s.osc1Voices = obj.value("osc1Voices").toInt(s.osc1Voices);
    s.osc2Voices = obj.value("osc2Voices").toInt(s.osc2Voices);
    s.osc1Detune = static_cast<float>(obj.value("osc1Detune").toDouble(s.osc1Detune));
    s.osc2Detune = static_cast<float>(obj.value("osc2Detune").toDouble(s.osc2Detune));
    s.osc1Gain = static_cast<float>(obj.value("osc1Gain").toDouble(s.osc1Gain));
    s.osc2Gain = static_cast<float>(obj.value("osc2Gain").toDouble(s.osc2Gain));
    s.osc1Pan = static_cast<float>(obj.value("osc1Pan").toDouble(s.osc1Pan));
    s.osc2Pan = static_cast<float>(obj.value("osc2Pan").toDouble(s.osc2Pan));
    const QJsonArray macros = obj.value("macros").toArray();
    for (int i = 0; i < macros.size() && i < 8; ++i) {
        s.macros[static_cast<size_t>(i)] = static_cast<float>(macros[i].toDouble(0.5));
    }
    return s;
}
}  // namespace

ProjectMenuOverlay::ProjectMenuOverlay(PadBank *pads, SeqPageWidget *seq, FxPageWidget *fx,
                                       QWidget *parent)
    : QWidget(parent), m_pads(pads), m_seq(seq), m_fx(fx) {
    setAutoFillBackground(false);
    setAttribute(Qt::WA_StyledBackground, true);
    setVisible(false);
    setFocusPolicy(Qt::StrongFocus);
}

void ProjectMenuOverlay::showMenu() {
    if (parentWidget()) {
        setGeometry(parentWidget()->rect());
    }
    ensureMediaDirs();
    refreshProjects();
    if (m_pads) {
        m_metronome = m_seq ? m_seq->metronomeEnabled() : false;
    }
    setVisible(true);
    raise();
    setFocus(Qt::OtherFocusReason);
    update();
}

QString ProjectMenuOverlay::mediaRoot() const {
    const QString env = qEnvironmentVariable("GROOVEBOX_MEDIA_ROOT");
    if (!env.isEmpty()) {
        return env;
    }

#ifdef Q_OS_LINUX
    const QStringList roots = {QStringLiteral("/media"),
                               QStringLiteral("/run/media"),
                               QStringLiteral("/mnt"),
                               QDir::home().filePath("media")};
    for (const QString &root : roots) {
        QDir base(root);
        if (!base.exists()) {
            continue;
        }
        const QFileInfoList entries =
            base.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &info : entries) {
            if (!info.isDir()) {
                continue;
            }
            if (info.isWritable()) {
                return info.absoluteFilePath();
            }
            QDir sub(info.absoluteFilePath());
            const QFileInfoList nested =
                sub.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            for (const QFileInfo &nest : nested) {
                if (nest.isDir() && nest.isWritable()) {
                    return nest.absoluteFilePath();
                }
            }
        }
    }
#endif

    return QDir::home().filePath("bloop_media");
}

QString ProjectMenuOverlay::projectDir() const {
    return QDir(mediaRoot()).filePath("PROJECT");
}

QString ProjectMenuOverlay::renderDir() const {
    return QDir(mediaRoot()).filePath("RENDER");
}

void ProjectMenuOverlay::ensureMediaDirs() {
    QDir root(mediaRoot());
    if (!root.exists()) {
        root.mkpath(".");
    }
    root.mkpath("PROJECT");
    root.mkpath("RENDER");
}

void ProjectMenuOverlay::refreshProjects() {
    m_projectNames.clear();
    QDir dir(projectDir());
    if (!dir.exists()) {
        return;
    }
    const QStringList files =
        dir.entryList(QStringList() << "*.bloop", QDir::Files, QDir::Name);
    for (const QString &file : files) {
        QString base = QFileInfo(file).completeBaseName();
        if (!base.isEmpty()) {
            m_projectNames << base;
        }
    }
    if (!m_projectNames.isEmpty()) {
        if (m_selectedProject < 0 || m_selectedProject >= m_projectNames.size()) {
            m_selectedProject = 0;
        }
    } else {
        m_selectedProject = -1;
    }
}

void ProjectMenuOverlay::newProject() {
    if (!m_pads) {
        return;
    }
    for (int pad = 0; pad < 8; ++pad) {
        m_pads->setPadPath(pad, QString());
        m_pads->setVolume(pad, 1.0f);
        m_pads->setPan(pad, 0.0f);
        m_pads->setPitch(pad, 0.0f);
        m_pads->setStretchIndex(pad, 0);
        m_pads->setStart(pad, 0.0f);
        m_pads->setEnd(pad, 1.0f);
        m_pads->setSliceCountIndex(pad, 0);
        m_pads->setSliceIndex(pad, 0);
        m_pads->setLoop(pad, false);
        m_pads->setNormalize(pad, false);
        m_pads->setFxBus(pad, 0);
        if (m_seq) {
            m_seq->applyPianoSteps(pad, QVector<int>());
            m_seq->applyPianoNotes(pad, QVector<int>());
        }
    }
    if (m_pads) {
        m_pads->setBpm(120);
    }
    if (m_seq) {
        m_metronome = false;
        m_seq->setMetronomeEnabled(false);
    }
    if (m_fx) {
        QVector<FxTrack> tracks;
        tracks.push_back({"MASTER", QVector<FxInsert>(4)});
        tracks.push_back({"A", QVector<FxInsert>(4)});
        tracks.push_back({"B", QVector<FxInsert>(4)});
        tracks.push_back({"C", QVector<FxInsert>(4)});
        tracks.push_back({"D", QVector<FxInsert>(4)});
        tracks.push_back({"E", QVector<FxInsert>(4)});
        m_fx->setTrackData(tracks);
    }
    for (int bus = 0; bus < 6; ++bus) {
        if (m_pads) {
            m_pads->setBusGain(bus, 1.0f);
        }
    }
    m_selectedProject = -1;
    update();
}

bool ProjectMenuOverlay::saveProject(const QString &name) {
    if (!m_pads || !m_seq || !m_fx) {
        return false;
    }
    ensureMediaDirs();
    QString baseName = name.trimmed();
    if (baseName.isEmpty()) {
        baseName = makeTimestampName("project");
    }
    const QString filePath = QDir(projectDir()).filePath(baseName + ".bloop");

    QJsonObject root;
    root["version"] = 3;
    root["bpm"] = m_pads->bpm();
    root["metronome"] = m_seq->metronomeEnabled();
    root["renderBars"] = m_renderBars;
    root["renderRate"] = m_renderRate;

    QJsonArray busGains;
    for (int bus = 0; bus < 6; ++bus) {
        busGains.append(m_pads->busGain(bus));
    }
    root["busGain"] = busGains;

    QJsonArray pads;
    for (int pad = 0; pad < 8; ++pad) {
        QJsonObject p;
        p["isSynth"] = m_pads->isSynth(pad);
        p["path"] = m_pads->padPath(pad);
        p["synthId"] = m_pads->synthId(pad);
        p["params"] = padParamsToJson(m_pads->params(pad));
        p["synthParams"] = synthParamsToJson(m_pads->synthParams(pad));
        QJsonArray steps;
        for (int step : m_seq->pianoSteps(pad)) {
            steps.append(step);
        }
        p["steps"] = steps;
        QJsonArray notes;
        for (int v : m_seq->pianoNotesData(pad)) {
            notes.append(v);
        }
        p["notes"] = notes;
        pads.append(p);
    }
    root["pads"] = pads;

    QJsonArray tracks;
    for (const FxTrack &track : m_fx->trackData()) {
        QJsonObject t;
        t["name"] = track.name;
        QJsonArray inserts;
        for (const FxInsert &ins : track.inserts) {
            QJsonObject obj;
            obj["effect"] = ins.effect;
            obj["p1"] = ins.p1;
            obj["p2"] = ins.p2;
            obj["p3"] = ins.p3;
            obj["p4"] = ins.p4;
            obj["p5"] = ins.p5;
            inserts.append(obj);
        }
        t["inserts"] = inserts;
        tracks.append(t);
    }
    root["fx"] = tracks;

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Compact);
    file.write("BLOOP3\n");
    file.write(json);
    const bool ok = file.commit();
    if (ok) {
        refreshProjects();
        const int idx = m_projectNames.indexOf(baseName);
        if (idx >= 0) {
            m_selectedProject = idx;
        }
    }
    return ok;
}

bool ProjectMenuOverlay::loadProject(const QString &name) {
    if (!m_pads || !m_seq || !m_fx) {
        return false;
    }
    ensureMediaDirs();
    if (name.trimmed().isEmpty()) {
        return false;
    }
    const QString filePath = QDir(projectDir()).filePath(name + ".bloop");
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    QByteArray data = file.readAll();
    file.close();
    int newline = data.indexOf('\n');
    if (newline >= 0) {
        data = data.mid(newline + 1);
    }
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (doc.isNull() || !doc.isObject()) {
        return false;
    }
    QJsonObject root = doc.object();

    m_pads->setBpm(root.value("bpm").toInt(m_pads->bpm()));
    m_metronome = root.value("metronome").toBool(false);
    m_seq->setMetronomeEnabled(m_metronome);
    m_renderBars = root.value("renderBars").toInt(m_renderBars);
    m_renderRate = root.value("renderRate").toInt(m_renderRate);

    const QJsonArray busGains = root.value("busGain").toArray();
    for (int bus = 0; bus < busGains.size() && bus < 6; ++bus) {
        m_pads->setBusGain(bus, static_cast<float>(busGains[bus].toDouble(1.0)));
    }

    const QJsonArray pads = root.value("pads").toArray();
    for (int pad = 0; pad < pads.size() && pad < 8; ++pad) {
        const QJsonObject obj = pads[pad].toObject();
        const bool isSynth = obj.value("isSynth").toBool(false);
        const QString path = obj.value("path").toString();
        const QString synthId = obj.value("synthId").toString();

        if (isSynth) {
            m_pads->setSynth(pad, synthId);
        } else {
            m_pads->setPadPath(pad, path);
        }

        const PadBank::PadParams pp = padParamsFromJson(obj.value("params").toObject());
        m_pads->setVolume(pad, pp.volume);
        m_pads->setPan(pad, pp.pan);
        m_pads->setPitch(pad, pp.pitch);
        m_pads->setStretchIndex(pad, pp.stretchIndex);
        m_pads->setStretchMode(pad, pp.stretchMode);
        m_pads->setStart(pad, pp.start);
        m_pads->setEnd(pad, pp.end);
        m_pads->setSliceCountIndex(pad, pp.sliceCountIndex);
        m_pads->setSliceIndex(pad, pp.sliceIndex);
        m_pads->setLoop(pad, pp.loop);
        m_pads->setNormalize(pad, pp.normalize);
        m_pads->setFxBus(pad, pp.fxBus);

        if (isSynth) {
            const PadBank::SynthParams sp =
                synthParamsFromJson(obj.value("synthParams").toObject());
            m_pads->setSynthAdsr(pad, sp.attack, sp.decay, sp.sustain, sp.release);
            m_pads->setSynthWave(pad, sp.wave);
            m_pads->setSynthVoices(pad, sp.voices);
            m_pads->setSynthDetune(pad, sp.detune);
            m_pads->setSynthOctave(pad, sp.octave);
            m_pads->setSynthFm(pad, sp.fmAmount, sp.ratio, sp.feedback);
            m_pads->setSynthFilter(pad, sp.cutoff, sp.resonance);
            m_pads->setSynthFilterType(pad, sp.filterType);
            m_pads->setSynthLfo(pad, sp.lfoRate, sp.lfoDepth);
            m_pads->setSynthOsc(pad, 0, sp.osc1Wave, sp.osc1Voices, sp.osc1Detune,
                                sp.osc1Gain, sp.osc1Pan);
            m_pads->setSynthOsc(pad, 1, sp.osc2Wave, sp.osc2Voices, sp.osc2Detune,
                                sp.osc2Gain, sp.osc2Pan);
            for (int i = 0; i < 8; ++i) {
                m_pads->setSynthMacro(pad, i, sp.macros[static_cast<size_t>(i)]);
            }
        }

        QVector<int> steps;
        for (const auto &item : obj.value("steps").toArray()) {
            steps.push_back(item.toInt());
        }
        m_seq->applyPianoSteps(pad, steps);
        QVector<int> notes;
        for (const auto &item : obj.value("notes").toArray()) {
            notes.push_back(item.toInt());
        }
        m_seq->applyPianoNotes(pad, notes);
    }

    QVector<FxTrack> tracks;
    const QJsonArray fx = root.value("fx").toArray();
    if (!fx.isEmpty()) {
        for (const auto &item : fx) {
            const QJsonObject obj = item.toObject();
            FxTrack track;
            track.name = obj.value("name").toString();
            const QJsonArray inserts = obj.value("inserts").toArray();
            for (const auto &ins : inserts) {
                const QJsonObject in = ins.toObject();
                FxInsert slot;
                slot.effect = in.value("effect").toString();
                slot.p1 = static_cast<float>(in.value("p1").toDouble(slot.p1));
                slot.p2 = static_cast<float>(in.value("p2").toDouble(slot.p2));
                slot.p3 = static_cast<float>(in.value("p3").toDouble(slot.p3));
                slot.p4 = static_cast<float>(in.value("p4").toDouble(slot.p4));
                slot.p5 = static_cast<float>(in.value("p5").toDouble(slot.p5));
                track.inserts.push_back(slot);
            }
            tracks.push_back(track);
        }
        if (!tracks.isEmpty()) {
            m_fx->setTrackData(tracks);
        }
    }

    update();
    return true;
}

void ProjectMenuOverlay::renderProject() {
    if (!m_seq) {
        return;
    }
    ensureMediaDirs();
    QString filename = makeTimestampName("render") + ".wav";
    const QString path = QDir(renderDir()).filePath(filename);
    m_seq->renderToFile(path, m_renderBars, m_renderRate);
}

void ProjectMenuOverlay::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter p(this);
    Theme::applyRenderHints(p);
    p.setBrush(Theme::withAlpha(Theme::bg0(), 230));
    p.setPen(Qt::NoPen);
    p.drawRect(rect());

    const float margin = Theme::pxF(20.0f);
    m_panelRect = rect().adjusted(margin, margin, -margin, -margin);

    const float gap = Theme::pxF(14.0f);
    const float leftW = m_panelRect.width() * 0.54f;
    m_leftRect = QRectF(m_panelRect.left(), m_panelRect.top(), leftW, m_panelRect.height());
    const float rightW = m_panelRect.width() - leftW - gap;
    const float rightX = m_leftRect.right() + gap;
    const float rightTopH = m_panelRect.height() * 0.56f;
    m_rightTopRect = QRectF(rightX, m_panelRect.top(), rightW, rightTopH);
    m_rightBottomRect =
        QRectF(rightX, m_rightTopRect.bottom() + gap, rightW,
               m_panelRect.bottom() - m_rightTopRect.bottom() - gap);

    auto drawPanel = [&](const QRectF &r, const QString &label) {
        p.setBrush(Theme::bg1());
        p.setPen(QPen(Theme::stroke(), 1.2));
        p.drawRoundedRect(r, Theme::px(12), Theme::px(12));
        p.setPen(Theme::accent());
        p.setFont(Theme::condensedFont(11, QFont::Bold));
        p.drawText(r.adjusted(Theme::px(12), Theme::px(8), -Theme::px(12), 0),
                   Qt::AlignLeft | Qt::AlignTop, label);
    };

    drawPanel(m_leftRect, "SETTINGS");
    drawPanel(m_rightTopRect, "PROJECTS");
    drawPanel(m_rightBottomRect, "RENDER");

    m_closeRect = QRectF(m_panelRect.right() - Theme::px(26),
                         m_panelRect.top() + Theme::px(10),
                         Theme::px(18), Theme::px(18));
    p.setPen(QPen(Theme::text(), 1.8));
    p.drawLine(m_closeRect.topLeft(), m_closeRect.bottomRight());
    p.drawLine(m_closeRect.topRight(), m_closeRect.bottomLeft());

    // Settings panel content.
    float y = m_leftRect.top() + Theme::pxF(36.0f);
    const float rowH = Theme::pxF(46.0f);
    const float rowW = m_leftRect.width() - Theme::pxF(24.0f);
    const float rowX = m_leftRect.left() + Theme::pxF(12.0f);

    auto drawRowBase = [&](const QString &label, const QString &value) {
        QRectF row(rowX, y, rowW, rowH);
        p.setBrush(Theme::bg2());
        p.setPen(QPen(Theme::stroke(), 1.0));
        p.drawRoundedRect(row, Theme::px(10), Theme::px(10));
        p.setPen(Theme::text());
        p.setFont(Theme::baseFont(10, QFont::DemiBold));
        p.drawText(row.adjusted(Theme::px(12), 0, -Theme::px(12), 0),
                   Qt::AlignLeft | Qt::AlignVCenter, label);
        p.setPen(Theme::textMuted());
        p.drawText(row.adjusted(Theme::px(12), 0, -Theme::px(12), 0),
                   Qt::AlignRight | Qt::AlignVCenter, value);
        y += rowH + Theme::pxF(10.0f);
        return row;
    };

    const int bpm = m_pads ? m_pads->bpm() : 120;
    QRectF bpmRow = drawRowBase("BPM", QString::number(bpm));
    const float btnW = Theme::pxF(30.0f);
    m_bpmMinusRect =
        QRectF(bpmRow.right() - Theme::pxF(92.0f), bpmRow.top() + Theme::pxF(8.0f),
               btnW, bpmRow.height() - Theme::pxF(16.0f));
    m_bpmPlusRect =
        QRectF(bpmRow.right() - Theme::pxF(50.0f), bpmRow.top() + Theme::pxF(8.0f),
               btnW, bpmRow.height() - Theme::pxF(16.0f));
    p.setBrush(Theme::bg3());
    p.setPen(QPen(Theme::stroke(), 1.0));
    p.drawRoundedRect(m_bpmMinusRect, Theme::px(6), Theme::px(6));
    p.drawRoundedRect(m_bpmPlusRect, Theme::px(6), Theme::px(6));
    p.setPen(Theme::accent());
    p.drawText(m_bpmMinusRect, Qt::AlignCenter, "-");
    p.drawText(m_bpmPlusRect, Qt::AlignCenter, "+");

    QString metroLabel = m_metronome ? "ON" : "OFF";
    QRectF metroRow = drawRowBase("METRONOME", metroLabel);
    m_metronomeRect = metroRow;

    QString rateLabel = (m_renderRate == 48000) ? "48 kHz" : "44.1 kHz";
    QRectF rateRow = drawRowBase("RENDER QUALITY", rateLabel);
    m_rateRect = rateRow;

    const QString media = mediaRoot();
    drawRowBase("MEDIA", QFontMetrics(Theme::baseFont(9, QFont::DemiBold))
                             .elidedText(media, Qt::ElideLeft, rowW - Theme::px(24)));

    // Project list + buttons.
    m_projectRowRects.clear();
    const QRectF projectList =
        QRectF(m_rightTopRect.left() + Theme::px(12),
               m_rightTopRect.top() + Theme::px(36),
               m_rightTopRect.width() - Theme::px(24),
               m_rightTopRect.height() - Theme::px(86));
    p.setBrush(Theme::bg2());
    p.setPen(QPen(Theme::stroke(), 1.0));
    p.drawRoundedRect(projectList, Theme::px(10), Theme::px(10));

    p.setPen(Theme::textMuted());
    p.setFont(Theme::baseFont(8, QFont::DemiBold));
    p.drawText(projectList.adjusted(Theme::px(8), Theme::px(6), -Theme::px(8), -Theme::px(6)),
               Qt::AlignLeft | Qt::AlignTop, "PROJECT FILES");

    const float projectRowH = Theme::pxF(24.0f);
    float py = projectList.top() + Theme::pxF(24.0f);
    for (int i = 0; i < m_projectNames.size(); ++i) {
        QRectF row(projectList.left() + Theme::px(8), py,
                   projectList.width() - Theme::px(16), projectRowH);
        m_projectRowRects.push_back(row);
        const bool selected = (i == m_selectedProject);
        p.setBrush(selected ? Theme::accentAlt() : Theme::bg3());
        p.setPen(QPen(Theme::stroke(), 1.0));
        p.drawRoundedRect(row, Theme::px(6), Theme::px(6));
        p.setPen(selected ? Theme::bg0() : Theme::text());
        p.setFont(Theme::baseFont(9, QFont::DemiBold));
        p.drawText(row.adjusted(Theme::px(8), 0, -Theme::px(8), 0),
                   Qt::AlignLeft | Qt::AlignVCenter, m_projectNames[i]);
        py += projectRowH + Theme::pxF(6.0f);
        if (py > projectList.bottom() - Theme::pxF(10.0f)) {
            break;
        }
    }
    if (m_projectNames.isEmpty()) {
        p.setPen(Theme::textMuted());
        p.setFont(Theme::baseFont(9));
        p.drawText(projectList, Qt::AlignCenter, "NO PROJECTS");
    }

    const float btnY = m_rightTopRect.bottom() - Theme::pxF(40.0f);
    const float projectBtnW = (m_rightTopRect.width() - Theme::pxF(36.0f)) / 3.0f;
    m_newRect = QRectF(m_rightTopRect.left() + Theme::pxF(12.0f), btnY, projectBtnW,
                       Theme::pxF(30.0f));
    m_saveRect = QRectF(m_newRect.right() + Theme::pxF(6.0f), btnY, projectBtnW,
                        Theme::pxF(30.0f));
    m_loadRect = QRectF(m_saveRect.right() + Theme::pxF(6.0f), btnY, projectBtnW,
                        Theme::pxF(30.0f));

    auto drawButton = [&](const QRectF &r, const QString &label, const QColor &color) {
        p.setBrush(color);
        p.setPen(QPen(Theme::stroke(), 1.0));
        p.drawRoundedRect(r, Theme::px(8), Theme::px(8));
        p.setPen(Theme::bg0());
        p.setFont(Theme::condensedFont(10, QFont::Bold));
        p.drawText(r, Qt::AlignCenter, label);
    };
    drawButton(m_newRect, "NEW", Theme::accentAlt());
    drawButton(m_saveRect, "SAVE", Theme::accent());
    drawButton(m_loadRect, "LOAD", Theme::bg3());

    // Render panel.
    const QRectF renderBox =
        QRectF(m_rightBottomRect.left() + Theme::px(12),
               m_rightBottomRect.top() + Theme::px(36),
               m_rightBottomRect.width() - Theme::px(24),
               m_rightBottomRect.height() - Theme::px(56));
    p.setBrush(Theme::bg2());
    p.setPen(QPen(Theme::stroke(), 1.0));
    p.drawRoundedRect(renderBox, Theme::px(10), Theme::px(10));

    p.setPen(Theme::text());
    p.setFont(Theme::baseFont(10, QFont::DemiBold));
    const QString barsLabel = QString("BARS: %1").arg(m_renderBars);
    m_renderBarsRect = QRectF(renderBox.left() + Theme::px(12),
                              renderBox.top() + Theme::px(12),
                              renderBox.width() * 0.4f, Theme::px(28));
    p.setBrush(Theme::bg3());
    p.setPen(QPen(Theme::stroke(), 1.0));
    p.drawRoundedRect(m_renderBarsRect, Theme::px(8), Theme::px(8));
    p.setPen(Theme::text());
    p.drawText(m_renderBarsRect, Qt::AlignCenter, barsLabel);

    m_renderBtnRect =
        QRectF(renderBox.right() - renderBox.width() * 0.4f - Theme::px(12),
               renderBox.top() + Theme::px(12), renderBox.width() * 0.4f, Theme::px(28));
    p.setBrush(Theme::accent());
    p.setPen(QPen(Theme::stroke(), 1.0));
    p.drawRoundedRect(m_renderBtnRect, Theme::px(8), Theme::px(8));
    p.setPen(Theme::bg0());
    p.setFont(Theme::condensedFont(10, QFont::Bold));
    p.drawText(m_renderBtnRect, Qt::AlignCenter, "RENDER WAV");

    p.setPen(Theme::textMuted());
    p.setFont(Theme::baseFont(9));
    p.drawText(renderBox.adjusted(Theme::px(12), Theme::px(52), -Theme::px(12), 0),
               Qt::AlignLeft | Qt::AlignTop,
               QString("OUTPUT: %1 Hz\nDEST: %2")
                   .arg(m_renderRate)
                   .arg(QDir(renderDir()).absolutePath()));
}

void ProjectMenuOverlay::mousePressEvent(QMouseEvent *event) {
    const QPointF pos = event->position();
    if (m_closeRect.contains(pos)) {
        setVisible(false);
        emit closed();
        return;
    }
    if (!m_panelRect.contains(pos)) {
        setVisible(false);
        emit closed();
        return;
    }

    if (m_bpmMinusRect.contains(pos) && m_pads) {
        m_pads->setBpm(m_pads->bpm() - 1);
        update();
        return;
    }
    if (m_bpmPlusRect.contains(pos) && m_pads) {
        m_pads->setBpm(m_pads->bpm() + 1);
        update();
        return;
    }
    if (m_metronomeRect.contains(pos) && m_seq) {
        m_metronome = !m_metronome;
        m_seq->setMetronomeEnabled(m_metronome);
        update();
        return;
    }
    if (m_rateRect.contains(pos)) {
        m_renderRate = (m_renderRate == 48000) ? 44100 : 48000;
        update();
        return;
    }

    for (int i = 0; i < m_projectRowRects.size(); ++i) {
        if (m_projectRowRects[i].contains(pos)) {
            m_selectedProject = i;
            update();
            return;
        }
    }

    if (m_newRect.contains(pos)) {
        newProject();
        return;
    }
    if (m_saveRect.contains(pos)) {
        const QString name = (m_selectedProject >= 0 && m_selectedProject < m_projectNames.size())
                                 ? m_projectNames[m_selectedProject]
                                 : QString();
        saveProject(name);
        update();
        return;
    }
    if (m_loadRect.contains(pos)) {
        if (m_selectedProject >= 0 && m_selectedProject < m_projectNames.size()) {
            loadProject(m_projectNames[m_selectedProject]);
        }
        update();
        return;
    }

    if (m_renderBarsRect.contains(pos)) {
        static const QVector<int> bars = {1, 2, 4, 8, 16};
        int idx = bars.indexOf(m_renderBars);
        idx = (idx + 1) % bars.size();
        m_renderBars = bars[idx];
        update();
        return;
    }
    if (m_renderBtnRect.contains(pos)) {
        renderProject();
        update();
        return;
    }
}

void ProjectMenuOverlay::keyPressEvent(QKeyEvent *event) {
    const int key = event->key();
    if (key == Qt::Key_M || key == Qt::Key_Escape) {
        setVisible(false);
        emit closed();
        return;
    }
    QWidget::keyPressEvent(event);
}
