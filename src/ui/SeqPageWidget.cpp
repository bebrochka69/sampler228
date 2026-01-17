#include "SeqPageWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include "Theme.h"

SeqPageWidget::SeqPageWidget(QWidget *parent) : QWidget(parent) {
    setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);

    m_padColors = {Theme::accent(), Theme::accentAlt(), QColor(110, 170, 255), QColor(255, 188, 64),
                   QColor(210, 120, 255), QColor(90, 220, 120), QColor(255, 90, 110), QColor(120, 200, 210)};

    for (int pad = 0; pad < 8; ++pad) {
        for (int step = 0; step < 64; ++step) {
            m_steps[pad][step] = false;
        }
    }
}

QRectF SeqPageWidget::gridRect() const {
    const int margin = 24;
    const float heightRatio = 0.58f;
    return QRectF(margin, margin, width() - 2 * margin, height() * heightRatio);
}

QRectF SeqPageWidget::padsRect() const {
    const QRectF grid = gridRect();
    return QRectF(grid.left(), grid.bottom() + 28, grid.width(), 110);
}

void SeqPageWidget::mousePressEvent(QMouseEvent *event) {
    const QPointF pos = event->position();
    const QRectF pads = padsRect();

    if (pads.contains(pos)) {
        const int cols = 8;
        const float padW = pads.width() / cols;
        const int idx = static_cast<int>((pos.x() - pads.left()) / padW);
        if (idx >= 0 && idx < 8) {
            m_activePad = idx;
            update();
        }
        return;
    }

    const QRectF grid = gridRect();
    if (!grid.contains(pos)) {
        return;
    }

    const int cols = 16;
    const int rows = 4;
    const float cellW = grid.width() / cols;
    const float cellH = grid.height() / rows;

    const int col = static_cast<int>((pos.x() - grid.left()) / cellW);
    const int row = static_cast<int>((pos.y() - grid.top()) / cellH);
    const int step = row * cols + col;
    if (step < 0 || step >= 64) {
        return;
    }

    if (event->modifiers().testFlag(Qt::ShiftModifier)) {
        const bool nextState = !m_steps[m_activePad][step];
        for (int i = 0; i < 64; ++i) {
            if (i % 2 == step % 2) {
                m_steps[m_activePad][i] = nextState;
            }
        }
    } else {
        m_steps[m_activePad][step] = !m_steps[m_activePad][step];
    }

    update();
}

void SeqPageWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);

    QPainter p(this);

    p.fillRect(rect(), Theme::bg0());

    const QRectF grid = gridRect();
    const int cols = 16;
    const int rows = 4;
    const float cellW = grid.width() / cols;
    const float cellH = grid.height() / rows;

    const QColor groupA = Theme::bg1();
    const QColor groupB = Theme::withAlpha(Theme::stroke(), 40);

    // Background groups.
    for (int col = 0; col < cols; ++col) {
        const int group = col / 4;
        const QColor groupColor = (group % 2 == 0) ? groupA : groupB;
        const QRectF groupRect(grid.left() + col * cellW, grid.top(), cellW, grid.height());
        p.fillRect(groupRect, groupColor);
    }

    // Grid and notes.
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const QRectF cell(grid.left() + col * cellW, grid.top() + row * cellH, cellW, cellH);
            const QRectF box = cell.adjusted(6, 6, -6, -6);
            const int step = row * cols + col;

            p.setPen(QPen(Theme::stroke(), 1.0));
            p.setBrush(Qt::NoBrush);
            p.drawRect(box);

            for (int pad = 0; pad < 8; ++pad) {
                if (pad == m_activePad) {
                    continue;
                }
                if (!m_steps[pad][step]) {
                    continue;
                }
                QColor ghost = m_padColors[pad];
                ghost.setAlpha(70);
                const QRectF ghostBox = box.adjusted(6, 6, -6, -6);
                p.setBrush(ghost);
                p.setPen(Qt::NoPen);
                p.drawRect(ghostBox);
            }

            if (m_steps[m_activePad][step]) {
                p.setBrush(m_padColors[m_activePad]);
                p.setPen(Qt::NoPen);
                p.drawRect(box.adjusted(3, 3, -3, -3));
            }
        }
    }

    // Pad input row.
    const QRectF pads = padsRect();
    const float padW = pads.width() / 8.0f;
    const float padH = 64.0f;
    p.setFont(Theme::baseFont(10, QFont::DemiBold));

    for (int i = 0; i < 8; ++i) {
        const QRectF padRect(pads.left() + i * padW + 6, pads.top() + 8, padW - 12, padH);
        const bool active = (i == m_activePad);
        p.setBrush(active ? m_padColors[i] : Theme::bg1());
        p.setPen(QPen(active ? Theme::accentAlt() : Theme::stroke(), 1.2));
        p.drawRect(padRect);

        p.setPen(active ? Theme::bg0() : Theme::textMuted());
        p.drawText(padRect, Qt::AlignCenter, QString("PAD %1").arg(i + 1));
    }

}
