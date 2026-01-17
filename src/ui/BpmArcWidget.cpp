#include "BpmArcWidget.h"

#include <QPainter>

#include "Theme.h"

BpmArcWidget::BpmArcWidget(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void BpmArcWidget::setBpm(int bpm) {
    if (m_bpm == bpm) {
        return;
    }
    m_bpm = bpm;
    update();
}

void BpmArcWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int r = width();
    const QRectF circleRect(0, -r, 2.0 * r, 2.0 * r);

    QLinearGradient grad(QPointF(r * 0.2, 0), QPointF(r * 1.2, r));
    grad.setColorAt(0.0, Theme::accent());
    grad.setColorAt(1.0, Theme::accentAlt());

    p.setPen(Qt::NoPen);
    p.setBrush(grad);
    p.drawPie(circleRect, 180 * 16, -90 * 16);

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(Theme::stroke(), 2.0));
    p.drawArc(circleRect, 180 * 16, -90 * 16);

    const QRectF textRect(width() * 0.2, height() * 0.2, width() * 0.6, height() * 0.6);
    p.setPen(Theme::bg0());
    p.setFont(Theme::condensedFont(28, QFont::Bold));
    p.drawText(textRect, Qt::AlignCenter, QString::number(m_bpm));

    p.setPen(Theme::bg0());
    p.setFont(Theme::baseFont(10, QFont::DemiBold));
    const QRectF bpmLabel(textRect.left(), textRect.bottom() - 10, textRect.width(), 12);
    p.drawText(bpmLabel, Qt::AlignCenter, "BPM");
}
