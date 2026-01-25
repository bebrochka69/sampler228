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
inline QColor bg0() { return QColor(26, 14, 24); }
inline QColor bg1() { return QColor(42, 26, 40); }
inline QColor bg2() { return QColor(30, 18, 28); }
inline QColor bg3() { return QColor(58, 32, 52); }
inline QColor stroke() { return QColor(120, 160, 200); }
inline QColor accent() { return QColor(18, 202, 255); }   // cyan
inline QColor accentAlt() { return QColor(255, 56, 118); } // magenta
inline QColor text() { return QColor(245, 245, 255); }
inline QColor textMuted() { return QColor(190, 200, 220); }
inline QColor warn() { return QColor(255, 210, 160); }
inline QColor danger() { return QColor(255, 80, 120); }

inline float uiScale();
inline int px(int value);
inline float pxF(float value);

inline QFont baseFont(int pt, QFont::Weight weight = QFont::Normal) {
    QFont f("DejaVu Serif");
    if (!QFontInfo(f).exactMatch()) {
        f = QFont("DejaVu Sans");
    }
    const int px = qMax(8, static_cast<int>(std::lround(pt * uiScale())));
    f.setPixelSize(px);
    f.setWeight(weight);
    f.setHintingPreference(QFont::PreferFullHinting);
    return f;
}

inline QFont condensedFont(int pt, QFont::Weight weight = QFont::DemiBold) {
    QFont f("DejaVu Sans");
    if (!QFontInfo(f).exactMatch()) {
        f = QFont("DejaVu Sans");
    }
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

inline void applyRenderHints(QPainter &p) {
    const bool lite = liteMode();
    p.setRenderHint(QPainter::Antialiasing, !lite);
    p.setRenderHint(QPainter::TextAntialiasing, !lite);
    p.setRenderHint(QPainter::SmoothPixmapTransform, !lite);
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

    const QPixmap &bg = leftBgPixmap();
    if (!bg.isNull()) {
        const qreal targetH = rect.height();
        const qreal ratio = targetH / bg.height();
        const qreal targetW = bg.width() * ratio;
        const QRectF target(rect.left(), rect.top(), targetW, targetH);
        p.save();
        p.setOpacity(0.22);
        p.drawPixmap(target, bg, QRectF(0, 0, bg.width(), bg.height()));
        p.restore();
    }

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
