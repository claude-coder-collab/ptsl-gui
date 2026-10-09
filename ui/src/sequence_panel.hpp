#pragma once

#include <ptslgui/catalog.hpp>
#include <ptslgui/sequence.hpp>

#include <QWidget>

#include <cstddef>
#include <optional>
#include <string>

class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace ptslgui::ui {

/// Edits a request sequence (steps and variables) and shows the status of each step while it runs.
class SequencePanel final : public QWidget {
    Q_OBJECT

public:
    explicit SequencePanel(const CommandCatalog& catalog, QWidget* parent = nullptr);

    void setSequence(Sequence sequence);

    [[nodiscard]] const Sequence& sequence() const { return sequence_; }

    /// Appends a step for the command with a unique label derived from its name, and selects it.
    void addStep(const CommandInfo& command, std::string requestTemplate);
    /// Replaces the command and request of a step.
    void updateStep(std::size_t index, const CommandInfo& command, std::string requestTemplate);
    void moveStep(std::size_t index, int offset);
    void removeStep(std::size_t index);
    void selectStep(std::size_t index);
    [[nodiscard]] std::optional<std::size_t> selectedStep() const;

    void setStepResult(std::size_t index, const StepResult& result);
    void clearResults();
    /// Running disables editing and swaps Run for Stop; canRun reflects the connection.
    void setRunState(bool running, bool canRun);

signals:
    void changed();
    void addRequested();
    void updateRequested(std::size_t index);
    void loadRequested(std::size_t index);
    void runRequested();
    void stopRequested();
    void newRequested();
    void openRequested();
    void saveRequested();

private:
    void rebuildSteps();
    void rebuildVariables();
    void onStepItemChanged(QTreeWidgetItem* item, int column);
    void onVariableChanged(int row, int column);
    void updateButtons();
    void notifyChanged();

    const CommandCatalog& catalog_;
    Sequence sequence_;
    bool rebuilding_ = false;
    bool running_ = false;
    bool canRun_ = false;
    QLineEdit* name_ = nullptr;
    QTreeWidget* steps_ = nullptr;
    QTableWidget* variables_ = nullptr;
    QPushButton* run_ = nullptr;
    QPushButton* stop_ = nullptr;
    QPushButton* add_ = nullptr;
    QPushButton* update_ = nullptr;
    QPushButton* load_ = nullptr;
    QPushButton* up_ = nullptr;
    QPushButton* down_ = nullptr;
    QPushButton* remove_ = nullptr;
    QPushButton* newButton_ = nullptr;
    QPushButton* open_ = nullptr;
    QPushButton* addVariable_ = nullptr;
    QPushButton* removeVariable_ = nullptr;
};

[[nodiscard]] QString stepStatusLabel(StepStatus status);

} // namespace ptslgui::ui
