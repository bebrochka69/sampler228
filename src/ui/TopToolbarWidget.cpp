#include "TopToolbarWidget.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>

#include "PadBank.h"
#include "Theme.h"

TopToolbarWidget::TopToolbarWidget(PadBank *pads, QWidget *parent)
    : QWidget(parent), m_stats(this), m_pads(pads) {
    setFixedHeight(72);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_tabs << "SAMPLES" << "EDIT" << "SEQ" << "FX" << "ARRANGE";

    connect(&m_statsTimer, &QTimer::timeout, this, &TopToolbarWidget::updateStats);
    m_statsTimer.start(1000);
    updateStats();

    if (m_pads) {
        connect(m_pads, &PadBank::padChanged, this, [this](int) { update(); });
        connect(m_pads, &PadBank::activePadChanged, this, [this](int) { update(); });
    }

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

    QFont tabFont = Theme::condensedFont(12, QFont::Bold);
    QFontMetrics fm(tabFont);

    const int top = 8;
    const int bottom = top + 30;
    const int slant = 12;
    const int gap = 14;
    const int leftMargin = 14;

    int x = leftMargin;
    for (int i = 0; i < m_tabs.size(); ++i) {
        const int textWidth = fm.horizontalAdvance(m_tabs[i]);
        const int tabWidth = qMax(78, textWidth + 28);

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

void TopToolbarWidget::mousePressEvent(QMouseEvent *event) {
    const QPointF pos = event->position();

    for (int i = 0; i < m_padRects.size(); ++i) {
        if (m_padRects[i].contains(pos)) {
            if (m_pads) {
                m_pads->setActivePad(i);
            }
            return;
        }
    }

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
    Theme::paintBackground(p, rect());
    p.setPen(QPen(Theme::withAlpha(Theme::stroke(), 160), 1.4));
    p.drawRect(rect().adjusted(1, 1, -2, -2));
    p.setPen(QPen(Theme::withAlpha(Theme::accent(), 140), 1.0));
    p.drawLine(QPointF(2, height() - 2), QPointF(width() - 3, height() - 2));

    QFont tabFont = Theme::condensedFont(12, QFont::Bold);
    p.setFont(tabFont);

    for (int i = 0; i < m_tabPolys.size(); ++i) {
        const bool active = (i == m_activeIndex);
        const QRectF bounds = m_tabPolys[i].boundingRect();

        if (active) {
            QLinearGradient grad(bounds.topLeft(), bounds.bottomLeft());
            grad.setColorAt(0.0, Theme::accent());
            grad.setColorAt(1.0, Theme::withAlpha(Theme::accent(), 160));
            p.setBrush(grad);
            p.setPen(QPen(Theme::accent(), 1.4));
            p.drawPolygon(m_tabPolys[i]);
            p.setPen(Theme::bg0());
            p.drawText(bounds, Qt::AlignCenter, m_tabs[i]);
        } else {
            p.setPen(Theme::textMuted());
            p.setBrush(Qt::NoBrush);
            p.drawPolygon(m_tabPolys[i]);
            p.setPen(Theme::text());
            p.drawText(bounds, Qt::AlignCenter, m_tabs[i]);
        }
    }

    const int h = height();
    const int rightMargin = 14;
    const int bpmWidth = 124;
    const int bpmHeight = 34;
    m_bpmRect = QRectF(width() - rightMargin - bpmWidth, (h - bpmHeight) / 2.0, bpmWidth, bpmHeight);

    const int padSize = 14;
    const int padGap = 6;
    const int padCount = 8;
    const int padsWidth = padCount * padSize + (padCount - 1) * padGap;
    const QRectF padsRect(m_bpmRect.left() - padsWidth - 18, 14, padsWidth, 34);

    const int centerLeft = m_tabsWidth + 20;
    const int centerRight = static_cast<int>(padsRect.left()) - 16;
    const int centerWidth = qMax(0, centerRight - centerLeft);
    const QRectF centerRect(centerLeft, 8, centerWidth, h - 16);

    // CPU/RAM indicators.
    if (centerRect.width() > 180) {
        const QRectF statsRect(centerRect.left(), centerRect.top(), 170, centerRect.height());
        const float cpu = m_stats.cpuUsage();
        const float ram = m_stats.ramUsage();

        p.setPen(Theme::text());
        p.setFont(Theme::baseFont(9, QFont::DemiBold));
        const QRectF cpuLabel(statsRect.left(), statsRect.top() + 4, 48, 12);
        const QRectF ramLabel(statsRect.left(), statsRect.top() + 22, 48, 12);
        p.drawText(cpuLabel, Qt::AlignLeft | Qt::AlignVCenter, "CPU");
        p.drawText(ramLabel, Qt::AlignLeft | Qt::AlignVCenter, "RAM");

        const QRectF cpuBox(statsRect.left() + 48, statsRect.top() + 2, 94, 12);
        const QRectF ramBox(statsRect.left() + 48, statsRect.top() + 20, 94, 12);
        p.setPen(QPen(Theme::stroke(), 1.0));
        p.setBrush(Theme::bg1());
        p.drawRect(cpuBox);
        p.drawRect(ramBox);

        p.setPen(Qt::NoPen);
        p.setBrush(Theme::accent());
        p.drawRect(QRectF(cpuBox.left(), cpuBox.top(), cpuBox.width() * cpu, cpuBox.height()));
        p.setBrush(Theme::accentAlt());
        p.drawRect(QRectF(ramBox.left(), ramBox.top(), ramBox.width() * ram, ramBox.height()));

        p.setPen(Theme::textMuted());
        p.setFont(Theme::baseFont(8));
        p.drawText(QRectF(cpuBox.right() + 6, cpuBox.top(), 30, cpuBox.height()),
                   Qt::AlignLeft | Qt::AlignVCenter, QString::number(static_cast<int>(cpu * 100)));
        p.drawText(QRectF(ramBox.right() + 6, ramBox.top(), 30, ramBox.height()),
                   Qt::AlignLeft | Qt::AlignVCenter, QString::number(static_cast<int>(ram * 100)));
    }

    // Stereo meter outline (no simulated audio).
    if (centerRect.width() > 320) {
        const QRectF meterRect(centerRect.left() + 190, centerRect.top(), 120, centerRect.height());
        p.setPen(QPen(Theme::stroke(), 1.0));
        p.setBrush(Theme::bg1());
        p.drawRect(meterRect.adjusted(0, 2, 0, -2));

        p.setFont(Theme::baseFont(9, QFont::DemiBold));
        p.setPen(Theme::text());
        p.drawText(QRectF(meterRect.left() + 6, meterRect.top() + 6, 20, 12), Qt::AlignLeft, "L");
        p.drawText(QRectF(meterRect.left() + 60, meterRect.top() + 6, 20, 12), Qt::AlignLeft, "R");

        const QRectF lBar(meterRect.left() + 16, meterRect.bottom() - 12, 40, 6);
        const QRectF rBar(meterRect.left() + 62, meterRect.bottom() - 12, 40, 6);
        p.setPen(QPen(Theme::stroke(), 1.0));
        p.setBrush(Theme::bg2());
        p.drawRect(lBar);
        p.drawRect(rBar);
    }

    // Pad indicators.
    m_padRects.clear();
    p.setFont(Theme::baseFont(8, QFont::DemiBold));
    p.setPen(Theme::text());
    p.drawText(QRectF(padsRect.left(), padsRect.top() - 10, padsRect.width(), 10), Qt::AlignCenter,
               "PADS");

    for (int i = 0; i < padCount; ++i) {
        const float x = padsRect.left() + i * (padSize + padGap);
        const QRectF padRect(x, padsRect.top(), padSize, padSize);
        m_padRects.push_back(padRect);

        const bool loaded = m_pads ? m_pads->isLoaded(i) : false;
        const bool active = m_pads ? (m_pads->activePad() == i) : false;

        p.setBrush(loaded ? Theme::accent() : Theme::bg1());
        p.setPen(QPen(active ? Theme::accentAlt() : Theme::stroke(), 1.2));
        p.drawRect(padRect);
    }

    // BPM box.
    p.setBrush(Theme::bg1());
    p.setPen(QPen(Theme::accentAlt(), 1.4));
    p.drawRect(m_bpmRect);
    p.setFont(Theme::condensedFont(13, QFont::Bold));
    p.setPen(Theme::accentAlt());
    p.drawText(m_bpmRect, Qt::AlignCenter, QString("BPM %1").arg(m_bpm));
}
