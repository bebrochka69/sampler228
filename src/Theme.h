#pragma once

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFont>
#include <QFontInfo>
#include <QGuiApplication>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPixmap>
#include <QRandomGenerator>
#include <QScreen>
#include <QtGlobal>
#include <cmath>

namespace Theme {
inline QColor bg0() { return QColor(214, 213, 206); }      // OP-1 light base
inline QColor bg1() { return QColor(205, 204, 198); }
inline QColor bg2() { return QColor(196, 195, 188); }
inline QColor bg3() { return QColor(184, 183, 176); }
inline QColor stroke() { return QColor(120, 120, 112); }
inline QColor accent() { return QColor(74, 163, 255); }    // OP-1 blue
inline QColor accentAlt() { return QColor(255, 154, 60); } // OP-1 orange
inline QColor text() { return QColor(36, 36, 34); }
inline QColor textMuted() { return QColor(92, 92, 86); }
inline QColor warn() { return QColor(99, 210, 96); }       // OP-1 green
inline QColor danger() { return QColor(220, 90, 80); }

inline float uiScale();
inline int px(int value);
inline float pxF(float value);

inline QFont baseFont(int pt, QFont::Weight weight = QFont::Normal) {
    QFont f("DejaVu Sans Mono");
    if (!QFontInfo(f).exactMatch()) {
        f = QFont("monospace");
    }
    f.setStyleHint(QFont::TypeWriter);
    const int px = qMax(8, static_cast<int>(std::lround(pt * uiScale())));
    f.setPixelSize(px);
    f.setWeight(weight);
    f.setHintingPreference(QFont::PreferFullHinting);
    return f;
}

inline QFont condensedFont(int pt, QFont::Weight weight = QFont::DemiBold) {
    QFont f("DejaVu Sans Mono");
    if (!QFontInfo(f).exactMatch()) {
        f = QFont("monospace");
    }
    f.setStyleHint(QFont::TypeWriter);
    const int px = qMax(8, static_cast<int>(std::lround(pt * uiScale())));
    f.setPixelSize(px);
    f.setWeight(weight);
    f.setHintingPreference(QFont::PreferFullHinting);
    return f;
}

inline float uiScale() {
    static float scale = -1.0f;
    if (scale > 0.0f) {
        return scale;
    }
    bool ok = false;
    const QByteArray env = qgetenv("GROOVEBOX_SCALE");
    const float envScale = env.toFloat(&ok);
    if (ok && envScale > 0.1f) {
        scale = envScale;
        return scale;
    }
    QSize size(1280, 720);
    if (QGuiApplication::primaryScreen()) {
        size = QGuiApplication::primaryScreen()->geometry().size();
    }
    const float sx = static_cast<float>(size.width()) / 1280.0f;
    const float sy = static_cast<float>(size.height()) / 720.0f;
    float base = qMin(sx, sy);
    if (size.width() <= 800 || size.height() <= 480) {
        base = qMax(base, 1.0f);
    } else if (size.width() <= 1024 || size.height() <= 600) {
        base = qMax(base, 0.9f);
    }
    scale = qBound(0.7f, base, 1.1f);
    return scale;
}

inline int px(int value) {
    return qMax(1, static_cast<int>(std::lround(value * uiScale())));
}

inline float pxF(float value) {
    return value * uiScale();
}

inline QColor withAlpha(const QColor &c, int alpha) {
    QColor out = c;
    out.setAlpha(alpha);
    return out;
}

inline bool liteMode() {
    return qEnvironmentVariableIsSet("GROOVEBOX_LITE");
}

inline void applyRenderHints(QPainter &p) {
    const bool lite = liteMode();
    p.setRenderHint(QPainter::Antialiasing, !lite);
    p.setRenderHint(QPainter::TextAntialiasing, !lite);
    p.setRenderHint(QPainter::SmoothPixmapTransform, !lite);
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
                const int g = qBound(0, v + 10, 255);
                row[x] = qRgba(g, g, g, 18);
            }
        }
        return out;
    }();
    return img;
}

inline const QPixmap &leftBgPixmap() {
    static QPixmap pix;
    static bool loaded = false;
    if (loaded) {
        return pix;
    }
    loaded = true;
    QString path = qEnvironmentVariable("GROOVEBOX_BG_LEFT");
    if (path.isEmpty()) {
        const QString base = QCoreApplication::applicationDirPath();
        const QStringList candidates = {
            base + "/assets/bg_left.png",
            base + "/assets/backgrounds/left.png",
            base + "/assets/backgrounds/bg_left.png",
            QDir(base).absoluteFilePath("../assets/bg_left.png"),
            QDir(base).absoluteFilePath("../assets/backgrounds/left.png"),
            QDir(base).absoluteFilePath("../assets/backgrounds/bg_left.png"),
        };
        for (const QString &cand : candidates) {
            if (QFileInfo::exists(cand)) {
                path = cand;
                break;
            }
        }
    }
    if (!path.isEmpty()) {
        pix.load(path);
    }
    return pix;
}

inline void drawFog(QPainter &p, const QRectF &rect, const QColor &color, float opacity,
                    float speed, float scale) {
    Q_UNUSED(speed);
    Q_UNUSED(scale);
    p.save();
    p.setOpacity(opacity);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawRect(rect);
    p.restore();
}

inline void drawScanlines(QPainter &p, const QRectF &rect, int step, int alpha) {
    p.save();
    p.setPen(QPen(withAlpha(QColor(0, 0, 0), alpha), 1.0));
    for (int y = 0; y < rect.height(); y += step) {
        const qreal yy = rect.top() + y;
        p.drawLine(QPointF(rect.left(), yy), QPointF(rect.right(), yy));
    }
    p.restore();
}

inline void drawGrain(QPainter &p, const QRectF &rect, float opacity) {
    p.save();
    p.setOpacity(opacity);
    p.setCompositionMode(QPainter::CompositionMode_Screen);
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
    grad.setColorAt(0.0, QColor(222, 221, 214));
    grad.setColorAt(1.0, QColor(206, 205, 198));
    p.fillRect(rect, grad);

    if (!liteMode()) {
        drawGrain(p, rect, 0.08f);
    }
}
}
