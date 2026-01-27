#include "TopToolbarWidget.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <cmath>

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
        const float level = qBound(0.0f, m_pads->busMeter(0), 1.0f);
        const float attack = 0.35f;
        const float release = 0.12f;
        const float coeff = (level > m_levelL) ? attack : release;
        m_levelL = m_levelL + (level - m_levelL) * coeff;
        m_levelR = m_levelL;
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
    Theme::applyRenderHints(p);
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

    // Master VU meter (arc)
    if (centerRect.width() > statsWidth + Theme::pxF(140.0f)) {
        const float meterLeft =
            statsRect.isNull() ? centerRect.left() : statsRect.right() + Theme::pxF(16.0f);
        const QRectF meterRect(meterLeft, centerRect.top(), Theme::px(140), centerRect.height());
        p.setBrush(Theme::bg1());
        p.setPen(QPen(Theme::stroke(), 1.0));
        p.drawRoundedRect(meterRect.adjusted(0, Theme::px(4), 0, -Theme::px(4)), Theme::px(8),
                          Theme::px(8));

        const QRectF arcRect(meterRect.left() + Theme::px(10), meterRect.top() + Theme::px(6),
                             meterRect.width() - Theme::px(20), meterRect.height() - Theme::px(12));
        p.setPen(QPen(Theme::textMuted(), 2.0));
        p.drawArc(arcRect, 210 * 16, 120 * 16);

        // Tick marks
        p.setPen(QPen(Theme::textMuted(), 1.4));
        for (int i = 0; i <= 6; ++i) {
            const float t = static_cast<float>(i) / 6.0f;
            const float ang = (210.0f + 120.0f * t) * static_cast<float>(M_PI) / 180.0f;
            const QPointF c = arcRect.center();
            const float r1 = arcRect.width() * 0.45f;
            const float r2 = r1 - Theme::pxF(6.0f);
            p.drawLine(QPointF(c.x() + std::cos(ang) * r1, c.y() + std::sin(ang) * r1),
                       QPointF(c.x() + std::cos(ang) * r2, c.y() + std::sin(ang) * r2));
        }

        // Needle based on master level
        const float level = qBound(0.0f, m_levelL, 1.0f);
        const float ang = (210.0f + 120.0f * level) * static_cast<float>(M_PI) / 180.0f;
        const QPointF c = arcRect.center();
        const float r = arcRect.width() * 0.42f;
        p.setPen(QPen(Theme::accentAlt(), 2.0, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(c, QPointF(c.x() + std::cos(ang) * r, c.y() + std::sin(ang) * r));
        p.setBrush(Theme::accentAlt());
        p.setPen(Qt::NoPen);
        p.drawEllipse(c, Theme::pxF(3.0f), Theme::pxF(3.0f));
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
