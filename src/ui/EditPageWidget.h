#pragma once

#include <QString>
#include <QVector>
#include <QWidget>

class QPaintEvent;
class QKeyEvent;

class SampleSession;
class PadBank;

class EditPageWidget : public QWidget {
    Q_OBJECT
public:
    explicit EditPageWidget(SampleSession *session, PadBank *pads, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    struct Param {
        QString label;
        enum Type {
            Volume,
            Pan,
            Pitch,
            Stretch,
            Start,
            End,
            Slice,
            Mode
        } type;
    };

    QVector<Param> m_params;
    SampleSession *m_session = nullptr;
    PadBank *m_pads = nullptr;
    int m_selectedParam = 0;
};
