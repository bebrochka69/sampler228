#pragma once

#include <QVector>
#include <QString>
#include <memory>
#include <vector>

class QFileInfo;

class SampleBrowserModel {
public:
    struct Node {
        QString name;
        QString path;
        bool isDir = false;
        bool expanded = false;
        bool scanned = false;
        Node *parent = nullptr;
        std::vector<std::unique_ptr<Node>> children;
    };

    struct Entry {
        Node *node = nullptr;
        int depth = 0;
    };

    void refresh();
    QVector<Entry> entries() const;

    Node *nodeAt(int index) const;
    void toggleExpanded(Node *node);
    void setSelected(Node *node);
    Node *selected() const { return m_selected; }
    bool isEmpty() const { return m_roots.empty(); }

private:
    void rebuildEntries() const;
    void appendEntries(Node *node, int depth) const;
    void scanNode(Node *node);
    static bool isAudioFile(const QFileInfo &info);

    std::vector<std::unique_ptr<Node>> m_roots;
    mutable QVector<Entry> m_entries;
    mutable bool m_dirty = true;
    Node *m_selected = nullptr;
};
