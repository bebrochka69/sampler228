#include "EditPageWidget.h"

#include <QPainter>

#include "SampleSession.h"
#include "Theme.h"
#include "WaveformRenderer.h"

EditPageWidget::EditPageWidget(SampleSession *session, QWidget *parent)
    : QWidget(parent), m_session(session) {
    setAutoFillBackground(false);

    m_params = {
        {"VOLUME", 0.72f},
        {"PAN", 0.44f},
        {"PITCH", 0.58f},
        {"STRETCH", 0.36f},
        {"START", 0.18f},
        {"END", 0.92f},
        {"SLICE", 0.60f},
        {"MODE", 0.42f},
    };

    m_wave = WaveformRenderer::makeDemoWave(220, 21);

    if (m_session) {
        connect(m_session, &SampleSession::waveformChanged, this, [this]() { update(); });
    }
}

void EditPageWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    QLinearGradient bg(0, 0, 0, height());
    bg.setColorAt(0.0, Theme::bg0());
    bg.setColorAt(1.0, Theme::bg2());
    p.fillRect(rect(), bg);

    const int margin = 24;
    const QRectF waveRect(margin, margin, width() - 2 * margin, height() * 0.45f);

    p.setBrush(Theme::bg1());
    p.setPen(QPen(Theme::stroke(), 1.0));
    p.drawRoundedRect(waveRect, 10, 10);

    const QRectF waveInner = waveRect.adjusted(12, 12, -12, -12);
    QVector<float> wave = m_wave;
    if (m_session && m_session->hasWaveform()) {
        wave = m_session->waveform();
    }
    WaveformRenderer::drawWaveform(p, waveInner, wave, Theme::accentAlt(),
                                   Theme::withAlpha(Theme::textMuted(), 120));

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

    // Parameters grid.
    const QRectF gridRect(margin, waveRect.bottom() + 16, width() - 2 * margin,
                          height() - waveRect.bottom() - 90);
    const int cols = 4;
    const int rows = 2;
    const float cellW = (gridRect.width() - (cols - 1) * 16.0f) / cols;
    const float cellH = (gridRect.height() - (rows - 1) * 16.0f) / rows;

    p.setFont(Theme::baseFont(10, QFont::DemiBold));

    for (int i = 0; i < m_params.size(); ++i) {
        const int r = i / cols;
        const int c = i % cols;
        const float x = gridRect.left() + c * (cellW + 16.0f);
        const float y = gridRect.top() + r * (cellH + 16.0f);
        const QRectF cell(x, y, cellW, cellH);

        p.setBrush(Theme::bg1());
        p.setPen(QPen(Theme::stroke(), 1.0));
        p.drawRoundedRect(cell, 10, 10);

        const QRectF iconRect = cell.adjusted(14, 12, -14, -28);
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::accentAlt());

        switch (i % 4) {
            case 0:
                p.drawEllipse(iconRect.center(), iconRect.width() * 0.25f, iconRect.height() * 0.25f);
                break;
            case 1: {
                QPolygonF tri;
                tri << QPointF(iconRect.center().x(), iconRect.top())
                    << QPointF(iconRect.right(), iconRect.bottom())
                    << QPointF(iconRect.left(), iconRect.bottom());
                p.drawPolygon(tri);
                break;
            }
            case 2:
                p.drawRoundedRect(iconRect.adjusted(6, 6, -6, -6), 6, 6);
                break;
            case 3:
                p.drawRect(iconRect.adjusted(10, 10, -10, -10));
                break;
        }

        p.setPen(Theme::text());
        p.drawText(QRectF(cell.left(), cell.bottom() - 26, cell.width(), 16),
                   Qt::AlignCenter, m_params[i].label);

        const QRectF valueLine(cell.left() + 18, cell.bottom() - 10, cell.width() - 36, 3);
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::withAlpha(Theme::stroke(), 160));
        p.drawRoundedRect(valueLine, 2, 2);
        p.setBrush(Theme::accent());
        p.drawRoundedRect(QRectF(valueLine.left(), valueLine.top(), valueLine.width() * m_params[i].value,
                                 valueLine.height()),
                          2, 2);
    }

    // Action buttons.
    const QRectF buttonsRect(margin, height() - 58, width() - 2 * margin, 40);
    const QRectF deleteRect(buttonsRect.left(), buttonsRect.top(), buttonsRect.width() * 0.45f, 40);
    const QRectF copyRect(buttonsRect.right() - buttonsRect.width() * 0.45f, buttonsRect.top(),
                          buttonsRect.width() * 0.45f, 40);

    p.setFont(Theme::condensedFont(12, QFont::DemiBold));

    p.setBrush(Theme::bg1());
    p.setPen(QPen(Theme::accent(), 1.4));
    p.drawRoundedRect(deleteRect, 8, 8);
    p.setPen(Theme::accent());
    p.drawText(deleteRect, Qt::AlignCenter, "DELETE PAD");

    p.setBrush(Theme::bg1());
    p.setPen(QPen(Theme::accentAlt(), 1.4));
    p.drawRoundedRect(copyRect, 8, 8);
    p.setPen(Theme::accentAlt());
    p.drawText(copyRect, Qt::AlignCenter, "COPY PAD");
}
