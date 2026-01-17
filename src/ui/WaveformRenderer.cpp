#include "WaveformRenderer.h"

#include <QPainter>
void WaveformRenderer::drawWaveform(QPainter &p, const QRectF &rect, const QVector<float> &samples,
                                    const QColor &lineColor, const QColor &midColor) {
    if (samples.isEmpty() || rect.width() <= 1.0 || rect.height() <= 1.0) {
        return;
    }

    p.save();
    p.setClipRect(rect);

    const float midY = rect.center().y();
    const float amp = rect.height() * 0.42f;

    QPen linePen(lineColor, 1.2);
    p.setPen(linePen);

    const int count = samples.size();
    for (int i = 0; i < count; ++i) {
        const float x = rect.left() + (rect.width() * i) / (count - 1);
        const float v = samples[i];
        const float y1 = midY - v * amp;
        const float y2 = midY + v * amp;
        p.drawLine(QPointF(x, y1), QPointF(x, y2));
    }

    QPen midPen(midColor, 1.0);
    p.setPen(midPen);
    p.drawLine(QPointF(rect.left(), midY), QPointF(rect.right(), midY));

    p.restore();
}
