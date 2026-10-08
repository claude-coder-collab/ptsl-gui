#include "command_browser.hpp"
#include "history_panel.hpp"
#include "preferences_dialog.hpp"
#include "request_editor.hpp"
#include "response_view.hpp"

#include <ptslgui/protocol.hpp>
#include <ptslgui/ui/main_window.hpp>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QSaveFile>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QtConcurrentRun>

namespace ptslgui::ui {
namespace {

constexpr int statusMessageMs = 8000;
constexpr int historyDockHeight = 160;
const char* const historyFilter = "History (*.json)";

} // namespace

MainWindow::MainWindow(const CommandCatalog& catalog, const ProtoSchema& schema, IPtslSession& session,
                       QSettings& settings, QWidget* parent)
    : QMainWindow(parent)
    , catalog_(catalog)
    , schema_(schema)
    , session_(session)
    , settings_(settings)
    , controller_(std::make_unique<RequestController>(catalog_, schema_, session_, history_,
                                                      [this](std::function<void()> task) {
                                                          QMetaObject::invokeMethod(this, std::move(task),
                                                                                    Qt::QueuedConnection);
                                                      }))
    , confirm_(
          [this](const CommandInfo& command, bool& dontAskAgain) { return confirmWithDialog(command, dontAskAgain); })
    , browser_(new CommandBrowser(catalog_, this))
    , editor_(new RequestEditor(
          schema_, [this](int commandId, std::string_view json) { return controller_->prepare(commandId, json); },
          this))
    , responseView_(new ResponseView(this))
    , historyPanel_(new HistoryPanel(this))
    , splitter_(new QSplitter(Qt::Horizontal, this)) {
    setWindowTitle(tr("PTSL GUI"));
    setObjectName("mainWindow");

    splitter_->setObjectName("mainSplitter");
    splitter_->addWidget(browser_);
    splitter_->addWidget(editor_);
    splitter_->addWidget(responseView_);
    splitter_->setStretchFactor(0, 1);
    splitter_->setStretchFactor(1, 2);
    splitter_->setStretchFactor(2, 2);
    setCentralWidget(splitter_);

    auto* historyDock = new QDockWidget(tr("History"), this);
    historyDock->setObjectName("historyDock");
    historyDock->setWidget(historyPanel_);
    addDockWidget(Qt::BottomDockWidgetArea, historyDock);
    resizeDocks({historyDock}, {historyDockHeight}, Qt::Vertical);

    buildToolbar();
    buildMenus();
    statusBar()->setObjectName("statusBar");

    connect(browser_, &CommandBrowser::commandSelected, this, &MainWindow::showCommand);
    connect(editor_, &RequestEditor::sendRequested, this, &MainWindow::sendCurrent);
    connect(editor_, &RequestEditor::formatRequested, this, &MainWindow::formatCurrent);
    connect(historyPanel_, &HistoryPanel::entrySelected, this, &MainWindow::showHistoryEntry);
    connect(historyPanel_, &HistoryPanel::loadRequested, this, &MainWindow::loadHistoryEntry);
    connect(historyPanel_, &HistoryPanel::resendRequested, this, &MainWindow::resendHistoryEntry);
    connect(historyPanel_, &HistoryPanel::exportRequested, this, &MainWindow::chooseExportPath);
    connect(historyPanel_, &HistoryPanel::importRequested, this, &MainWindow::chooseImportPath);
    connect(historyPanel_, &HistoryPanel::clearRequested, this, &MainWindow::clearHistory);

    resize(1400, 900);
    restoreSettings();
    updateConnectionUi();
}

MainWindow::~MainWindow() = default;

bool MainWindow::selectCommand(int commandId) {
    if (catalog_.findById(commandId) == nullptr) {
        return false;
    }
    browser_->select(commandId);
    showCommand(commandId);
    return true;
}

void MainWindow::setConfirmHandler(ConfirmHandler handler) {
    confirm_ = std::move(handler);
}

void MainWindow::buildToolbar() {
    auto* toolbar = addToolBar(tr("Connection"));
    toolbar->setObjectName("connectionToolbar");
    toolbar->setMovable(false);

    addressEdit_ = new QLineEdit(toolbar);
    addressEdit_->setObjectName("addressEdit");
    addressEdit_->setMaximumWidth(220);
    addressEdit_->setToolTip(tr("PTSL server address"));

    launchCheck_ = new QCheckBox(tr("Launch Pro Tools"), toolbar);
    launchCheck_->setObjectName("launchCheck");

    connectAction_ = new QAction(tr("Connect"), this);
    connectAction_->setObjectName("connectAction");
    cancelAction_ = new QAction(tr("Cancel all"), this);
    cancelAction_->setObjectName("cancelAction");

    connectionLabel_ = new QLabel(toolbar);
    connectionLabel_->setObjectName("connectionStatus");

    toolbar->addWidget(new QLabel(tr("Address "), toolbar));
    toolbar->addWidget(addressEdit_);
    toolbar->addWidget(launchCheck_);
    toolbar->addAction(connectAction_);
    toolbar->addAction(cancelAction_);
    toolbar->addSeparator();
    toolbar->addWidget(connectionLabel_);

    connect(connectAction_, &QAction::triggered, this, &MainWindow::toggleConnection);
    connect(cancelAction_, &QAction::triggered, this, [this] { session_.cancelAll(); });
}

void MainWindow::buildMenus() {
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("Import History…"), this, &MainWindow::chooseImportPath);
    file->addAction(tr("Export History…"), this, &MainWindow::chooseExportPath);
    file->addSeparator();
    QAction* quit = file->addAction(tr("Quit"), qApp, &QApplication::closeAllWindows);
    quit->setMenuRole(QAction::QuitRole);
    quit->setShortcut(QKeySequence::Quit);

    QMenu* request = menuBar()->addMenu(tr("&Request"));
    request->addAction(tr("Send"), this, &MainWindow::sendCurrent);
    request->addAction(tr("Format JSON"), this, &MainWindow::formatCurrent);
    request->addSeparator();
    confirmAction_ = request->addAction(tr("Ask Before Modifying the Session"));
    confirmAction_->setObjectName("confirmAction");
    confirmAction_->setCheckable(true);
    confirmAction_->setChecked(settings_.confirmMutating());
    connect(confirmAction_, &QAction::toggled, this, [this](bool checked) { settings_.setConfirmMutating(checked); });
    QAction* preferences = request->addAction(tr("Preferences…"), this, &MainWindow::showPreferences);
    preferences->setMenuRole(QAction::PreferencesRole);
    preferences->setShortcut(QKeySequence::Preferences);

    QMenu* view = menuBar()->addMenu(tr("&View"));
    view->addAction(findChild<QDockWidget*>("historyDock")->toggleViewAction());

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    QAction* about = help->addAction(tr("About PTSL GUI"), this, [this] {
        QMessageBox::about(this, tr("About PTSL GUI"),
                           tr("<b>PTSL GUI</b><br>Sends any Pro Tools Scripting Library command and shows the "
                              "response.<br><br>MIT licence. Uses Qt (LGPLv3) and the Avid PTSL SDK."));
    });
    about->setMenuRole(QAction::AboutRole);
}

void MainWindow::restoreSettings() {
    addressEdit_->setText(settings_.address());
    launchCheck_->setChecked(settings_.launchHost());
    if (const QByteArray geometry = settings_.windowGeometry(); !geometry.isEmpty()) {
        restoreGeometry(geometry);
    }
    if (const QByteArray state = settings_.windowState(); !state.isEmpty()) {
        restoreState(state);
    }
    if (const QByteArray splitter = settings_.splitterState(); !splitter.isEmpty()) {
        splitter_->restoreState(splitter);
    }
    if (const CommandInfo* command = catalog_.findByName(settings_.lastCommand().toStdString())) {
        selectCommand(command->id);
    }
}

void MainWindow::saveSettings() {
    settings_.setAddress(addressEdit_->text().trimmed());
    settings_.setLaunchHost(launchCheck_->isChecked());
    settings_.setWindowLayout(saveGeometry(), saveState(), splitter_->saveState());
    settings_.sync();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    saveSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::showPreferences() {
    PreferencesDialog dialog(settings_, this);
    if (dialog.exec() == QDialog::Accepted) {
        confirmAction_->setChecked(settings_.confirmMutating());
    }
}

bool MainWindow::confirmWithDialog(const CommandInfo& command, bool& dontAskAgain) {
    QMessageBox box(QMessageBox::Question, tr("Modify the session?"),
                    tr("%1 modifies the Pro Tools session.").arg(QString::fromStdString(command.displayName)),
                    QMessageBox::Yes | QMessageBox::Cancel, this);
    box.setInformativeText(tr("Send it?"));
    box.button(QMessageBox::Yes)->setText(tr("Send"));
    box.setDefaultButton(QMessageBox::Yes);
    auto* dontAsk = new QCheckBox(tr("Don't ask again"), &box);
    box.setCheckBox(dontAsk);
    const bool accepted = box.exec() == QMessageBox::Yes;
    dontAskAgain = accepted && dontAsk->isChecked();
    return accepted;
}

void MainWindow::toggleConnection() {
    if (connecting_) {
        return;
    }
    if (session_.state() == ConnectionState::Connected) {
        session_.disconnect();
        hostVersion_.reset();
        browser_->setHostVersion(std::nullopt);
        updateConnectionUi();
        emit connectionChanged(false);
        return;
    }

    ConnectionSettings settings;
    settings.address = addressEdit_->text().trimmed().toStdString();
    settings.launchHost = launchCheck_->isChecked();
    settings_.setAddress(addressEdit_->text().trimmed());
    settings_.setLaunchHost(settings.launchHost);
    connecting_ = true;
    updateConnectionUi();

    auto* watcher = new QFutureWatcher<std::expected<void, std::string>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher] {
        onConnected(watcher->result());
        watcher->deleteLater();
    });
    IPtslSession* session = &session_;
    watcher->setFuture(QtConcurrent::run([session, settings] { return session->connect(settings); }));
}

void MainWindow::onConnected(const std::expected<void, std::string>& result) {
    connecting_ = false;
    if (!result) {
        statusBar()->showMessage(tr("Connection failed: %1").arg(QString::fromStdString(result.error())),
                                 statusMessageMs);
    }
    updateConnectionUi();
    if (result) {
        emit connectionChanged(true);
        requestHostVersion();
    }
}

void MainWindow::requestHostVersion() {
    const CommandInfo* command = catalog_.findByName(protocol::getPtslVersion);
    if (command == nullptr) {
        return;
    }
    const auto sent = controller_->send(command->id, "", [this](const HistoryEntry& entry) {
        onEntryUpdated(entry);
        if (entry.outcome != Outcome::Completed) {
            return;
        }
        hostVersion_ = protocol::versionFromResponse(entry.responseJson);
        browser_->setHostVersion(hostVersion_);
        updateConnectionUi();
    });
    if (!sent) {
        statusBar()->showMessage(tr("Could not query the PTSL version: %1").arg(QString::fromStdString(sent.error())),
                                 statusMessageMs);
    } else if (const HistoryEntry* entry = history_.find(*sent)) {
        historyPanel_->updateEntry(*entry);
    }
}

void MainWindow::updateConnectionUi() {
    const bool connected = session_.state() == ConnectionState::Connected;
    connectAction_->setEnabled(!connecting_);
    connectAction_->setText(connected ? tr("Disconnect") : tr("Connect"));
    cancelAction_->setEnabled(connected);
    addressEdit_->setEnabled(!connected && !connecting_);
    launchCheck_->setEnabled(!connected && !connecting_);
    editor_->setSendEnabled(connected);

    QString text;
    if (connecting_) {
        text = tr("Connecting…");
    } else if (!connected) {
        text = tr("Disconnected");
    } else if (hostVersion_) {
        text = tr("Connected · PTSL %1").arg(QString::fromStdString(hostVersion_->toString()));
    } else {
        text = tr("Connected");
    }
    connectionLabel_->setText(text);
}

void MainWindow::showCommand(int commandId) {
    const CommandInfo* command = catalog_.findById(commandId);
    if (command == current_) {
        return;
    }
    current_ = command;
    editor_->setCommand(command);
    if (command == nullptr) {
        return;
    }
    settings_.setLastCommand(QString::fromStdString(command->name));
    if (!settings_.rememberRequests()) {
        return;
    }
    if (const auto saved = settings_.lastRequest(command->name); saved && controller_->prepare(command->id, *saved)) {
        editor_->setRequestText(*saved);
    }
}

void MainWindow::sendCurrent() {
    if (current_ != nullptr) {
        send(*current_, editor_->requestText(), true);
    }
}

bool MainWindow::send(const CommandInfo& command, const std::string& requestJson, bool fromEditor) {
    if (settings_.confirmMutating() && command.isMutating()) {
        bool dontAskAgain = false;
        if (!confirm_(command, dontAskAgain)) {
            statusBar()->showMessage(tr("%1 was not sent").arg(QString::fromStdString(command.displayName)),
                                     statusMessageMs);
            return false;
        }
        if (dontAskAgain) {
            settings_.setConfirmMutating(false);
            confirmAction_->setChecked(false);
        }
    }
    if (hostVersion_ && command.since && command.isUnsupportedBy(*hostVersion_)) {
        statusBar()->showMessage(tr("Warning: %1 requires Pro Tools %2; connected host is %3")
                                     .arg(QString::fromStdString(command.displayName),
                                          QString::fromStdString(command.since->toString()),
                                          QString::fromStdString(hostVersion_->toString())),
                                 statusMessageMs);
    }
    const auto sent =
        controller_->send(command.id, requestJson, [this](const HistoryEntry& entry) { onEntryUpdated(entry); });
    if (!sent) {
        if (fromEditor) {
            editor_->showError(QString::fromStdString(sent.error()));
        } else {
            statusBar()->showMessage(QString::fromStdString(sent.error()), statusMessageMs);
        }
        return false;
    }
    if (fromEditor && settings_.rememberRequests()) {
        settings_.setLastRequest(command.name, requestJson);
    }
    if (const HistoryEntry* entry = history_.find(*sent)) {
        historyPanel_->updateEntry(*entry);
        displayedSequence_ = *sent;
        historyPanel_->select(*sent);
        responseView_->showEntry(*entry);
    }
    return true;
}

void MainWindow::onEntryUpdated(const HistoryEntry& entry) {
    historyPanel_->updateEntry(entry);
    if (displayedSequence_ == entry.sequence) {
        responseView_->showEntry(entry);
    }
    emit responseUpdated(entry.sequence);
}

void MainWindow::showHistoryEntry(std::uint64_t sequence) {
    if (const HistoryEntry* entry = history_.find(sequence)) {
        displayedSequence_ = sequence;
        responseView_->showEntry(*entry);
    }
}

void MainWindow::loadHistoryEntry(std::uint64_t sequence) {
    const HistoryEntry* entry = history_.find(sequence);
    if (entry == nullptr || !selectCommand(entry->commandId)) {
        return;
    }
    const std::string request = entry->requestJson.empty() ? std::string("{}") : entry->requestJson;
    if (current_ != nullptr && current_->requestType) {
        editor_->setRequestText(
            schema_.normalizeJson(*current_->requestType, request, JsonFormat{.pretty = true}).value_or(request));
    }
}

void MainWindow::resendHistoryEntry(std::uint64_t sequence) {
    const HistoryEntry* entry = history_.find(sequence);
    if (entry == nullptr) {
        return;
    }
    if (const CommandInfo* command = catalog_.findById(entry->commandId)) {
        send(*command, entry->requestJson, false);
    }
}

std::expected<void, std::string> MainWindow::exportHistory(const QString& path) const {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return std::unexpected(file.errorString().toStdString());
    }
    const QByteArray data = QByteArray::fromStdString(history_.toJson());
    if (file.write(data) != data.size() || !file.commit()) {
        return std::unexpected(file.errorString().toStdString());
    }
    return {};
}

std::expected<void, std::string> MainWindow::importHistory(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::unexpected(file.errorString().toStdString());
    }
    auto loaded = history_.loadJson(file.readAll().toStdString());
    if (!loaded) {
        return loaded;
    }
    displayedSequence_.reset();
    responseView_->clear();
    historyPanel_->setEntries(history_);
    return {};
}

void MainWindow::chooseExportPath() {
    const QString path = QFileDialog::getSaveFileName(this, tr("Export History"), QStringLiteral("ptsl-history.json"),
                                                      tr(historyFilter));
    if (path.isEmpty()) {
        return;
    }
    const auto result = exportHistory(path);
    statusBar()->showMessage(result ? tr("History exported to %1").arg(path)
                                    : tr("Export failed: %1").arg(QString::fromStdString(result.error())),
                             statusMessageMs);
}

void MainWindow::chooseImportPath() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Import History"), QString(), tr(historyFilter));
    if (path.isEmpty()) {
        return;
    }
    const auto result = importHistory(path);
    statusBar()->showMessage(result ? tr("History imported from %1").arg(path)
                                    : tr("Import failed: %1").arg(QString::fromStdString(result.error())),
                             statusMessageMs);
}

void MainWindow::clearHistory() {
    history_.clear();
    displayedSequence_.reset();
    responseView_->clear();
    historyPanel_->setEntries(history_);
}

void MainWindow::formatCurrent() {
    if (current_ == nullptr || !current_->requestType) {
        return;
    }
    const auto formatted =
        schema_.normalizeJson(*current_->requestType, editor_->requestText(), JsonFormat{.pretty = true});
    if (formatted) {
        editor_->setRequestText(*formatted);
    } else {
        editor_->showError(QString::fromStdString(formatted.error()));
    }
}

} // namespace ptslgui::ui
