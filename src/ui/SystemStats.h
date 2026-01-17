#pragma once

#include <QObject>

class SystemStats : public QObject {
    Q_OBJECT
public:
    explicit SystemStats(QObject *parent = nullptr);

    void update();
    float cpuUsage() const { return m_cpuUsage; }
    float ramUsage() const { return m_ramUsage; }

private:
#ifdef Q_OS_LINUX
    bool readCpu(quint64 &idle, quint64 &total) const;
    bool readRam(float &usage) const;

    quint64 m_prevIdle = 0;
    quint64 m_prevTotal = 0;
    bool m_hasPrev = false;
#endif

    float m_cpuUsage = 0.22f;
    float m_ramUsage = 0.37f;
    float m_phase = 0.0f;
};
