#pragma once

#include <QColor>
#include <QRectF>
#include <QVector>

class QPainter;

namespace WaveformRenderer {
void drawWaveform(QPainter &p, const QRectF &rect, const QVector<float> &samples,
                  const QColor &lineColor, const QColor &midColor);
}
