#include "PadHoldMenuOverlay.h"

#include <QMouseEvent>
#include <QPainter>

#include "PadBank.h"
#include "Theme.h"

PadHoldMenuOverlay::PadHoldMenuOverlay(PadBank *pads, QWidget *parent)
    : QWidget(parent), m_pads(pads) {
    setAutoFillBackground(false);
    setAttribute(Qt::WA_StyledBackground, true);
    setVisible(false);
}

void PadHoldMenuOverlay::showForPad(int pad) {
    m_activePad = qBound(0, pad, 7);
    if (parentWidget()) {
        setGeometry(parentWidget()->rect());
    }
    if (m_pads) {
        m_pads->setActivePad(m_activePad);
    }
    setVisible(true);
    raise();
    update();
}

void PadHoldMenuOverlay::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(Theme::withAlpha(Theme::bg0(), 235));
    p.setPen(Qt::NoPen);
    p.drawRect(rect());

    const float panelW = Theme::pxF(360.0f);
    const float panelH = Theme::pxF(200.0f);
    m_panelRect = QRectF((width() - panelW) * 0.5f, (height() - panelH) * 0.5f,
                         panelW, panelH);
    p.setBrush(Theme::bg1());
    p.setPen(QPen(Theme::stroke(), 1.2));
    p.drawRoundedRect(m_panelRect, Theme::px(14), Theme::px(14));

    const QRectF header(m_panelRect.left() + Theme::px(16),
                        m_panelRect.top() + Theme::px(10),
                        m_panelRect.width() - Theme::px(32),
                        Theme::px(26));
    p.setPen(Theme::accent());
    p.setFont(Theme::condensedFont(13, QFont::Bold));
    p.drawText(header, Qt::AlignLeft | Qt::AlignVCenter, "PAD MENU");

    const QString padName = m_pads ? m_pads->padName(m_activePad) : QString();
    p.setPen(Theme::textMuted());
    p.setFont(Theme::baseFont(9, QFont::DemiBold));
    p.drawText(header, Qt::AlignRight | Qt::AlignVCenter,
               QString("PAD %1  %2").arg(m_activePad + 1).arg(padName.left(16)));

    m_closeRect = QRectF(m_panelRect.right() - Theme::px(28),
                         m_panelRect.top() + Theme::px(10),
                         Theme::px(18), Theme::px(18));
    p.setPen(QPen(Theme::text(), 1.6));
    p.drawLine(m_closeRect.topLeft(), m_closeRect.bottomRight());
    p.drawLine(m_closeRect.topRight(), m_closeRect.bottomLeft());

    const float rowH = Theme::pxF(44.0f);
    const float left = m_panelRect.left() + Theme::px(18);
    float top = m_panelRect.top() + Theme::px(52);
    const float width = m_panelRect.width() - Theme::px(36);

    auto drawRow = [&](QRectF &rect, const QString &label, const QColor &fill) {
        rect = QRectF(left, top, width, rowH);
        p.setBrush(fill);
        p.setPen(QPen(Theme::stroke(), 1.0));
        p.drawRoundedRect(rect, Theme::px(8), Theme::px(8));
        p.setPen(Theme::bg0());
        p.setFont(Theme::condensedFont(12, QFont::Bold));
        p.drawText(rect, Qt::AlignCenter, label);
        top += rowH + Theme::px(10);
    };

    drawRow(m_pianoRect, "PIANO ROLL", Theme::accentAlt());
    drawRow(m_replaceRect, "REPLACE", Theme::accent());
    drawRow(m_cancelRect, "CANCEL", Theme::bg2());
}

void PadHoldMenuOverlay::mousePressEvent(QMouseEvent *event) {
    const QPointF pos = event->position();
    if (m_closeRect.contains(pos) || m_cancelRect.contains(pos)) {
        setVisible(false);
        emit closed();
        return;
    }
    if (m_pianoRect.contains(pos)) {
        setVisible(false);
        emit pianoRollRequested(m_activePad);
        return;
    }
    if (m_replaceRect.contains(pos)) {
        setVisible(false);
        emit replaceRequested(m_activePad);
        return;
    }
}
