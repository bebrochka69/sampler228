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

    const int h = height();
    const int top = 10;
    const int bottom = top + 26;
    const int slant = 10;
    const int gap = 14;
    const int leftMargin = 14;

    int x = leftMargin;
    for (int i = 0; i < m_tabs.size(); ++i) {
        const int textWidth = fm.horizontalAdvance(m_tabs[i]);
        const int tabWidth = qMax(72, textWidth + 24);

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
    p.setRenderHint(QPainter::Antialiasing, true);

    p.fillRect(rect(), Theme::bg0());
    p.setPen(QPen(Theme::stroke(), 1.2));
    p.drawRect(rect().adjusted(0, 0, -1, -1));

    QFont tabFont = Theme::condensedFont(12, QFont::Bold);
    p.setFont(tabFont);

    for (int i = 0; i < m_tabPolys.size(); ++i) {
        const bool active = (i == m_activeIndex);
        const QRectF bounds = m_tabPolys[i].boundingRect();

        if (active) {
            p.setBrush(Theme::accent());
            p.setPen(QPen(Theme::accent(), 1.2));
            p.drawPolygon(m_tabPolys[i]);
            p.setPen(Theme::bg0());
            p.drawText(bounds, Qt::AlignCenter, m_tabs[i]);
        } else {
            p.setPen(Theme::text());
            p.drawText(bounds, Qt::AlignCenter, m_tabs[i]);
        }
    }

    const int h = height();
    const int rightMargin = 14;
    const int bpmWidth = 110;
    const int bpmHeight = 30;
    m_bpmRect = QRectF(width() - rightMargin - bpmWidth, (h - bpmHeight) / 2.0, bpmWidth, bpmHeight);

    const int padSize = 12;
    const int padGap = 6;
    const int padCount = 8;
    const int padsWidth = padCount * padSize + (padCount - 1) * padGap;
    const QRectF padsRect(m_bpmRect.left() - padsWidth - 18, 16, padsWidth, 32);

    const int centerLeft = m_tabsWidth + 20;
    const int centerRight = static_cast<int>(padsRect.left()) - 16;
    const int centerWidth = qMax(0, centerRight - centerLeft);
    const QRectF centerRect(centerLeft, 8, centerWidth, h - 16);

    // CPU/RAM indicators.
    if (centerRect.width() > 180) {
        const QRectF statsRect(centerRect.left(), centerRect.top(), 160, centerRect.height());
        const float cpu = m_stats.cpuUsage();
        const float ram = m_stats.ramUsage();

        p.setPen(Theme::text());
        p.setFont(Theme::baseFont(9, QFont::DemiBold));
        p.drawText(QPointF(statsRect.left(), statsRect.top() + 10),
                   QString("CPU %1%").arg(static_cast<int>(cpu * 100)));
        p.drawText(QPointF(statsRect.left(), statsRect.top() + 26),
                   QString("RAM %1%").arg(static_cast<int>(ram * 100)));

        const QRectF cpuBar(statsRect.left() + 70, statsRect.top() + 2, 80, 8);
        const QRectF ramBar(statsRect.left() + 70, statsRect.top() + 18, 80, 8);
        p.setPen(QPen(Theme::stroke(), 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRect(cpuBar);
        p.drawRect(ramBar);

        p.setPen(Qt::NoPen);
        p.setBrush(Theme::accent());
        p.drawRect(QRectF(cpuBar.left(), cpuBar.top(), cpuBar.width() * cpu, cpuBar.height()));
        p.setBrush(Theme::accentAlt());
        p.drawRect(QRectF(ramBar.left(), ramBar.top(), ramBar.width() * ram, ramBar.height()));
    }

    // Stereo meter outline (no simulated audio).
    if (centerRect.width() > 320) {
        const QRectF meterRect(centerRect.left() + 180, centerRect.top(), 120, centerRect.height());
        p.setPen(QPen(Theme::stroke(), 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRect(meterRect.adjusted(0, 2, 0, -2));

        p.setFont(Theme::baseFont(9, QFont::DemiBold));
        p.setPen(Theme::text());
        p.drawText(QRectF(meterRect.left() + 6, meterRect.top() + 6, 20, 12), Qt::AlignLeft, "L");
        p.drawText(QRectF(meterRect.left() + 60, meterRect.top() + 6, 20, 12), Qt::AlignLeft, "R");

        const QRectF lBar(meterRect.left() + 16, meterRect.bottom() - 12, 40, 6);
        const QRectF rBar(meterRect.left() + 62, meterRect.bottom() - 12, 40, 6);
        p.setPen(QPen(Theme::stroke(), 1.0));
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
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(Theme::accentAlt(), 1.2));
    p.drawRect(m_bpmRect);
    p.setFont(Theme::condensedFont(12, QFont::Bold));
    p.setPen(Theme::accentAlt());
    p.drawText(m_bpmRect, Qt::AlignCenter, QString("BPM %1").arg(m_bpm));
}
