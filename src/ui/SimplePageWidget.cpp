#include "SimplePageWidget.h"

#include <QPainter>

#include "Theme.h"

SimplePageWidget::SimplePageWidget(const QString &title, QWidget *parent)
    : QWidget(parent), m_title(title) {
    setAutoFillBackground(false);
}

void SimplePageWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    QLinearGradient bg(0, 0, 0, height());
    bg.setColorAt(0.0, Theme::bg0());
    bg.setColorAt(1.0, Theme::bg2());
    p.fillRect(rect(), bg);

    const QRectF card(width() * 0.12f, height() * 0.2f, width() * 0.76f, height() * 0.6f);
    p.setBrush(Theme::bg1());
    p.setPen(QPen(Theme::stroke(), 1.2));
    p.drawRoundedRect(card, 16, 16);

    p.setPen(Theme::text());
    p.setFont(Theme::condensedFont(36, QFont::Bold));
    p.drawText(card.adjusted(0, 20, 0, -20), Qt::AlignCenter, m_title);

    p.setPen(Qt::NoPen);
    p.setBrush(Theme::withAlpha(Theme::accentAlt(), 90));
    p.drawEllipse(QPointF(card.left() + 120, card.bottom() - 80), 46, 46);

    p.setBrush(Theme::withAlpha(Theme::accent(), 90));
    p.drawRect(QRectF(card.right() - 160, card.top() + 80, 80, 40));
}
