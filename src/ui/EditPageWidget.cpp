#include "EditPageWidget.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QShowEvent>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

#include "PadBank.h"
#include "SampleSession.h"
#include "Theme.h"
#include "WaveformRenderer.h"

namespace {
void drawWaveformRibbon(QPainter &p, const QRectF &rect, const QVector<float> &samples, float gain) {
    if (samples.isEmpty() || rect.width() <= 2.0 || rect.height() <= 2.0) {
        return;
    }

    const int count = samples.size();
    const int steps = qMax(2, static_cast<int>(rect.width()));
    const float midY = rect.center().y();
    const float amp = rect.height() * 0.45f;

    p.save();
    p.setClipRect(rect);
    p.setPen(QPen(QColor(25, 220, 255), 1.2));
    for (int x = 0; x < steps; ++x) {
        const int i0 = (x * count) / steps;
        const int i1 = qMin(count - 1, ((x + 1) * count) / steps);
        float minV = 1.0f;
        float maxV = -1.0f;
        for (int i = i0; i <= i1; ++i) {
            const float v = qBound(-1.0f, samples[i] * gain, 1.0f);
            if (v < minV) minV = v;
            if (v > maxV) maxV = v;
        }
        const float px = rect.left() + static_cast<float>(x);
        const float yTop = midY - maxV * amp;
        const float yBot = midY - minV * amp;
        p.drawLine(QPointF(px, yTop), QPointF(px, yBot));
    }
    p.setPen(QPen(QColor(180, 200, 220, 120), 1.0));
    p.drawLine(QPointF(rect.left(), midY), QPointF(rect.right(), midY));
    p.restore();
}

QString keyNameFromIndex(int idx, bool minor) {
    static const char *names[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                  "F#", "G",  "G#", "A",  "A#", "B"};
    idx = (idx % 12 + 12) % 12;
    return QString("%1 %2").arg(names[idx]).arg(minor ? "MIN" : "MAJ");
}

QString detectKeyFromBuffer(const std::shared_ptr<AudioEngine::Buffer> &buffer) {
    if (!buffer || !buffer->isValid()) {
        return QString();
    }

    const int channels = std::max(1, buffer->channels);
    const int sampleRate = std::max(1, buffer->sampleRate);
    const int totalFrames = buffer->frames();
    const int maxFrames = std::min(totalFrames, sampleRate * 4);
    if (maxFrames <= 0) {
        return QString();
    }

    const int targetRate = 8000;
    int step = std::max(1, sampleRate / targetRate);
    int usedRate = sampleRate / step;
    if (usedRate < 2000) {
        step = 1;
        usedRate = sampleRate;
    }

    std::vector<float> mono;
    mono.reserve(static_cast<size_t>(maxFrames / step + 1));
    const QVector<float> &samples = buffer->samples;
    for (int i = 0; i < maxFrames; i += step) {
        const int base = i * channels;
        float v = 0.0f;
        for (int ch = 0; ch < channels; ++ch) {
            const int idx = base + ch;
            if (idx < samples.size()) {
                v += samples[idx];
            }
        }
        mono.push_back(v / static_cast<float>(channels));
    }

    if (mono.size() < 512) {
        return QString();
    }

    const int window = std::min(1024, static_cast<int>(mono.size()));
    const int hop = window / 2;
    int minLag = usedRate / 1000;
    int maxLag = usedRate / 50;
    minLag = std::max(4, minLag);
    maxLag = std::min(maxLag, window - 4);
    if (maxLag <= minLag) {
        return QString();
    }

    std::array<double, 12> histogram{};
    histogram.fill(0.0);
    int windows = 0;

    for (int start = 0; start + window < static_cast<int>(mono.size()); start += hop) {
        if (windows++ > 80) {
            break;
        }
        const float *x = mono.data() + start;
        double energy = 0.0;
        for (int i = 0; i < window; ++i) {
            energy += x[i] * x[i];
        }
        if (energy < 1e-4) {
            continue;
        }

        double bestCorr = 0.0;
        int bestLag = -1;
        for (int lag = minLag; lag <= maxLag; ++lag) {
            double corr = 0.0;
            for (int i = 0; i < window - lag; ++i) {
                corr += x[i] * x[i + lag];
            }
            if (corr > bestCorr) {
                bestCorr = corr;
                bestLag = lag;
            }
        }

        if (bestLag <= 0) {
            continue;
        }

        const double norm = bestCorr / energy;
        if (norm < 0.08) {
            continue;
        }
        const double freq = static_cast<double>(usedRate) / static_cast<double>(bestLag);
        if (freq < 50.0 || freq > 1000.0) {
            continue;
        }
        const double midi = 69.0 + 12.0 * std::log2(freq / 440.0);
        int pc = static_cast<int>(std::lround(midi)) % 12;
        if (pc < 0) {
            pc += 12;
        }
        histogram[static_cast<size_t>(pc)] += norm;
    }

    double sum = 0.0;
    for (double v : histogram) {
        sum += v;
    }
    if (sum < 0.5) {
        return QString();
    }
    for (double &v : histogram) {
        v /= sum;
    }

    static const double majorProfile[12] = {6.35, 2.23, 3.48, 2.33, 4.38, 4.09,
                                            2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
    static const double minorProfile[12] = {6.33, 2.68, 3.52, 5.38, 2.60, 3.53,
                                            2.54, 4.75, 3.98, 2.69, 3.34, 3.17};
    double majorSum = 0.0;
    double minorSum = 0.0;
    for (double v : majorProfile) {
        majorSum += v;
    }
    for (double v : minorProfile) {
        minorSum += v;
    }

    int bestMajor = 0;
    int bestMinor = 0;
    double bestMajorScore = -1.0;
    double bestMinorScore = -1.0;
    for (int key = 0; key < 12; ++key) {
        double majorScore = 0.0;
        double minorScore = 0.0;
        for (int i = 0; i < 12; ++i) {
            const int idx = (i + key) % 12;
            majorScore += histogram[static_cast<size_t>(idx)] * majorProfile[i];
            minorScore += histogram[static_cast<size_t>(idx)] * minorProfile[i];
        }
        majorScore /= majorSum;
        minorScore /= minorSum;
        if (majorScore > bestMajorScore) {
            bestMajorScore = majorScore;
            bestMajor = key;
        }
        if (minorScore > bestMinorScore) {
            bestMinorScore = minorScore;
            bestMinor = key;
        }
    }

    const double bestScore = std::max(bestMajorScore, bestMinorScore);
    if (bestScore < 0.18) {
        return QString();
    }
    const bool chooseMinor = bestMinorScore > bestMajorScore;
    const int bestKey = chooseMinor ? bestMinor : bestMajor;
    return keyNameFromIndex(bestKey, chooseMinor);
}
}  // namespace

EditPageWidget::EditPageWidget(SampleSession *session, PadBank *pads, QWidget *parent)
    : QWidget(parent), m_session(session), m_pads(pads) {
    setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);

    m_animTimer.setInterval(33);
    connect(&m_animTimer, &QTimer::timeout, this, [this]() {
        if (!isVisible()) {
            return;
        }
        bool active = false;
        if (m_pads) {
            active = m_pads->isPlaying(m_pads->activePad());
        }
        if (!active && m_session) {
            active = m_session->isPlaying();
        }
        if (active) {
            update();
        }
    });
    m_animTimer.start();

    m_params = {
        {"VOLUME", Param::Volume},
        {"PAN", Param::Pan},
        {"PITCH", Param::Pitch},
        {"STRETCH", Param::Stretch},
        {"START", Param::Start},
        {"END", Param::End},
        {"SLICE", Param::Slice},
        {"MODE", Param::Mode},
    };

    if (m_session) {
        connect(m_session, &SampleSession::waveformChanged, this, [this]() { update(); });
    }
    if (m_pads) {
        connect(m_pads, &PadBank::padParamsChanged, this, [this](int) { update(); });
        connect(m_pads, &PadBank::activePadChanged, this, [this](int) {
            syncWaveSource();
            m_keyText = "KEY: --";
            update();
        });
        connect(m_pads, &PadBank::padChanged, this, [this](int) {
            syncWaveSource();
            m_keyText = "KEY: --";
            update();
        });
    }

    syncWaveSource();
}

QString EditPageWidget::iconFileFor(Param::Type type) const {
    QString base;
    switch (type) {
        case Param::Volume:
            base = "volume";
            break;
        case Param::Pan:
            base = "pan";
            break;
        case Param::Pitch:
            base = "pitch";
            break;
        case Param::Stretch:
            base = "stretch";
            break;
        case Param::Start:
            base = "start";
            break;
        case Param::End:
            base = "end";
            break;
        case Param::Slice:
            base = "slice";
            break;
        case Param::Mode:
            base = "mode";
            break;
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList searchDirs = {appDir + "/icons", appDir + "/assets/icons"};
    const QStringList files = {base + ".png"};

    for (const QString &dir : searchDirs) {
        for (const QString &file : files) {
            const QString path = dir + "/" + file;
            if (QFileInfo::exists(path)) {
                return path;
            }
        }
    }
    return QString();
}

QPixmap EditPageWidget::iconForType(Param::Type type) {
    const int key = static_cast<int>(type);
    if (m_iconCache.contains(key)) {
        return m_iconCache.value(key);
    }
    QPixmap pix;
    const QString path = iconFileFor(type);
    if (!path.isEmpty()) {
        pix.load(path);
    }
    m_iconCache.insert(key, pix);
    return pix;
}

void EditPageWidget::keyPressEvent(QKeyEvent *event) {
    if (m_params.isEmpty() || !m_pads) {
        return;
    }

    const int key = event->key();
    if (key == Qt::Key_Down) {
        m_selectedParam = (m_selectedParam + 1) % m_params.size();
        update();
        return;
    }
    if (key == Qt::Key_Up) {
        m_selectedParam = (m_selectedParam - 1 + m_params.size()) % m_params.size();
        update();
        return;
    }

    const int pad = m_pads->activePad();
    const bool shift = event->modifiers().testFlag(Qt::ShiftModifier);
    const Param::Type type = m_params[m_selectedParam].type;

    if (key == Qt::Key_Space) {
        if (m_pads->isPlaying(pad)) {
            m_pads->stopPad(pad);
        } else {
            m_pads->triggerPad(pad);
        }
        update();
        return;
    }
    if (key == Qt::Key_F) {
        const int nextBus = (m_pads->fxBus(pad) + 1) % 6;
        m_pads->setFxBus(pad, nextBus);
        update();
        return;
    }

    auto adjust = [&](float delta) {
        PadBank::PadParams params = m_pads->params(pad);
        switch (type) {
            case Param::Volume:
                m_pads->setVolume(pad, params.volume + delta);
                break;
            case Param::Pan:
                m_pads->setPan(pad, params.pan + delta);
                break;
            case Param::Pitch:
                m_pads->setPitch(pad, params.pitch + delta * 12.0f);
                break;
            case Param::Start:
                m_pads->setStart(pad, params.start + delta);
                break;
            case Param::End:
                m_pads->setEnd(pad, params.end + delta);
                break;
            default:
                break;
        }
    };

    if (key == Qt::Key_Left || key == Qt::Key_Minus) {
        switch (type) {
            case Param::Stretch:
                m_pads->setStretchIndex(pad, m_pads->params(pad).stretchIndex - 1);
                break;
            case Param::Slice:
                if (shift) {
                    m_pads->setSliceCountIndex(pad, m_pads->params(pad).sliceCountIndex - 1);
                } else {
                    m_pads->setSliceIndex(pad, m_pads->params(pad).sliceIndex - 1);
                }
                break;
            case Param::Mode:
                m_pads->setLoop(pad, !m_pads->params(pad).loop);
                break;
            case Param::Pitch:
                adjust(-1.0f / 12.0f);
                break;
            case Param::Pan:
                adjust(-0.05f);
                break;
            case Param::Volume:
                adjust(-0.02f);
                break;
            case Param::Start:
            case Param::End:
                adjust(-0.01f);
                break;
        }
        update();
        return;
    }
    if (key == Qt::Key_Right || key == Qt::Key_Plus || key == Qt::Key_Equal) {
        switch (type) {
            case Param::Stretch:
                m_pads->setStretchIndex(pad, m_pads->params(pad).stretchIndex + 1);
                break;
            case Param::Slice:
                if (shift) {
                    m_pads->setSliceCountIndex(pad, m_pads->params(pad).sliceCountIndex + 1);
                } else {
                    m_pads->setSliceIndex(pad, m_pads->params(pad).sliceIndex + 1);
                }
                break;
            case Param::Mode:
                m_pads->setLoop(pad, !m_pads->params(pad).loop);
                break;
            case Param::Pitch:
                adjust(1.0f / 12.0f);
                break;
            case Param::Pan:
                adjust(0.05f);
                break;
            case Param::Volume:
                adjust(0.02f);
                break;
            case Param::Start:
            case Param::End:
                adjust(0.01f);
                break;
        }
        update();
        return;
    }
    if (key == Qt::Key_Home) {
        switch (type) {
            case Param::Volume:
                m_pads->setVolume(pad, 0.0f);
                break;
            case Param::Pan:
                m_pads->setPan(pad, -1.0f);
                break;
            case Param::Pitch:
                m_pads->setPitch(pad, -12.0f);
                break;
            case Param::Start:
                m_pads->setStart(pad, 0.0f);
                break;
            case Param::End:
                m_pads->setEnd(pad, 0.0f);
                break;
            default:
                break;
        }
        update();
        return;
    }
    if (key == Qt::Key_End) {
        switch (type) {
            case Param::Volume:
                m_pads->setVolume(pad, 1.0f);
                break;
            case Param::Pan:
                m_pads->setPan(pad, 1.0f);
                break;
            case Param::Pitch:
                m_pads->setPitch(pad, 12.0f);
                break;
            case Param::Start:
                m_pads->setStart(pad, 1.0f);
                break;
            case Param::End:
                m_pads->setEnd(pad, 1.0f);
                break;
            default:
                break;
        }
        update();
        return;
    }
}

void EditPageWidget::mousePressEvent(QMouseEvent *event) {
    setFocus(Qt::MouseFocusReason);
    const QPointF pos = event->position();

    if (m_fxBusRect.contains(pos) && m_pads) {
        const int pad = m_pads->activePad();
        const int nextBus = (m_pads->fxBus(pad) + 1) % 6;
        m_pads->setFxBus(pad, nextBus);
        update();
        return;
    }

    if (m_stretchModeRect.contains(pos) && m_pads) {
        const int pad = m_pads->activePad();
        const PadBank::PadParams pp = m_pads->params(pad);
        const int nextMode = (pp.stretchMode > 0) ? 0 : 1;
        m_pads->setStretchMode(pad, nextMode);
        update();
        return;
    }

    if (m_keyButtonRect.contains(pos) && m_pads) {
        const int pad = m_pads->activePad();
        const QString loading = "KEY: ...";
        m_keyText = loading;
        auto buffer = m_pads->rawBuffer(pad);
        if (!buffer || !buffer->isValid()) {
            m_pads->requestRawBuffer(pad);
            m_keyText = "KEY: LOADING";
        } else {
            const QString key = detectKeyFromBuffer(buffer);
            m_keyText = key.isEmpty() ? "KEY: UNKNOWN" : QString("KEY: %1").arg(key);
        }
        update();
        return;
    }

    if (m_normalizeRect.contains(pos) && m_pads) {
        const int pad = m_pads->activePad();
        PadBank::PadParams params = m_pads->params(pad);
        m_pads->setNormalize(pad, !params.normalize);
        update();
        return;
    }

    if (m_copyRect.contains(pos) && m_pads) {
        const int from = m_pads->activePad();
        const int to = (from + 1) % m_pads->padCount();
        m_pads->copyPad(from, to);
        m_pads->setActivePad(to);
        update();
        return;
    }

    for (int i = 0; i < m_paramRects.size(); ++i) {
        if (m_paramRects[i].contains(pos)) {
            m_selectedParam = i;
            update();
            return;
        }
    }
}

void EditPageWidget::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    syncWaveSource();
}

void EditPageWidget::syncWaveSource() {
    if (!m_pads || !m_session) {
        return;
    }
    const QString padPath = m_pads->padPath(m_pads->activePad());
    if (padPath.isEmpty()) {
        return;
    }
    const SampleSession::DecodeMode mode =
        Theme::liteMode() ? SampleSession::DecodeMode::Fast : SampleSession::DecodeMode::Full;
    if (padPath != m_session->sourcePath() || m_session->decodeMode() != mode) {
        m_session->setSource(padPath, mode);
    }
}

void EditPageWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);

    QPainter p(this);
    Theme::paintBackground(p, rect());
    Theme::applyRenderHints(p);

    const int margin = Theme::px(24);
    const int headerHeight = Theme::px(24);
    const QRectF headerRect(margin, margin, width() - 2 * margin, headerHeight);
    const float keyW = Theme::pxF(70.0f);
    const float keyH = Theme::pxF(20.0f);
    const float stretchW = Theme::pxF(140.0f);
    m_keyButtonRect = QRectF(headerRect.right() - keyW, headerRect.top(), keyW, keyH);
    m_stretchModeRect =
        QRectF(m_keyButtonRect.left() - Theme::pxF(8.0f) - stretchW, headerRect.top(),
               stretchW, keyH);
    p.setPen(Theme::accent());
    p.setFont(Theme::condensedFont(12, QFont::Bold));
    p.drawText(headerRect, Qt::AlignLeft | Qt::AlignVCenter, "EDIT / SAMPLE");
    p.setPen(Theme::textMuted());
    p.setFont(Theme::baseFont(8));
    const QRectF helpRect(headerRect.left(), headerRect.top(),
                          m_stretchModeRect.left() - headerRect.left() - Theme::px(6),
                          headerRect.height());
    p.drawText(helpRect, Qt::AlignRight | Qt::AlignVCenter,
               "UP/DOWN select  LEFT/RIGHT adjust  SHIFT=Slice");

    const QRectF waveRect(margin, headerRect.bottom() + Theme::px(10),
                          width() - 2 * margin, height() * 0.42f);

    p.setBrush(Theme::bg2());
    p.setPen(QPen(Theme::stroke(), 1.0));
    p.drawRoundedRect(m_keyButtonRect, Theme::px(6), Theme::px(6));
    p.setPen(Theme::accentAlt());
    p.setFont(Theme::baseFont(8, QFont::Bold));
    p.drawText(m_keyButtonRect, Qt::AlignCenter, "KEY");

    QString stretchLabel = "STRETCH: SIMPLE";
    if (m_pads) {
        const PadBank::PadParams pp = m_pads->params(m_pads->activePad());
        stretchLabel = pp.stretchMode > 0 ? "STRETCH: HQ" : "STRETCH: SIMPLE";
    }
    p.setBrush(Theme::bg2());
    p.setPen(QPen(Theme::stroke(), 1.0));
    p.drawRoundedRect(m_stretchModeRect, Theme::px(6), Theme::px(6));
    p.setPen(Theme::text());
    p.setFont(Theme::baseFont(8, QFont::DemiBold));
    p.drawText(m_stretchModeRect.adjusted(Theme::px(6), 0, -Theme::px(6), 0),
               Qt::AlignLeft | Qt::AlignVCenter, stretchLabel);

    p.setBrush(QColor(128, 35, 60));
    p.setPen(QPen(Theme::accent(), 1.4));
    p.drawRoundedRect(waveRect, Theme::px(12), Theme::px(12));

    const QRectF waveInner = waveRect.adjusted(Theme::px(12), Theme::px(12),
                                               -Theme::px(12), -Theme::px(12));
    QVector<float> wave;
    if (m_session && m_session->hasWaveform()) {
        wave = m_session->waveform();
    }
    if (wave.isEmpty()) {
        p.setPen(Theme::textMuted());
        p.setFont(Theme::baseFont(12, QFont::DemiBold));
        p.drawText(waveInner, Qt::AlignCenter, "NO SAMPLE");
    } else {
        float normGain = 1.0f;
        if (m_pads) {
            normGain = m_pads->normalizeGainForPad(m_pads->activePad());
        }
        drawWaveformRibbon(p, waveInner, wave, normGain);
    }

    if (!m_keyText.isEmpty()) {
        p.setPen(Theme::textMuted());
        p.setFont(Theme::baseFont(9, QFont::DemiBold));
        p.drawText(QRectF(waveInner.left(), waveInner.bottom() + Theme::px(6),
                          waveInner.width(), Theme::px(16)),
                   Qt::AlignLeft | Qt::AlignVCenter, m_keyText);
    }

    // Vertical grid lines with stronger center divisions.
    const int lines = 17;
    for (int i = 0; i < lines; ++i) {
        const float x = waveInner.left() + (waveInner.width() * i) / (lines - 1);
        const bool major = (i % 4 == 0);
        QColor lineColor = major ? Theme::withAlpha(Theme::textMuted(), 160)
                                 : Theme::withAlpha(Theme::textMuted(), 80);
        p.setPen(QPen(lineColor, major ? 2.0 : 1.0));
        p.drawLine(QPointF(x, waveInner.top()), QPointF(x, waveInner.bottom()));
    }

    PadBank::PadParams params;
    if (m_pads) {
        params = m_pads->params(m_pads->activePad());
    }

    const float start = qBound(0.0f, params.start, 1.0f);
    const float end = qBound(0.0f, params.end, 1.0f);
    const float sliceStart = qMin(start, end);
    const float sliceEnd = qMax(start, end);

    const float startX = waveInner.left() + waveInner.width() * sliceStart;
    const float endX = waveInner.left() + waveInner.width() * sliceEnd;

    p.setPen(Qt::NoPen);
    p.setBrush(Theme::withAlpha(Theme::accentAlt(), 28));
    p.drawRect(QRectF(startX, waveInner.top(), endX - startX, waveInner.height()));

    const int sliceCount = PadBank::sliceCountForIndex(params.sliceCountIndex);
    const int sliceIndex = qBound(0, params.sliceIndex, sliceCount - 1);
    const float sliceW = (sliceEnd - sliceStart) / static_cast<float>(sliceCount);

    if (sliceCount > 1 && sliceW > 0.0f) {
        for (int i = 1; i < sliceCount; ++i) {
            const float sx = waveInner.left() + waveInner.width() * (sliceStart + sliceW * i);
            p.setPen(QPen(Theme::withAlpha(Theme::accentAlt(), 120), 1.0));
            p.drawLine(QPointF(sx, waveInner.top() + 4), QPointF(sx, waveInner.bottom() - 4));
        }

        const float selStart = sliceStart + sliceW * sliceIndex;
        const float selEnd = selStart + sliceW;
        const float selX = waveInner.left() + waveInner.width() * selStart;
        const float selW = waveInner.width() * sliceW;
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::withAlpha(Theme::accent(), 48));
        p.drawRect(QRectF(selX, waveInner.top(), selW, waveInner.height()));
    }

    p.setPen(QPen(Theme::accentAlt(), 2.0));
    p.drawLine(QPointF(startX, waveInner.top()), QPointF(startX, waveInner.bottom()));
    p.setPen(QPen(Theme::accent(), 2.0));
    p.drawLine(QPointF(endX, waveInner.top()), QPointF(endX, waveInner.bottom()));

    float playhead = -1.0f;
    if (m_pads) {
        playhead = m_pads->padPlayhead(m_pads->activePad());
    }
    if (playhead < 0.0f && m_session) {
        playhead = m_session->playbackProgress();
    }
    if (playhead >= 0.0f) {
        const float clamped = qBound(0.0f, playhead, 1.0f);
        const float px = waveInner.left() + waveInner.width() * clamped;
        p.setPen(QPen(Theme::withAlpha(Theme::accent(), 220), 2.0));
        p.drawLine(QPointF(px, waveInner.top()), QPointF(px, waveInner.bottom()));
    }

    // Parameters list (large text, no boxes).
    const QRectF listRect(margin, waveRect.bottom() + Theme::px(18),
                          width() - 2 * margin, height() - waveRect.bottom() - Theme::px(96));
    const int rows = 4;
    const int cols = 2;
    const float colGap = Theme::pxF(24.0f);
    const float colW = (listRect.width() - colGap) / cols;
    const float rowH = listRect.height() / rows;

    m_paramRects.clear();
    p.setFont(Theme::condensedFont(20, QFont::Bold));

    for (int i = 0; i < m_params.size(); ++i) {
        const int c = i / rows;
        const int r = i % rows;
        const float x = listRect.left() + c * (colW + colGap);
        const float y = listRect.top() + r * rowH;
        const QRectF rowRect(x, y, colW, rowH);
        m_paramRects.push_back(rowRect);

        const bool selected = (i == m_selectedParam);
        const QColor labelColor = selected ? Theme::accent() : Theme::text();
        const QColor valueColor = selected ? Theme::accentAlt() : Theme::textMuted();

        QString valueText;
        switch (m_params[i].type) {
            case Param::Volume:
                valueText = QString("%1%").arg(static_cast<int>(params.volume * 100));
                break;
            case Param::Pan: {
                const int panVal = static_cast<int>(qAbs(params.pan) * 100);
                valueText = params.pan < 0.0f ? QString("L%1").arg(panVal)
                                              : QString("R%1").arg(panVal);
                if (panVal == 0) {
                    valueText = "C";
                }
                break;
            }
            case Param::Pitch: {
                const int pitchVal = static_cast<int>(params.pitch);
                valueText = QString("%1%2 st").arg(pitchVal >= 0 ? "+" : "").arg(pitchVal);
                break;
            }
            case Param::Stretch:
                valueText = PadBank::stretchLabel(params.stretchIndex);
                break;
            case Param::Start:
                valueText = QString("%1%").arg(static_cast<int>(params.start * 100));
                break;
            case Param::End:
                valueText = QString("%1%").arg(static_cast<int>(params.end * 100));
                break;
            case Param::Slice: {
                const int count = PadBank::sliceCountForIndex(params.sliceCountIndex);
                if (count <= 1) {
                    valueText = "OFF";
                } else {
                    valueText = QString("%1 / %2").arg(count).arg(params.sliceIndex + 1);
                }
                break;
            }
            case Param::Mode:
                valueText = params.loop ? "LOOP" : "ONESHOT";
                break;
        }

        p.setPen(labelColor);
        p.drawText(QRectF(rowRect.left(), rowRect.top(), rowRect.width(), rowRect.height()),
                   Qt::AlignLeft | Qt::AlignVCenter, m_params[i].label);

        p.setPen(valueColor);
        p.setFont(Theme::condensedFont(18, QFont::DemiBold));
        p.drawText(QRectF(rowRect.left(), rowRect.top(), rowRect.width(), rowRect.height()),
                   Qt::AlignRight | Qt::AlignVCenter, valueText);
        p.setFont(Theme::condensedFont(20, QFont::Bold));

        // underline for selected row
        if (selected) {
            p.setPen(QPen(Theme::accentAlt(), Theme::pxF(2.0f)));
            p.drawLine(QPointF(rowRect.left(), rowRect.bottom() - Theme::px(6)),
                       QPointF(rowRect.right(), rowRect.bottom() - Theme::px(6)));
        } else {
            p.setPen(QPen(Theme::withAlpha(Theme::stroke(), 80), Theme::pxF(1.0f)));
            p.drawLine(QPointF(rowRect.left(), rowRect.bottom() - Theme::px(6)),
                       QPointF(rowRect.right(), rowRect.bottom() - Theme::px(6)));
        }
    }

    // Action buttons.
    const QRectF buttonsRect(margin, height() - Theme::px(58),
                             width() - 2 * margin, Theme::px(40));
    const QRectF deleteRect(buttonsRect.left(), buttonsRect.top(),
                            buttonsRect.width() * 0.45f, Theme::px(40));
    const QRectF copyRect(buttonsRect.right() - buttonsRect.width() * 0.45f,
                          buttonsRect.top(), buttonsRect.width() * 0.45f, Theme::px(40));
    m_deleteRect = deleteRect;
    m_copyRect = copyRect;

    p.setFont(Theme::condensedFont(12, QFont::DemiBold));

    // FX bus selector.
    const QRectF fxRect(buttonsRect.center().x() - Theme::px(90),
                        buttonsRect.top(), Theme::px(180), Theme::px(40));
    m_fxBusRect = fxRect;
    p.setBrush(Theme::bg1());
    p.setPen(QPen(Theme::accent(), 1.2));
    p.drawRoundedRect(fxRect, Theme::px(8), Theme::px(8));
    const int busIndex = m_pads ? m_pads->fxBus(m_pads->activePad()) : 0;
    const QString busText = QString("FX BUS: %1").arg(PadBank::fxBusLabel(busIndex));
    p.setPen(Theme::accent());
    p.drawText(fxRect, Qt::AlignCenter, busText);

    // Normalize button (placed above button row for visibility).
    const QRectF normRect(margin, buttonsRect.top() - Theme::px(46),
                          Theme::px(180), Theme::px(40));
    m_normalizeRect = normRect;
    const bool normOn = m_pads ? m_pads->params(m_pads->activePad()).normalize : false;
    p.setBrush(normOn ? Theme::accentAlt() : Theme::bg1());
    p.setPen(QPen(normOn ? Theme::accentAlt() : Theme::accent(), 1.2));
    p.drawRoundedRect(normRect, Theme::px(8), Theme::px(8));
    p.setPen(normOn ? Theme::bg0() : Theme::accent());
    p.drawText(normRect, Qt::AlignCenter, "NORMALIZE");

    p.setBrush(Theme::bg1());
    p.setPen(QPen(Theme::accentAlt(), 1.2));
    p.drawRoundedRect(deleteRect, Theme::px(10), Theme::px(10));
    p.setPen(Theme::accentAlt());
    p.drawText(deleteRect, Qt::AlignCenter, "DELETE PAD");

    p.setBrush(Theme::bg1());
    p.setPen(QPen(Theme::accent(), 1.2));
    p.drawRoundedRect(copyRect, Theme::px(10), Theme::px(10));
    p.setPen(Theme::accent());
    p.drawText(copyRect, Qt::AlignCenter, "COPY PAD");

}
