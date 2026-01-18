#include "EditPageWidget.h"

#include <QKeyEvent>
#include <QPainter>
#include <QtGlobal>

#include "SampleSession.h"
#include "Theme.h"
#include "WaveformRenderer.h"

EditPageWidget::EditPageWidget(SampleSession *session, QWidget *parent)
    : QWidget(parent), m_session(session) {
    setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);

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

    if (m_session) {
        connect(m_session, &SampleSession::waveformChanged, this, [this]() { update(); });
    }
}

void EditPageWidget::keyPressEvent(QKeyEvent *event) {
    if (m_params.isEmpty()) {
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

    auto clampValue = [](float value) {
        return qBound(0.0f, value, 1.0f);
    };

    if (key == Qt::Key_Left || key == Qt::Key_Minus) {
        m_params[m_selectedParam].value = clampValue(m_params[m_selectedParam].value - 0.02f);
        update();
        return;
    }
    if (key == Qt::Key_Right || key == Qt::Key_Plus || key == Qt::Key_Equal) {
        m_params[m_selectedParam].value = clampValue(m_params[m_selectedParam].value + 0.02f);
        update();
        return;
    }
    if (key == Qt::Key_Home) {
        m_params[m_selectedParam].value = 0.0f;
        update();
        return;
    }
    if (key == Qt::Key_End) {
        m_params[m_selectedParam].value = 1.0f;
        update();
        return;
    }
}

void EditPageWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);

    QPainter p(this);
    Theme::paintBackground(p, rect());

    const int margin = 24;
    const int headerHeight = 24;
    const QRectF headerRect(margin, margin, width() - 2 * margin, headerHeight);
    p.setPen(Theme::accent());
    p.setFont(Theme::condensedFont(12, QFont::Bold));
    p.drawText(headerRect, Qt::AlignLeft | Qt::AlignVCenter, "EDIT / SAMPLE");
    p.setPen(Theme::textMuted());
    p.setFont(Theme::baseFont(8));
    p.drawText(QRectF(headerRect.left(), headerRect.top(), headerRect.width(), headerRect.height()),
               Qt::AlignRight | Qt::AlignVCenter, "UP/DOWN select  LEFT/RIGHT adjust");

    const QRectF waveRect(margin, headerRect.bottom() + 10, width() - 2 * margin, height() * 0.42f);

    p.setBrush(Theme::bg1());
    p.setPen(QPen(Theme::stroke(), 1.2));
    p.drawRect(waveRect);

    const QRectF waveInner = waveRect.adjusted(12, 12, -12, -12);
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

    // Parameters grid.
    const QRectF gridRect(margin, waveRect.bottom() + 16, width() - 2 * margin,
                          height() - waveRect.bottom() - 96);
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

        const bool selected = (i == m_selectedParam);
        p.setBrush(selected ? Theme::bg2() : Theme::bg1());
        p.setPen(QPen(selected ? Theme::accentAlt() : Theme::stroke(), selected ? 1.6 : 1.0));
        p.drawRect(cell);

        const QRectF iconRect = cell.adjusted(14, 12, -14, -28);
        p.setPen(Qt::NoPen);
        p.setBrush(selected ? Theme::accent() : Theme::accentAlt());
        p.drawRect(iconRect.adjusted(6, 6, -6, -6));

        p.setPen(selected ? Theme::accentAlt() : Theme::text());
        p.drawText(QRectF(cell.left(), cell.bottom() - 26, cell.width(), 16),
                   Qt::AlignCenter, m_params[i].label);

        const QRectF valueLine(cell.left() + 18, cell.bottom() - 10, cell.width() - 36, 3);
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::withAlpha(Theme::stroke(), 160));
        p.drawRect(valueLine);
        p.setBrush(selected ? Theme::accent() : Theme::accentAlt());
        p.drawRect(QRectF(valueLine.left(), valueLine.top(), valueLine.width() * m_params[i].value,
                          valueLine.height()));

        p.setPen(Theme::textMuted());
        p.setFont(Theme::baseFont(8));
        const int percent = static_cast<int>(m_params[i].value * 100.0f);
        p.drawText(QRectF(cell.left(), cell.top() + 6, cell.width(), 12),
                   Qt::AlignCenter, QString::number(percent));
        p.setFont(Theme::baseFont(10, QFont::DemiBold));
    }

    // Action buttons.
    const QRectF buttonsRect(margin, height() - 58, width() - 2 * margin, 40);
    const QRectF deleteRect(buttonsRect.left(), buttonsRect.top(), buttonsRect.width() * 0.45f, 40);
    const QRectF copyRect(buttonsRect.right() - buttonsRect.width() * 0.45f, buttonsRect.top(),
                          buttonsRect.width() * 0.45f, 40);

    p.setFont(Theme::condensedFont(12, QFont::DemiBold));

    p.setBrush(Theme::bg1());
    p.setPen(QPen(Theme::accentAlt(), 1.2));
    p.drawRect(deleteRect);
    p.setPen(Theme::accentAlt());
    p.drawText(deleteRect, Qt::AlignCenter, "DELETE PAD");

    p.setBrush(Theme::bg1());
    p.setPen(QPen(Theme::accent(), 1.2));
    p.drawRect(copyRect);
    p.setPen(Theme::accent());
    p.drawText(copyRect, Qt::AlignCenter, "COPY PAD");
}
