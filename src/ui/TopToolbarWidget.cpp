#include "TopToolbarWidget.h"

#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QRandomGenerator>
#include <QtMath>

#include "Theme.h"

TopToolbarWidget::TopToolbarWidget(QWidget *parent)
    : QWidget(parent), m_stats(this) {
    setFixedHeight(72);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_tabs << "SAMPLE" << "EDIT" << "SEQ" << "FX" << "ARRANG";
    m_padLoaded.resize(8);
    for (int i = 0; i < m_padLoaded.size(); ++i) {
        m_padLoaded[i] = (i % 3 != 0);
    }

    connect(&m_statsTimer, &QTimer::timeout, this, &TopToolbarWidget::updateStats);
    m_statsTimer.start(1000);

    connect(&m_meterTimer, &QTimer::timeout, this, &TopToolbarWidget::tickMeters);
    m_meterTimer.start(33);

    rebuildTabs();
}

void TopToolbarWidget::setActiveIndex(int index) {
    if (index < 0 || index >= m_tabs.size()) {
        return;
    }
    if (m_activeIndex == index) {
        return;
    }
    m_activeIndex = index;
    update();
}

void TopToolbarWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    rebuildTabs();
}

void TopToolbarWidget::rebuildTabs() {
    m_tabPolys.clear();

    const int h = height();
    const int top = 8;
    const int bottom = h - 8;
    const int slant = 14;
    const int gap = 8;
    const int leftMargin = 16;

    const int tabCount = m_tabs.size();
    const int maxWidth = static_cast<int>(width() * 0.48f);
    int tabWidth = (maxWidth - gap * (tabCount - 1)) / tabCount;
    tabWidth = qBound(88, tabWidth, 124);

    int x = leftMargin;
    for (int i = 0; i < tabCount; ++i) {
        QPolygonF poly;
        poly << QPointF(x + slant, top)
             << QPointF(x + tabWidth, top)
             << QPointF(x + tabWidth - slant, bottom)
             << QPointF(x, bottom);
        m_tabPolys.push_back(poly);
        x += tabWidth + gap;
    }

    m_tabsWidth = x - leftMargin - gap;
}

void TopToolbarWidget::updateStats() {
    m_stats.update();
    update();
}

void TopToolbarWidget::tickMeters() {
    // Lightweight animation so the meters feel alive without real audio input.
    m_phase += 0.045f;

    const float targetL = 0.25f + 0.55f * qAbs(qSin(m_phase * 1.2f));
    const float targetR = 0.22f + 0.55f * qAbs(qSin(m_phase * 1.15f + 0.7f));

    const float jitterL = static_cast<float>(QRandomGenerator::global()->generateDouble() - 0.5) * 0.08f;
    const float jitterR = static_cast<float>(QRandomGenerator::global()->generateDouble() - 0.5) * 0.08f;

    m_levelL = qBound(0.0f, m_levelL * 0.82f + (targetL + jitterL) * 0.18f, 1.0f);
    m_levelR = qBound(0.0f, m_levelR * 0.82f + (targetR + jitterR) * 0.18f, 1.0f);

    if (m_levelL > m_peakL) {
        m_peakL = m_levelL;
    } else {
        m_peakL = qMax(0.0f, m_peakL - 0.008f);
    }

    if (m_levelR > m_peakR) {
        m_peakR = m_levelR;
    } else {
        m_peakR = qMax(0.0f, m_peakR - 0.008f);
    }

    if (m_levelL > 0.95f) {
        m_clipHoldL = 8;
    } else if (m_clipHoldL > 0) {
        --m_clipHoldL;
    }

    if (m_levelR > 0.95f) {
        m_clipHoldR = 8;
    } else if (m_clipHoldR > 0) {
        --m_clipHoldR;
    }

    update();
}

void TopToolbarWidget::mousePressEvent(QMouseEvent *event) {
    const QPointF pos = event->position();
    for (int i = 0; i < m_tabPolys.size(); ++i) {
        if (m_tabPolys[i].containsPoint(pos, Qt::OddEvenFill)) {
            setActiveIndex(i);
            emit pageSelected(i);
            break;
        }
    }
}

void TopToolbarWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    QLinearGradient bg(0, 0, 0, height());
    bg.setColorAt(0.0, Theme::bg2());
    bg.setColorAt(1.0, Theme::bg0());
    p.fillRect(rect(), bg);

    p.setPen(QPen(Theme::stroke(), 1.0));
    p.drawLine(QPointF(0, height() - 1), QPointF(width(), height() - 1));

    QFont tabFont = Theme::condensedFont(12, QFont::DemiBold);
    p.setFont(tabFont);

    for (int i = 0; i < m_tabPolys.size(); ++i) {
        const bool active = (i == m_activeIndex);
        QColor fill = active ? Theme::accent() : Theme::bg3();
        QColor text = active ? Theme::bg0() : Theme::text();
        QColor outline = active ? Theme::accent() : Theme::stroke();

        p.setBrush(fill);
        p.setPen(QPen(outline, 1.2));
        p.drawPolygon(m_tabPolys[i]);

        p.setPen(text);
        p.drawText(m_tabPolys[i].boundingRect(), Qt::AlignCenter, m_tabs[i]);
    }

    const int h = height();
    const int rightReserve = h * 2 + 140;
    const int leftStart = m_tabsWidth + 24;
    const int centerWidth = qMax(180, width() - leftStart - rightReserve);
    const QRectF centerRect(leftStart, 0, centerWidth, h);

    // CPU/RAM section.
    const QRectF statsRect(centerRect.left() + 10, 10, 150, h - 20);
    p.setPen(Theme::textMuted());
    p.setFont(Theme::baseFont(9, QFont::DemiBold));

    const float cpu = m_stats.cpuUsage();
    const float ram = m_stats.ramUsage();

    const QRectF cpuBar(statsRect.left(), statsRect.top() + 12, 120, 6);
    const QRectF ramBar(statsRect.left(), statsRect.top() + 32, 120, 6);

    p.drawText(QPointF(statsRect.left(), statsRect.top() + 8), "CPU");
    p.drawText(QPointF(statsRect.left(), statsRect.top() + 28), "RAM");

    p.setBrush(Theme::withAlpha(Theme::stroke(), 160));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(cpuBar, 2, 2);
    p.drawRoundedRect(ramBar, 2, 2);

    p.setBrush(Theme::accent());
    p.drawRoundedRect(QRectF(cpuBar.left(), cpuBar.top(), cpuBar.width() * cpu, cpuBar.height()), 2, 2);
    p.setBrush(Theme::accentAlt());
    p.drawRoundedRect(QRectF(ramBar.left(), ramBar.top(), ramBar.width() * ram, ramBar.height()), 2, 2);

    p.setPen(Theme::textMuted());
    p.setFont(Theme::baseFont(9));
    p.drawText(QPointF(cpuBar.right() + 6, cpuBar.bottom() + 1),
               QString::number(static_cast<int>(cpu * 100)) + "%");
    p.drawText(QPointF(ramBar.right() + 6, ramBar.bottom() + 1),
               QString::number(static_cast<int>(ram * 100)) + "%");

    // Stereo meter section.
    const QRectF meterRect(statsRect.right() + 40, 8, 120, h - 16);
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::withAlpha(Theme::stroke(), 180));
    p.drawRoundedRect(meterRect, 6, 6);

    const float meterPadding = 10.0f;
    const float barWidth = 18.0f;
    const float barGap = 14.0f;
    const float barHeight = meterRect.height() - 2 * meterPadding;
    const float barTop = meterRect.top() + meterPadding;
    const float barLeft = meterRect.left() + 18.0f;

    auto drawMeterBar = [&](float level, float peak, bool clip, float x) {
        const QRectF bar(x, barTop, barWidth, barHeight);
        p.setBrush(Theme::bg1());
        p.drawRoundedRect(bar, 4, 4);

        const float fillHeight = barHeight * level;
        const QRectF fill(bar.left(), bar.bottom() - fillHeight, barWidth, fillHeight);
        QLinearGradient grad(fill.topLeft(), fill.bottomLeft());
        grad.setColorAt(0.0, Theme::danger());
        grad.setColorAt(0.4, Theme::warn());
        grad.setColorAt(1.0, Theme::accentAlt());
        p.setBrush(grad);
        p.drawRoundedRect(fill, 4, 4);

        const float peakY = bar.bottom() - barHeight * peak;
        p.setPen(QPen(Theme::text(), 2));
        p.drawLine(QPointF(bar.left(), peakY), QPointF(bar.right(), peakY));

        if (clip) {
            p.setPen(Qt::NoPen);
            p.setBrush(Theme::danger());
            p.drawRect(QRectF(bar.left(), bar.top() - 6, barWidth, 4));
        }
    };

    drawMeterBar(m_levelL, m_peakL, m_clipHoldL > 0, barLeft);
    drawMeterBar(m_levelR, m_peakR, m_clipHoldR > 0, barLeft + barWidth + barGap);

    p.setPen(Theme::textMuted());
    p.setFont(Theme::baseFont(9, QFont::DemiBold));
    p.drawText(QRectF(barLeft - 2, meterRect.top() + 2, barWidth + 4, 12), Qt::AlignCenter, "L");
    p.drawText(QRectF(barLeft + barWidth + barGap - 2, meterRect.top() + 2, barWidth + 4, 12),\n               Qt::AlignCenter, "R");

    const float zeroDbY = barTop + barHeight * 0.2f;
    p.setPen(QPen(Theme::textMuted(), 1.0));
    p.drawLine(QPointF(meterRect.left() + 6, zeroDbY), QPointF(meterRect.right() - 6, zeroDbY));
    p.drawText(QPointF(meterRect.right() - 34, zeroDbY - 2), "0 dB");

    // Pad indicators.
    const int arcSize = h * 2;
    const int padAreaRight = width() - arcSize - 12;
    const QRectF padRect(padAreaRight - 150, 10, 140, h - 20);

    const int cols = 4;
    const int rows = 2;
    const float size = 16.0f;
    const float padGap = 10.0f;
    const float startX = padRect.left() + 10.0f;
    const float startY = padRect.top() + 6.0f;

    p.setFont(Theme::baseFont(9, QFont::DemiBold));
    p.setPen(Theme::textMuted());
    p.drawText(QPointF(padRect.left(), padRect.bottom() + 2), "PADS");

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const int idx = r * cols + c;
            if (idx >= m_padLoaded.size()) {
                continue;
            }
            const float x = startX + c * (size + padGap);
            const float y = startY + r * (size + padGap);
            const QRectF box(x, y, size, size);
            p.setBrush(m_padLoaded[idx] ? Theme::accentAlt() : Theme::bg1());
            p.setPen(QPen(Theme::stroke(), 1.0));
            p.drawRoundedRect(box, 3, 3);
        }
    }

    // Right bar lead-in toward the arc.
    const QRectF barRect(padAreaRight + 6, h * 0.5f - 2.0f, arcSize - 8.0f, 4.0f);
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::accent());
    p.drawRoundedRect(barRect, 2, 2);
}
