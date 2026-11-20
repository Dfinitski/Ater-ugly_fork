#pragma once

#include "models/RadioModel.h"

#include <QWidget>

class QLabel;
class QPushButton;
class QTableWidget;

namespace AetherSDR {

class MemoryBrowsePanel : public QWidget {
    Q_OBJECT

public:
    explicit MemoryBrowsePanel(QWidget* parent = nullptr);

    void setMemories(const QMap<int, MemoryEntry>& memories);
    void setWritable(bool writable);
    void focusClosestToFrequency(double frequencyMhz);

signals:
    void memoryActivated(int memoryIndex);
    void quickAddRequested();
    // ANAN fork build 528: "Remove" of a row's right-click menu.
    void removeRequested(int memoryIndex);

private:
    void populateTable();
    QColor rowBackground(const MemoryEntry& memory, bool highlight) const;
    void scrollToHighlightedRow();

    QMap<int, MemoryEntry> m_memories;
    QLabel* m_emptyLabel{nullptr};
    QTableWidget* m_table{nullptr};
    QPushButton* m_addBtn{nullptr};
    int m_highlightedMemoryIndex{-1};
    bool m_writable{true};   // build 528
};

} // namespace AetherSDR
