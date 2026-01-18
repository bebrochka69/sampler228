#pragma once

#include <QString>
#include <QVector>
#include <QWidget>

class QPaintEvent;
class QKeyEvent;

class SampleSession;

class EditPageWidget : public QWidget {
    Q_OBJECT
public:
    explicit EditPageWidget(SampleSession *session, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    struct Param {
        QString label;
        float value = 0.0f;
    };

    QVector<Param> m_params;
    SampleSession *m_session = nullptr;
    int m_selectedParam = 0;
};
