#pragma once

#include <QVector>
#include <QWidget>
#include <array>

class PadBank;

class PianoRollOverlay : public QWidget {
    Q_OBJECT
public:
    struct Note {
        int start = 0;
        int length = 4;
        int row = 0;
    };

    explicit PianoRollOverlay(PadBank *pads, QWidget *parent = nullptr);

    void showForPad(int pad);

signals:
    void closed();
    void stepsChanged(int pad, const QVector<int> &steps);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    enum DragMode {
        DragNone,
        DragPan,
        DragMove,
        DragResize
    };

    QRectF timelineRect() const;
    QRectF keyboardRect() const;
    QRectF panelRect() const;
    QRectF rightPanelRect() const;
    float baseCellWidth() const;
    float cellWidth() const;
    float rowHeight() const;
    int clampStep(int step) const;
    int stepFromX(float x) const;
    float xFromStep(int step) const;
    int rowFromY(float y) const;
    float yFromRow(int row) const;
    QString noteLabel(int row) const;
    int noteAt(const QPointF &pos) const;
    bool hitNoteRightEdge(const Note &note, float x) const;

    void zoomBy(float factor);
    void clampScroll();
    void emitStepsChanged();

    PadBank *m_pads = nullptr;
    int m_activePad = 0;
    std::array<QVector<Note>, 8> m_notes;

    float m_zoom = 1.0f;
    float m_scroll = 0.0f;
    int m_totalSteps = 128;
    int m_rows = 24;
    int m_baseMidi = 60;

    bool m_deleteMode = false;
    DragMode m_dragMode = DragNone;
    int m_dragNoteIndex = -1;
    QPointF m_pressPos;
    float m_pressScroll = 0.0f;
    Note m_pressNote;

    QRectF m_closeRect;
    QRectF m_zoomInRect;
    QRectF m_zoomOutRect;
    QRectF m_deleteRect;
};
