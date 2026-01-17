#include "SampleBrowserModel.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QStorageInfo>

namespace {
bool isUsbMount(const QStorageInfo &info) {
    if (!info.isValid() || !info.isReady()) {
        return false;
    }
    const QString root = info.rootPath();
    if (root == "/") {
        return false;
    }
    if (info.isRemovable()) {
        return true;
    }
    return root.startsWith("/media/") || root.startsWith("/run/media/") || root.startsWith("/mnt/");
}
}  // namespace

void SampleBrowserModel::refresh() {
    m_roots.clear();
    m_selected = nullptr;
    m_dirty = true;

    QSet<QString> seenRoots;
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

        auto node = std::make_unique<Node>();
        node->path = root;
        node->isDir = true;
        node->expanded = true;
        node->scanned = false;

        QString name = volume.displayName();
        if (name.isEmpty()) {
            name = QFileInfo(root).fileName();
        }
        node->name = name.isEmpty() ? root : name;

        scanNode(node.get());
        m_roots.push_back(std::move(node));
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
