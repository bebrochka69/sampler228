#include "EditPageWidget.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QtGlobal>
#include <cmath>

#include "PadBank.h"
#include "SampleSession.h"
#include "Theme.h"
#include "WaveformRenderer.h"

EditPageWidget::EditPageWidget(SampleSession *session, PadBank *pads, QWidget *parent)
    : QWidget(parent), m_session(session), m_pads(pads) {
    setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);

    m_ambientTimer.setInterval(66);
    connect(&m_ambientTimer, &QTimer::timeout, this, [this]() {
        if (isVisible()) {
            update();
        }
    });
    m_ambientTimer.start();

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
        connect(m_pads, &PadBank::activePadChanged, this, [this](int) { update(); });
        connect(m_pads, &PadBank::padChanged, this, [this](int) { update(); });
    }
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

    for (int i = 0; i < m_paramRects.size(); ++i) {
        if (m_paramRects[i].contains(pos)) {
            m_selectedParam = i;
            update();
            return;
        }
    }
}

void EditPageWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);

    QPainter p(this);
    Theme::paintBackground(p, rect());
    p.setRenderHint(QPainter::Antialiasing, true);

    if (m_pads && m_session) {
        const QString padPath = m_pads->padPath(m_pads->activePad());
        if (!padPath.isEmpty() &&
            (padPath != m_session->sourcePath() ||
             m_session->decodeMode() != SampleSession::DecodeMode::Full)) {
            m_session->setSource(padPath, SampleSession::DecodeMode::Full);
        }
    }

    const int margin = 24;
    const int headerHeight = 24;
    const QRectF headerRect(margin, margin, width() - 2 * margin, headerHeight);
    p.setPen(Theme::accent());
    p.setFont(Theme::condensedFont(12, QFont::Bold));
    p.drawText(headerRect, Qt::AlignLeft | Qt::AlignVCenter, "EDIT / SAMPLE");
    p.setPen(Theme::textMuted());
    p.setFont(Theme::baseFont(8));
    p.drawText(QRectF(headerRect.left(), headerRect.top(), headerRect.width(), headerRect.height()),
               Qt::AlignRight | Qt::AlignVCenter, "UP/DOWN select  LEFT/RIGHT adjust  SHIFT=Slice");

    const QRectF waveRect(margin, headerRect.bottom() + 10, width() - 2 * margin, height() * 0.42f);

    p.setBrush(Theme::bg1());
    p.setPen(QPen(Theme::stroke(), 1.2));
    p.drawRoundedRect(waveRect, 12, 12);

    const QRectF waveInner = waveRect.adjusted(12, 12, -12, -12);
    // Living texture beneath waveform (VIDEO_05).
    p.save();
    p.setClipRect(waveInner);
    p.setCompositionMode(QPainter::CompositionMode_SoftLight);
    const float t = Theme::timeSeconds();
    p.setPen(QPen(Theme::withAlpha(Theme::accentAlt(), 60), 1.2));
    for (int i = 0; i < 5; ++i) {
        QPainterPath path;
        const float yBase = waveInner.top() + (i + 1) * (waveInner.height() / 6.0f);
        path.moveTo(waveInner.left(), yBase);
        for (int x = 0; x <= 32; ++x) {
            const float px = waveInner.left() + waveInner.width() * (x / 32.0f);
            const float phase = t * 0.6f + i * 1.3f;
            const float amp = 6.0f + i * 1.4f;
            const float py = yBase + std::sin(px * 0.012f + phase) * amp;
            path.lineTo(px, py);
        }
        p.drawPath(path);
    }
    p.restore();
    QVector<float> wave;
    if (m_session && m_session->hasWaveform()) {
        wave = m_session->waveform();
    }
    if (wave.isEmpty()) {
        p.setPen(Theme::textMuted());
        p.setFont(Theme::baseFont(12, QFont::DemiBold));
        p.drawText(waveInner, Qt::AlignCenter, "NO SAMPLE");
    } else {
        WaveformRenderer::drawWaveform(p, waveInner, wave, Theme::accent(),
                                       Theme::withAlpha(Theme::textMuted(), 120));
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

    // Parameters grid.
    const QRectF gridRect(margin, waveRect.bottom() + 16, width() - 2 * margin,
                          height() - waveRect.bottom() - 96);
    const int cols = 4;
    const int rows = 2;
    const float cellW = (gridRect.width() - (cols - 1) * 16.0f) / cols;
    const float cellH = (gridRect.height() - (rows - 1) * 16.0f) / rows;

    p.setFont(Theme::baseFont(10, QFont::DemiBold));

    m_paramRects.clear();
    for (int i = 0; i < m_params.size(); ++i) {
        const int r = i / cols;
        const int c = i % cols;
        const float x = gridRect.left() + c * (cellW + 16.0f);
        const float y = gridRect.top() + r * (cellH + 16.0f);
        const QRectF cell(x, y, cellW, cellH);
        m_paramRects.push_back(cell);

        const bool selected = (i == m_selectedParam);
        p.setBrush(selected ? Theme::bg2() : Theme::bg1());
        p.setPen(QPen(selected ? Theme::accentAlt() : Theme::stroke(), selected ? 1.6 : 1.0));
        p.drawRoundedRect(cell, 10, 10);

        const QRectF iconRect = cell.adjusted(14, 12, -14, -28);
        const QPixmap icon = iconForType(m_params[i].type);
        if (!icon.isNull()) {
            const QRectF drawRect = iconRect.adjusted(6, 6, -6, -6);
            const QPixmap scaled = icon.scaled(drawRect.size().toSize(), Qt::KeepAspectRatio,
                                              Qt::SmoothTransformation);
            const QPointF center = drawRect.center();
            const QRectF target(center.x() - scaled.width() / 2.0, center.y() - scaled.height() / 2.0,
                                scaled.width(), scaled.height());
            p.drawPixmap(target, scaled, scaled.rect());
        } else {
            p.setPen(Qt::NoPen);
            p.setBrush(selected ? Theme::accent() : Theme::accentAlt());
            p.drawRoundedRect(iconRect.adjusted(6, 6, -6, -6), 6, 6);
        }

        p.setPen(selected ? Theme::accentAlt() : Theme::text());
        p.drawText(QRectF(cell.left(), cell.bottom() - 26, cell.width(), 16),
                   Qt::AlignCenter, m_params[i].label);

        float valueNorm = 0.0f;
        QString valueText;

        switch (m_params[i].type) {
            case Param::Volume:
                valueNorm = params.volume;
                valueText = QString("%1%").arg(static_cast<int>(params.volume * 100));
                break;
            case Param::Pan: {
                valueNorm = (params.pan + 1.0f) * 0.5f;
                const int panVal = static_cast<int>(qAbs(params.pan) * 100);
                valueText = params.pan < 0.0f ? QString("L%1").arg(panVal)
                                              : QString("R%1").arg(panVal);
                if (panVal == 0) {
                    valueText = "C";
                }
                break;
            }
            case Param::Pitch: {
                valueNorm = (params.pitch + 12.0f) / 24.0f;
                const int pitchVal = static_cast<int>(params.pitch);
                valueText = QString("%1%2 st").arg(pitchVal >= 0 ? "+" : "").arg(pitchVal);
                break;
            }
            case Param::Stretch:
                valueNorm = static_cast<float>(params.stretchIndex) /
                            static_cast<float>(qMax(1, PadBank::stretchCount() - 1));
                valueText = PadBank::stretchLabel(params.stretchIndex);
                break;
            case Param::Start:
                valueNorm = params.start;
                valueText = QString("%1%").arg(static_cast<int>(params.start * 100));
                break;
            case Param::End:
                valueNorm = params.end;
                valueText = QString("%1%").arg(static_cast<int>(params.end * 100));
                break;
            case Param::Slice: {
                const int count = PadBank::sliceCountForIndex(params.sliceCountIndex);
                valueNorm = static_cast<float>(params.sliceIndex) / static_cast<float>(qMax(1, count - 1));
                if (count <= 1) {
                    valueText = "OFF";
                } else {
                    valueText = QString("%1 / %2").arg(count).arg(params.sliceIndex + 1);
                }
                break;
            }
            case Param::Mode:
                valueNorm = params.loop ? 1.0f : 0.0f;
                valueText = params.loop ? "LOOP" : "ONESHOT";
                break;
        }

        const QRectF valueLine(cell.left() + 18, cell.bottom() - 10, cell.width() - 36, 3);
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::withAlpha(Theme::stroke(), 160));
        p.drawRect(valueLine);
        p.setBrush(selected ? Theme::accent() : Theme::accentAlt());
        p.drawRect(QRectF(valueLine.left(), valueLine.top(), valueLine.width() * valueNorm,
                          valueLine.height()));

        p.setPen(Theme::textMuted());
        p.setFont(Theme::baseFont(8));
        p.drawText(QRectF(cell.left(), cell.top() + 6, cell.width(), 12),
                   Qt::AlignCenter, valueText);
        p.setFont(Theme::baseFont(10, QFont::DemiBold));
    }

    // Action buttons.
    const QRectF buttonsRect(margin, height() - 58, width() - 2 * margin, 40);
    const QRectF deleteRect(buttonsRect.left(), buttonsRect.top(), buttonsRect.width() * 0.45f, 40);
    const QRectF copyRect(buttonsRect.right() - buttonsRect.width() * 0.45f, buttonsRect.top(),
                          buttonsRect.width() * 0.45f, 40);

    p.setFont(Theme::condensedFont(12, QFont::DemiBold));

    // FX bus selector.
    const QRectF fxRect(buttonsRect.center().x() - 90, buttonsRect.top(), 180, 40);
    m_fxBusRect = fxRect;
    p.setBrush(Theme::bg1());
    p.setPen(QPen(Theme::accent(), 1.2));
    p.drawRoundedRect(fxRect, 8, 8);
    const int busIndex = m_pads ? m_pads->fxBus(m_pads->activePad()) : 0;
    const QString busText = QString("FX BUS: %1").arg(PadBank::fxBusLabel(busIndex));
    p.setPen(Theme::accent());
    p.drawText(fxRect, Qt::AlignCenter, busText);

    p.setBrush(Theme::bg1());
    p.setPen(QPen(Theme::accentAlt(), 1.2));
    p.drawRoundedRect(deleteRect, 10, 10);
    p.setPen(Theme::accentAlt());
    p.drawText(deleteRect, Qt::AlignCenter, "DELETE PAD");

    p.setBrush(Theme::bg1());
    p.setPen(QPen(Theme::accent(), 1.2));
    p.drawRoundedRect(copyRect, 10, 10);
    p.setPen(Theme::accent());
    p.drawText(copyRect, Qt::AlignCenter, "COPY PAD");

    // Idle ambience (VIDEO_06).
    if (!m_pads || !m_pads->isPlaying(m_pads->activePad())) {
        Theme::drawIdleDust(p, rect(), 0.05f);
    }
}
