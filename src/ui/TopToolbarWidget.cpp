#include "TopToolbarWidget.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include "PadBank.h"
#include "Theme.h"

TopToolbarWidget::TopToolbarWidget(PadBank *pads, QWidget *parent)
    : QWidget(parent), m_stats(this), m_pads(pads) {
    setFixedHeight(Theme::px(72));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_tabs << "SAMPLES" << "EDIT" << "SEQ" << "FX" << "ARRANGE";

    connect(&m_statsTimer, &QTimer::timeout, this, &TopToolbarWidget::updateStats);
    m_statsTimer.start(2000);
    updateStats();

    if (m_pads) {
        connect(m_pads, &PadBank::padChanged, this, [this](int) { update(); });
        connect(m_pads, &PadBank::activePadChanged, this, [this](int) { update(); });
        connect(m_pads, &PadBank::bpmChanged, this, [this](int bpm) {
            m_bpm = bpm;
            update();
        });
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

    const int top = Theme::px(8);
    const int bottom = top + Theme::px(30);
    const int slant = Theme::px(12);
    const int gap = Theme::px(14);
    const int leftMargin = Theme::px(14);

    int x = leftMargin;
    for (int i = 0; i < m_tabs.size(); ++i) {
        const int textWidth = fm.horizontalAdvance(m_tabs[i]);
        const int tabWidth = qMax(Theme::px(78), textWidth + Theme::px(28));

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
    if (m_pads) {
        m_bpm = m_pads->bpm();
    }
    update();
}

void TopToolbarWidget::mousePressEvent(QMouseEvent *event) {
    const QPointF pos = event->position();

    if (m_bpmRect.contains(pos)) {
        const bool shift = event->modifiers().testFlag(Qt::ShiftModifier);
        const int delta = shift ? 5 : 1;
        adjustBpm(event->button() == Qt::RightButton ? -delta : delta);
        return;
    }

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

void TopToolbarWidget::wheelEvent(QWheelEvent *event) {
    if (!m_bpmRect.contains(event->position())) {
        QWidget::wheelEvent(event);
        return;
    }

    const int delta = event->angleDelta().y() > 0 ? 1 : -1;
    adjustBpm(delta);
    event->accept();
}

void TopToolbarWidget::adjustBpm(int delta) {
    const int next = qBound(30, m_bpm + delta, 300);
    if (next == m_bpm) {
        return;
    }
    m_bpm = next;
    if (m_pads) {
        m_pads->setBpm(next);
    }
    update();
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
    const int rightMargin = Theme::px(14);
    const int bpmWidth = Theme::px(124);
    const int bpmHeight = Theme::px(34);
    m_bpmRect = QRectF(width() - rightMargin - bpmWidth, (h - bpmHeight) / 2.0, bpmWidth, bpmHeight);

    const int padSize = Theme::px(14);
    const int padGap = Theme::px(6);
    const int padCount = 8;
    const int padsWidth = padCount * padSize + (padCount - 1) * padGap;
    const QRectF padsRect(m_bpmRect.left() - padsWidth - Theme::px(18),
                          Theme::px(14), padsWidth, Theme::px(34));

    const int centerLeft = m_tabsWidth + Theme::px(20);
    const int centerRight = static_cast<int>(padsRect.left()) - Theme::px(16);
    const int centerWidth = qMax(0, centerRight - centerLeft);
    const QRectF centerRect(centerLeft, Theme::px(8), centerWidth, h - Theme::px(16));

    // CPU/RAM/LOAD indicators (always visible).
    const float statsWidth = Theme::pxF(220.0f);
    QRectF statsRect;
    if (centerRect.width() > 90.0f) {
        const float width = qMin(statsWidth, centerRect.width());
        statsRect = QRectF(centerRect.left(), centerRect.top(), width, centerRect.height());
    } else {
        const qreal fallbackWidth = qMax<qreal>(0.0, width() - Theme::pxF(28.0f));
        statsRect = QRectF(Theme::px(14), height() - Theme::px(24),
                           qMin(statsWidth, fallbackWidth), Theme::px(18));
    }

    if (statsRect.width() > 60.0f) {
        const float cpu = m_stats.cpuUsage();
        const float ram = m_stats.ramUsage();
        const float load = m_stats.loadUsage();

        p.setPen(Theme::text());
        p.setFont(Theme::baseFont(9, QFont::DemiBold));
        QString statsText =
            QString("CPU %1%  RAM %2%  LOAD %3%")
                .arg(static_cast<int>(cpu * 100))
                .arg(static_cast<int>(ram * 100))
                .arg(static_cast<int>(load * 100));
        QFontMetrics fm(p.font());
        statsText = fm.elidedText(statsText, Qt::ElideRight, static_cast<int>(statsRect.width()));
        p.drawText(statsRect, Qt::AlignLeft | Qt::AlignVCenter, statsText);
    }

    // Stereo meter outline (no simulated audio).
    if (centerRect.width() > statsWidth + Theme::pxF(140.0f)) {
        const float meterLeft =
            statsRect.isNull() ? centerRect.left() : statsRect.right() + Theme::pxF(16.0f);
        const QRectF meterRect(meterLeft, centerRect.top(), Theme::px(120), centerRect.height());
        p.setPen(QPen(Theme::stroke(), 1.0));
        p.setBrush(Theme::bg1());
        p.drawRect(meterRect.adjusted(0, Theme::px(2), 0, -Theme::px(2)));

        p.setFont(Theme::baseFont(9, QFont::DemiBold));
        p.setPen(Theme::text());
        p.drawText(QRectF(meterRect.left() + Theme::px(6), meterRect.top() + Theme::px(6),
                          Theme::px(20), Theme::px(12)),
                   Qt::AlignLeft, "L");
        p.drawText(QRectF(meterRect.left() + Theme::px(60), meterRect.top() + Theme::px(6),
                          Theme::px(20), Theme::px(12)),
                   Qt::AlignLeft, "R");

        const QRectF lBar(meterRect.left() + Theme::px(16),
                          meterRect.bottom() - Theme::px(12), Theme::px(40), Theme::px(6));
        const QRectF rBar(meterRect.left() + Theme::px(62),
                          meterRect.bottom() - Theme::px(12), Theme::px(40), Theme::px(6));
        p.setPen(QPen(Theme::stroke(), 1.0));
        p.setBrush(Theme::bg2());
        p.drawRect(lBar);
        p.drawRect(rBar);
    }

    // Pad indicators.
    m_padRects.clear();
    p.setFont(Theme::baseFont(8, QFont::DemiBold));
    p.setPen(Theme::text());
    p.drawText(QRectF(padsRect.left(), padsRect.top() - Theme::px(10), padsRect.width(),
                      Theme::px(10)),
               Qt::AlignCenter,
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
