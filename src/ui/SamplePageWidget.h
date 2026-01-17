#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QPaintEvent;

class SamplePageWidget : public QWidget {
    Q_OBJECT
public:
    explicit SamplePageWidget(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    struct NavItem {
        QString label;
        int indent = 0;
        bool isFolder = false;
        bool selected = false;
    };

    QVector<NavItem> m_navItems;
    QStringList m_projects;
    QVector<float> m_wave;
};
