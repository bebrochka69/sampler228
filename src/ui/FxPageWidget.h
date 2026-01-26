#pragma once

#include <QElapsedTimer>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <QWidget>

class QKeyEvent;
class QHideEvent;
class QMouseEvent;
class QPaintEvent;
class QPainter;
class QShowEvent;
class PadBank;

struct FxInsert {
    QString effect;
    float p1 = 0.5f;
    float p2 = 0.5f;
    float p3 = 0.5f;
    float p4 = 0.5f;
};

struct FxTrack {
    QString name;
    QVector<FxInsert> inserts;
};

struct FxInsertHit {
    QRectF rect;
    int track = 0;
    int slot = 0;
};

struct FxEffectHit {
    QRectF rect;
    int index = 0;
};

class FxPageWidget : public QWidget {
    Q_OBJECT
public:
    explicit FxPageWidget(PadBank *pads, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void syncBusEffects(int trackIndex);
    void assignEffect(int effectIndex);
    void swapSlot(int track, int a, int b);
    void advanceAnimation();
    void drawEffectPreview(QPainter &p, const QRectF &rect, const FxInsert &slot, float level);

    QVector<FxTrack> m_tracks;
    QStringList m_effects;
    PadBank *m_pads = nullptr;

    int m_selectedTrack = 0;
    int m_selectedSlot = 0;
    int m_selectedEffect = 0;
    bool m_showMenu = false;
    int m_selectedParam = 0;

    QVector<FxInsertHit> m_slotHits;
    QVector<FxEffectHit> m_effectHits;

    QTimer m_animTimer;
    QElapsedTimer m_clock;
    float m_animTime = 0.0f;
    float m_sidechainValue = 0.0f;
    float m_compValue = 0.0f;
};
