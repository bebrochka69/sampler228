#include "SampleBrowserModel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStorageInfo>
#include <QTextStream>
#include <functional>

namespace {
bool isUsbMount(const QStorageInfo &info) {
    if (!info.isValid() || !info.isReady()) {
        return false;
    }
    const QString root = info.rootPath();
    if (root == "/") {
        return false;
    }
    return root.startsWith("/media/") || root.startsWith("/run/media/") || root.startsWith("/mnt/");
}

void scanProcMounts(QSet<QString> &seenRoots,
                    const std::function<bool(const QString &, const QString &, bool, bool)> &addRoot) {
    QFile file("/proc/mounts");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }
    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        const QStringList parts = line.split(' ');
        if (parts.size() < 2) {
            continue;
        }
        const QString device = parts[0];
        const QString mountPoint = parts[1];
        if (!device.startsWith("/dev/sd") && !device.startsWith("/dev/usb")) {
            continue;
        }
        if (!mountPoint.startsWith("/media/") && !mountPoint.startsWith("/run/media/") &&
            !mountPoint.startsWith("/mnt/")) {
            continue;
        }
        if (seenRoots.contains(mountPoint)) {
            continue;
        }
        addRoot(mountPoint, QFileInfo(mountPoint).fileName(), true, true);
    }
}
}  // namespace

void SampleBrowserModel::refresh() {
    m_roots.clear();
    m_selected = nullptr;
    m_dirty = true;

    QSet<QString> seenRoots;
    auto addRootIfExists = [&](const QString &path, const QString &name, bool expanded,
                               bool preScan) -> bool {
        QDir dir(path);
        if (!dir.exists()) {
            return false;
        }
        const QString normalized = QDir::cleanPath(path);
        if (seenRoots.contains(normalized)) {
            return false;
        }
        seenRoots.insert(normalized);

        auto node = std::make_unique<Node>();
        node->path = normalized;
        node->isDir = true;
        node->expanded = expanded;
        node->scanned = false;
        node->name = name.isEmpty() ? normalized : name;

        if (preScan) {
            node->expanded = true;
            node->scanned = false;
            scanNode(node.get());
        }

        m_roots.push_back(std::move(node));
        return true;
    };
    const QList<QStorageInfo> volumes = QStorageInfo::mountedVolumes();
    for (const QStorageInfo &volume : volumes) {
        if (!isUsbMount(volume)) {
            continue;
        }
        const QString root = volume.rootPath();
        if (seenRoots.contains(root)) {
            continue;
        }
        seenRoots.insert(root);
        QString name = volume.displayName();
        if (name.isEmpty()) {
            name = QFileInfo(root).fileName();
        }
        addRootIfExists(root, name, true, true);
    }

    scanProcMounts(seenRoots, addRootIfExists);

    // Fallback: manually scan common mount roots.
    auto scanMountRoot = [&](const QString &root) {
        QDir dir(root);
        if (!dir.exists()) {
            return;
        }
        const QFileInfoList entries =
            dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &info : entries) {
            addRootIfExists(info.absoluteFilePath(), info.fileName(), true, true);
        }
    };

    const QString user = qEnvironmentVariable("USER");
    if (!user.isEmpty()) {
        scanMountRoot(QString("/media/%1").arg(user));
        scanMountRoot(QString("/run/media/%1").arg(user));
    }
    scanMountRoot("/media");
    scanMountRoot("/run/media");
    scanMountRoot("/mnt");
    addRootIfExists("/mnt/usb", "USB", true, true);
    addRootIfExists("/media/usb", "USB", true, true);

    if (m_roots.empty()) {
        const QString home = QDir::homePath();
        const QString samples = home + "/samples";
        const QString samplesCaps = home + "/Samples";
        const QString music = home + "/Music";

        addRootIfExists(samples, "LOCAL SAMPLES", false, false);
        addRootIfExists(samplesCaps, "LOCAL SAMPLES", false, false);
        addRootIfExists(music, "LOCAL MUSIC", false, false);
    }
}

QVector<SampleBrowserModel::Entry> SampleBrowserModel::entries() const {
    if (m_dirty) {
        rebuildEntries();
        m_dirty = false;
    }
    return m_entries;
}

SampleBrowserModel::Node *SampleBrowserModel::nodeAt(int index) const {
    if (m_dirty) {
        rebuildEntries();
        m_dirty = false;
    }
    if (index < 0 || index >= m_entries.size()) {
        return nullptr;
    }
    return m_entries[index].node;
}

void SampleBrowserModel::toggleExpanded(Node *node) {
    if (!node || !node->isDir) {
        return;
    }
    if (!node->scanned) {
        scanNode(node);
    }
    node->expanded = !node->expanded;
    m_dirty = true;
}

void SampleBrowserModel::setSelected(Node *node) {
    m_selected = node;
}

void SampleBrowserModel::rebuildEntries() const {
    m_entries.clear();
    for (const auto &root : m_roots) {
        appendEntries(root.get(), 0);
    }
}

void SampleBrowserModel::appendEntries(Node *node, int depth) const {
    if (!node) {
        return;
    }
    m_entries.push_back({node, depth});
    if (!node->isDir || !node->expanded) {
        return;
    }
    for (const auto &child : node->children) {
        appendEntries(child.get(), depth + 1);
    }
}

void SampleBrowserModel::scanNode(Node *node) {
    if (!node || !node->isDir) {
        return;
    }

    node->children.clear();
    QDir dir(node->path);
    if (!dir.exists()) {
        node->scanned = true;
        return;
    }

    const QFileInfoList dirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable,
                                                 QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &info : dirs) {
        if (info.fileName().startsWith('.')) {
            continue;
        }
        auto child = std::make_unique<Node>();
        child->name = info.fileName();
        child->path = info.absoluteFilePath();
        child->isDir = true;
        child->expanded = false;
        child->scanned = false;
        child->parent = node;
        node->children.push_back(std::move(child));
    }

    const QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::Readable,
                                                  QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &info : files) {
        if (!isAudioFile(info)) {
            continue;
        }
        auto child = std::make_unique<Node>();
        child->name = info.fileName();
        child->path = info.absoluteFilePath();
        child->isDir = false;
        child->expanded = false;
        child->scanned = true;
        child->parent = node;
        node->children.push_back(std::move(child));
    }

    node->scanned = true;
    m_dirty = true;
}

bool SampleBrowserModel::isAudioFile(const QFileInfo &info) {
    const QString ext = info.suffix().toLower();
    return ext == "wav" || ext == "mp3";
}
