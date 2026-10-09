#pragma once

#include <ptslgui/catalog.hpp>
#include <ptslgui/controller.hpp>
#include <ptslgui/history.hpp>
#include <ptslgui/schema.hpp>
#include <ptslgui/sequence.hpp>
#include <ptslgui/session.hpp>
#include <ptslgui/ui/app_settings.hpp>

#include <QMainWindow>
#include <QStringList>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>

class QAction;
class QCheckBox;
class QLabel;
class QLineEdit;
class QSettings;
class QSplitter;

namespace ptslgui::ui {

class CommandBrowser;
class HistoryPanel;
class RequestEditor;
class ResponseView;
class SequencePanel;

/// The main PTSL GUI window: command browser, request editor, response view and history.
/// Object names of child widgets are stable and used by tests.
class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    /// Asks whether a command that modifies the session may be sent; may set dontAskAgain.
    using ConfirmHandler = std::function<bool(const CommandInfo& command, bool& dontAskAgain)>;
    /// Asks whether a sequence containing the named session-modifying commands may run; may set dontAskAgain.
    using SequenceConfirmHandler = std::function<bool(const QStringList& commandNames, bool& dontAskAgain)>;

    MainWindow(const CommandCatalog& catalog, const ProtoSchema& schema, IPtslSession& session, QSettings& settings,
               QWidget* parent = nullptr);
    ~MainWindow() override;
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;
    MainWindow(MainWindow&&) = delete;
    MainWindow& operator=(MainWindow&&) = delete;

    /// Selects a command in the browser and editor; returns false for an unknown id.
    bool selectCommand(int commandId);

    [[nodiscard]] std::optional<Version> hostVersion() const { return hostVersion_; }

    [[nodiscard]] const History& history() const { return history_; }

    /// Replaces the confirmation dialog (used by tests).
    void setConfirmHandler(ConfirmHandler handler);
    void setSequenceConfirmHandler(SequenceConfirmHandler handler);

    std::expected<void, std::string> exportHistory(const QString& path) const;
    std::expected<void, std::string> importHistory(const QString& path);

    std::expected<void, std::string> saveSequence(const QString& path) const;
    std::expected<void, std::string> openSequence(const QString& path);
    /// Runs the sequence in the Sequence panel (after confirmation if needed); returns false if it did not start.
    bool runSequence();
    [[nodiscard]] bool sequenceRunning() const;

    /// Writes window layout and connection fields to the settings.
    void saveSettings();

signals:
    void connectionChanged(bool connected);
    void responseUpdated(std::uint64_t sequence);
    void sequenceFinished();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildToolbar();
    void buildMenus();
    void restoreSettings();
    void toggleConnection();
    void onConnected(const std::expected<void, std::string>& result);
    void requestHostVersion();
    void updateConnectionUi();
    void sendCurrent();
    bool send(const CommandInfo& command, const std::string& requestJson, bool fromEditor, bool confirm = true);
    void sendUndoRedo(const char* commandName);
    void onEntryUpdated(const HistoryEntry& entry);
    void showHistoryEntry(std::uint64_t sequence);
    void loadHistoryEntry(std::uint64_t sequence);
    void resendHistoryEntry(std::uint64_t sequence);
    void chooseExportPath();
    void chooseImportPath();
    void clearHistory();
    void formatCurrent();
    void showCommand(int commandId);
    void showPreferences();
    bool confirmWithDialog(const CommandInfo& command, bool& dontAskAgain);
    bool confirmSequenceWithDialog(const QStringList& commandNames, bool& dontAskAgain);
    void turnOffConfirmations();
    void addSequenceStep();
    void updateSequenceStep(std::size_t index);
    void loadSequenceStep(std::size_t index);
    void onSequenceStep(const StepResult& result, std::size_t index);
    void onSequenceFinished(SequenceOutcome outcome);
    void chooseSequenceOpenPath();
    void chooseSequenceSavePath();

    const CommandCatalog& catalog_;
    const ProtoSchema& schema_;
    IPtslSession& session_;
    AppSettings settings_;
    History history_;
    std::unique_ptr<RequestController> controller_;
    std::unique_ptr<SequenceRunner> runner_;
    ConfirmHandler confirm_;
    SequenceConfirmHandler confirmSequence_;

    CommandBrowser* browser_ = nullptr;
    RequestEditor* editor_ = nullptr;
    ResponseView* responseView_ = nullptr;
    HistoryPanel* historyPanel_ = nullptr;
    SequencePanel* sequencePanel_ = nullptr;
    QSplitter* splitter_ = nullptr;
    QLineEdit* addressEdit_ = nullptr;
    QCheckBox* launchCheck_ = nullptr;
    QAction* connectAction_ = nullptr;
    QAction* cancelAction_ = nullptr;
    QAction* confirmAction_ = nullptr;
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
    QLabel* connectionLabel_ = nullptr;

    const CommandInfo* current_ = nullptr;
    std::optional<std::uint64_t> displayedSequence_;
    std::optional<Version> hostVersion_;
    bool connecting_ = false;
};

} // namespace ptslgui::ui
