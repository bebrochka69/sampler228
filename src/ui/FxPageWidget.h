#pragma once

#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QShowEvent;
class PadBank;

struct FxSlot {
    QString effect;
    float p1 = 0.5f;
    float p2 = 0.5f;
    float p3 = 0.5f;
};

struct FxTrack {
    QString name;
    QVector<FxSlot> slots;
};

struct FxSlotHit {
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

private:
    void syncBusEffects(int trackIndex);
    void assignEffect(int effectIndex);
    void swapSlot(int track, int a, int b);

    QVector<FxTrack> m_tracks;
    QStringList m_effects;
    PadBank *m_pads = nullptr;

    int m_selectedTrack = 0;
    int m_selectedSlot = 0;
    int m_selectedEffect = 0;
    bool m_showMenu = false;
    int m_selectedParam = 0;

    QVector<FxSlotHit> m_slotHits;
    QVector<FxEffectHit> m_effectHits;
};
