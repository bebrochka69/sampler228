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

    Theme::paintBackground(p, rect());

    const QRectF card(width() * 0.12f, height() * 0.2f, width() * 0.76f, height() * 0.6f);
    p.setBrush(Theme::bg1());
    p.setPen(QPen(Theme::stroke(), 1.4));
    p.drawRect(card);

    p.setPen(Theme::text());
    p.setFont(Theme::condensedFont(36, QFont::Bold));
    p.drawText(card.adjusted(0, 20, 0, -20), Qt::AlignCenter, m_title);

    p.setPen(Qt::NoPen);
    p.setBrush(Theme::withAlpha(Theme::accentAlt(), 120));
    p.drawRect(QRectF(card.left() + 50, card.bottom() - 70, 110, 34));

    p.setBrush(Theme::withAlpha(Theme::accent(), 110));
    p.drawRect(QRectF(card.right() - 170, card.top() + 70, 110, 34));
}
