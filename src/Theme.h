#pragma once

#include <QColor>
#include <QFont>
#include <QFontInfo>

namespace Theme {
inline QColor bg0() { return QColor(30, 33, 44); }
inline QColor bg1() { return QColor(40, 44, 58); }
inline QColor bg2() { return QColor(24, 27, 37); }
inline QColor bg3() { return QColor(52, 57, 72); }
inline QColor stroke() { return QColor(104, 92, 214); }
inline QColor accent() { return QColor(122, 255, 205); }
inline QColor accentAlt() { return QColor(255, 140, 208); }
inline QColor text() { return QColor(232, 236, 245); }
inline QColor textMuted() { return QColor(180, 188, 210); }
inline QColor warn() { return QColor(255, 198, 80); }
inline QColor danger() { return QColor(255, 110, 120); }

inline QFont baseFont(int pt, QFont::Weight weight = QFont::Normal) {
    QFont f("DejaVu Sans Mono");
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
}
