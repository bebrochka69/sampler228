#include "SystemStats.h"

#include <QFile>
#include <QTextStream>
#include <QThread>
#include <QtGlobal>

SystemStats::SystemStats(QObject *parent) : QObject(parent) {}

void SystemStats::update() {
#ifdef Q_OS_LINUX
    bool updated = false;

    quint64 idle = 0;
    quint64 total = 0;
    if (readCpu(idle, total)) {
        if (m_hasPrev && total > m_prevTotal) {
            const quint64 idleDelta = idle - m_prevIdle;
            const quint64 totalDelta = total - m_prevTotal;
            const float usage = 1.0f - static_cast<float>(idleDelta) / static_cast<float>(totalDelta);
            m_cpuUsage = qBound(0.0f, usage, 1.0f);
        }
        m_prevIdle = idle;
        m_prevTotal = total;
        m_hasPrev = true;
        updated = true;
    }

    float ramUsage = 0.0f;
    if (readRam(ramUsage)) {
        m_ramUsage = ramUsage;
        updated = true;
    }

    float loadUsage = 0.0f;
    if (readLoad(loadUsage)) {
        m_loadUsage = loadUsage;
        updated = true;
    } else {
        m_loadUsage = 0.0f;
    }

    if (updated) {
        return;
    }
#endif

    // No /proc available: keep stats at zero instead of simulating.
    m_cpuUsage = 0.0f;
    m_ramUsage = 0.0f;
    m_loadUsage = 0.0f;
}

#ifdef Q_OS_LINUX
bool SystemStats::readCpu(quint64 &idle, quint64 &total) const {
    QFile file("/proc/stat");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream in(&file);
    const QString line = in.readLine();
    if (!line.startsWith("cpu")) {
        return false;
    }

    const QStringList parts = line.split(' ', Qt::SkipEmptyParts);
    if (parts.size() < 8) {
        return false;
    }

    quint64 user = parts[1].toULongLong();
    quint64 nice = parts[2].toULongLong();
    quint64 system = parts[3].toULongLong();
    quint64 idleTime = parts[4].toULongLong();
    quint64 iowait = parts[5].toULongLong();
    quint64 irq = parts[6].toULongLong();
    quint64 softirq = parts[7].toULongLong();
    quint64 steal = parts.size() > 8 ? parts[8].toULongLong() : 0;
    idle = idleTime + iowait;
    total = user + nice + system + idleTime + iowait + irq + softirq + steal;
    return total > 0;
}

bool SystemStats::readRam(float &usage) const {
    QFile file("/proc/meminfo");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream in(&file);
    quint64 total = 0;
    quint64 available = 0;

    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.startsWith("MemTotal:")) {
            const QStringList parts = line.split(' ', Qt::SkipEmptyParts);
            if (parts.size() >= 2) {
                total = parts[1].toULongLong();
            }
        } else if (line.startsWith("MemAvailable:")) {
            const QStringList parts = line.split(' ', Qt::SkipEmptyParts);
            if (parts.size() >= 2) {
                available = parts[1].toULongLong();
            }
        }
    }

    if (total == 0) {
        return false;
    }

    usage = 1.0f - static_cast<float>(available) / static_cast<float>(total);
    usage = qBound(0.0f, usage, 1.0f);
    return true;
}

bool SystemStats::readLoad(float &usage) const {
    QFile file("/proc/loadavg");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream in(&file);
    const QString line = in.readLine().trimmed();
    const QStringList parts = line.split(' ', Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return false;
    }

    bool ok = false;
    const double load1 = parts[0].toDouble(&ok);
    if (!ok) {
        return false;
    }

    int cores = QThread::idealThreadCount();
    if (cores <= 0) {
        cores = 1;
    }

    const double normalized = load1 / static_cast<double>(cores);
    usage = static_cast<float>(qBound(0.0, normalized, 1.0));
    return true;
}
#endif
