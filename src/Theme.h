#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QFont>
#include <QFontInfo>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPixmap>
#include <QRandomGenerator>
#include <QtGlobal>
#include <cmath>

namespace Theme {
inline QColor bg0() { return QColor(18, 16, 24); }
inline QColor bg1() { return QColor(34, 30, 44); }
inline QColor bg2() { return QColor(26, 22, 34); }
inline QColor bg3() { return QColor(46, 40, 60); }
inline QColor stroke() { return QColor(170, 160, 200); }
inline QColor accent() { return QColor(210, 214, 255); }
inline QColor accentAlt() { return QColor(244, 198, 204); }
inline QColor text() { return QColor(244, 240, 255); }
inline QColor textMuted() { return QColor(186, 178, 204); }
inline QColor warn() { return QColor(255, 210, 160); }
inline QColor danger() { return QColor(255, 160, 170); }

inline QFont baseFont(int pt, QFont::Weight weight = QFont::Normal) {
    QFont f("DejaVu Serif");
    if (!QFontInfo(f).exactMatch()) {
        f = QFont("DejaVu Sans");
    }
    f.setPointSize(pt);
    f.setWeight(weight);
    return f;
}

inline QFont condensedFont(int pt, QFont::Weight weight = QFont::DemiBold) {
    QFont f("DejaVu Sans");
    if (!QFontInfo(f).exactMatch()) {
        f = QFont("DejaVu Sans");
    }
    f.setPointSize(pt);
    f.setWeight(weight);
    return f;
}

inline QColor withAlpha(const QColor &c, int alpha) {
    QColor out = c;
    out.setAlpha(alpha);
    return out;
}

inline bool liteMode() {
    return qEnvironmentVariableIsSet("GROOVEBOX_LITE");
}

inline float timeSeconds() {
    static QElapsedTimer timer;
    if (!timer.isValid()) {
        timer.start();
    }
    return static_cast<float>(timer.elapsed()) / 1000.0f;
}

inline const QImage &grainImage() {
    static QImage img = []() {
        QImage out(256, 256, QImage::Format_ARGB32_Premultiplied);
        QRandomGenerator rng(0xC0FFEEu);
        for (int y = 0; y < out.height(); ++y) {
            QRgb *row = reinterpret_cast<QRgb *>(out.scanLine(y));
            for (int x = 0; x < out.width(); ++x) {
                const int v = static_cast<int>(rng.generate() % 255);
                row[x] = qRgba(v, v, v, 50);
            }
        }
        return out;
    }();
    return img;
}

inline void drawFog(QPainter &p, const QRectF &rect, const QColor &color, float opacity,
                    float speed, float scale) {
    const float t = timeSeconds() * speed;
    p.save();
    p.setOpacity(opacity);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    const float w = rect.width();
    const float h = rect.height();
    const QPointF c = rect.center();
    const QPointF p1(c.x() + std::sin(t * 0.7f) * w * 0.18f,
                     c.y() + std::cos(t * 0.5f) * h * 0.14f);
    const QPointF p2(c.x() + std::cos(t * 0.6f + 1.5f) * w * 0.22f,
                     c.y() + std::sin(t * 0.4f + 0.8f) * h * 0.18f);
    const QPointF p3(c.x() + std::sin(t * 0.4f + 2.4f) * w * 0.2f,
                     c.y() + std::cos(t * 0.3f + 1.9f) * h * 0.2f);
    p.drawEllipse(p1, w * 0.35f * scale, h * 0.28f * scale);
    p.drawEllipse(p2, w * 0.42f * scale, h * 0.32f * scale);
    p.drawEllipse(p3, w * 0.3f * scale, h * 0.24f * scale);
    p.restore();
}

inline void drawGrain(QPainter &p, const QRectF &rect, float opacity) {
    p.save();
    p.setOpacity(opacity);
    p.setCompositionMode(QPainter::CompositionMode_Overlay);
    QPixmap pix = QPixmap::fromImage(grainImage());
    const float t = timeSeconds() * 12.0f;
    const QPointF offset(std::fmod(t * 6.0f, pix.width()), std::fmod(t * 4.0f, pix.height()));
    p.drawTiledPixmap(rect, pix, -offset);
    p.restore();
}

inline void drawIdleDust(QPainter &p, const QRectF &rect, float opacity) {
    p.save();
    p.setOpacity(opacity);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(220, 220, 255, 40));
    const float t = timeSeconds() * 0.2f;
    const int count = 40;
    for (int i = 0; i < count; ++i) {
        const float fx = std::fmod((i * 37.0f + t * 60.0f), rect.width());
        const float fy = std::fmod((i * 83.0f + t * 40.0f), rect.height());
        p.drawEllipse(QPointF(rect.left() + fx, rect.top() + fy), 1.2f, 1.2f);
    }
    p.restore();
}

inline void paintBackground(QPainter &p, const QRectF &rect) {
    QLinearGradient grad(rect.topLeft(), rect.bottomLeft());
    grad.setColorAt(0.0, QColor(22, 18, 30));
    grad.setColorAt(0.6, QColor(30, 26, 40));
    grad.setColorAt(1.0, QColor(20, 18, 28));
    p.fillRect(rect, grad);

    if (!liteMode()) {
        p.save();
        p.setCompositionMode(QPainter::CompositionMode_SoftLight);
        drawFog(p, rect, QColor(255, 220, 235, 30), 0.08f, 0.12f, 1.0f);
        drawFog(p, rect, QColor(210, 230, 255, 30), 0.06f, 0.08f, 0.9f);
        p.restore();

        p.setPen(QPen(withAlpha(stroke(), 18), 1.0));
        for (int y = 0; y < rect.height(); y += 10) {
            const qreal yy = rect.top() + y;
            p.drawLine(QPointF(rect.left(), yy), QPointF(rect.right(), yy));
        }

        drawGrain(p, rect, 0.12f);
    }
}
}
