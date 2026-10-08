#pragma once

#include <ptslgui/history.hpp>

#include <QWidget>

#include <cstdint>
#include <optional>

class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace ptslgui::ui {

/// List of sent requests, newest first, with actions on the selected entry.
class HistoryPanel final : public QWidget {
    Q_OBJECT

public:
    explicit HistoryPanel(QWidget* parent = nullptr);

    /// Replaces the list with the history's entries.
    void setEntries(const History& history);
    /// Adds or updates one entry.
    void updateEntry(const HistoryEntry& entry);
    void select(std::uint64_t sequence);
    [[nodiscard]] std::optional<std::uint64_t> selectedSequence() const;
    [[nodiscard]] int count() const;

signals:
    void entrySelected(std::uint64_t sequence);
    void loadRequested(std::uint64_t sequence);
    void resendRequested(std::uint64_t sequence);
    void exportRequested();
    void importRequested();
    void clearRequested();

private:
    QTreeWidgetItem* itemFor(std::uint64_t sequence) const;
    void updateButtons();

    QTreeWidget* list_ = nullptr;
    QPushButton* load_ = nullptr;
    QPushButton* resend_ = nullptr;
    QPushButton* export_ = nullptr;
    QPushButton* clear_ = nullptr;
};

[[nodiscard]] QString outcomeLabel(Outcome outcome);

} // namespace ptslgui::ui
