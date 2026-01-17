#pragma once

#include <QColor>
#include <QRectF>
#include <QVector>

class QPainter;

namespace WaveformRenderer {
QVector<float> makeDemoWave(int count, quint32 seed);
void drawWaveform(QPainter &p, const QRectF &rect, const QVector<float> &samples,
                  const QColor &lineColor, const QColor &midColor);
}
