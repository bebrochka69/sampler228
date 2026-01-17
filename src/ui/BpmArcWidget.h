#pragma once

#include <QWidget>

class QPaintEvent;

class BpmArcWidget : public QWidget {
    Q_OBJECT
public:
    explicit BpmArcWidget(QWidget *parent = nullptr);

    void setBpm(int bpm);
    int bpm() const { return m_bpm; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int m_bpm = 124;
};
