#include "sequence_panel.hpp"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace ptslgui::ui {
namespace {

constexpr int NumberColumn = 0;
constexpr int LabelColumn = 1;
constexpr int CommandColumn = 2;
constexpr int ContinueColumn = 3;
constexpr int StatusColumn = 4;
constexpr int ColumnCount = 5;

constexpr int VariableNameColumn = 0;
constexpr int VariableValueColumn = 1;

QPushButton* button(const QString& text, const char* name, QWidget* parent) {
    auto* result = new QPushButton(text, parent);
    result->setObjectName(QString::fromLatin1(name));
    return result;
}

Qt::CheckState checkState(bool checked) {
    return checked ? Qt::Checked : Qt::Unchecked;
}

} // namespace

QString stepStatusLabel(StepStatus status) {
    switch (status) {
    case StepStatus::Waiting:
        return {};
    case StepStatus::Skipped:
        return QObject::tr("Skipped");
    case StepStatus::Running:
        return QObject::tr("Running…");
    case StepStatus::Completed:
        return QObject::tr("Completed");
    case StepStatus::Failed:
        return QObject::tr("Failed");
    case StepStatus::Cancelled:
        return QObject::tr("Cancelled");
    }
    return {};
}

SequencePanel::SequencePanel(const CommandCatalog& catalog, QWidget* parent)
    : QWidget(parent)
    , catalog_(catalog)
    , name_(new QLineEdit(this))
    , steps_(new QTreeWidget(this))
    , variables_(new QTableWidget(0, 2, this))
    , run_(button(tr("Run"), "sequenceRun", this))
    , stop_(button(tr("Stop"), "sequenceStop", this))
    , add_(button(tr("Add Current Request"), "sequenceAdd", this))
    , update_(button(tr("Update Step"), "sequenceUpdate", this))
    , load_(button(tr("Load"), "sequenceLoad", this))
    , up_(button(tr("Up"), "sequenceUp", this))
    , down_(button(tr("Down"), "sequenceDown", this))
    , remove_(button(tr("Remove"), "sequenceRemove", this))
    , newButton_(button(tr("New"), "sequenceNew", this))
    , open_(button(tr("Open…"), "sequenceOpen", this))
    , addVariable_(button(tr("Add Variable"), "variableAdd", this))
    , removeVariable_(button(tr("Remove Variable"), "variableRemove", this)) {
    setObjectName("sequencePanel");
    name_->setObjectName("sequenceName");
    name_->setPlaceholderText(tr("Untitled sequence"));
    auto* save = button(tr("Save…"), "sequenceSave", this);

    steps_->setObjectName("sequenceSteps");
    steps_->setColumnCount(ColumnCount);
    steps_->setHeaderLabels({tr("#"), tr("Label"), tr("Command"), tr("Continue on Error"), tr("Status")});
    steps_->setRootIsDecorated(false);
    steps_->setUniformRowHeights(true);
    steps_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    steps_->header()->setStretchLastSection(true);
    steps_->setToolTip(tr("Tick # to include a step. Double-click a label to rename it."));

    variables_->setObjectName("sequenceVariables");
    variables_->setHorizontalHeaderLabels({tr("Variable"), tr("Value")});
    variables_->horizontalHeader()->setStretchLastSection(true);
    variables_->verticalHeader()->setVisible(false);

    auto* help = new QLabel(tr("In a step's request, <code>{{label.field[0].name}}</code> inserts a value from an "
                               "earlier step's response and <code>{{vars.name}}</code> a variable."),
                            this);
    help->setObjectName("sequenceHelp");
    help->setWordWrap(true);

    auto* top = new QHBoxLayout;
    top->addWidget(name_, 1);
    top->addWidget(newButton_);
    top->addWidget(open_);
    top->addWidget(save);
    top->addSpacing(12);
    top->addWidget(run_);
    top->addWidget(stop_);

    auto* stepButtons = new QHBoxLayout;
    for (QPushButton* item : {add_, update_, load_, up_, down_, remove_}) {
        stepButtons->addWidget(item);
    }
    stepButtons->addStretch(1);
    auto* stepsPane = new QWidget(this);
    auto* stepsLayout = new QVBoxLayout(stepsPane);
    stepsLayout->setContentsMargins(0, 0, 0, 0);
    stepsLayout->addWidget(steps_, 1);
    stepsLayout->addLayout(stepButtons);

    auto* variableButtons = new QHBoxLayout;
    variableButtons->addWidget(addVariable_);
    variableButtons->addWidget(removeVariable_);
    variableButtons->addStretch(1);
    auto* variablesPane = new QWidget(this);
    auto* variablesLayout = new QVBoxLayout(variablesPane);
    variablesLayout->setContentsMargins(0, 0, 0, 0);
    variablesLayout->addWidget(variables_, 1);
    variablesLayout->addLayout(variableButtons);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(stepsPane);
    splitter->addWidget(variablesPane);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addLayout(top);
    layout->addWidget(splitter, 1);
    layout->addWidget(help);

    connect(name_, &QLineEdit::textEdited, this, [this](const QString& text) {
        sequence_.name = text.toStdString();
        notifyChanged();
    });
    connect(steps_, &QTreeWidget::itemChanged, this, &SequencePanel::onStepItemChanged);
    connect(steps_, &QTreeWidget::itemSelectionChanged, this, &SequencePanel::updateButtons);
    connect(steps_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int column) {
        if (column != LabelColumn) {
            emit loadRequested(static_cast<std::size_t>(steps_->indexOfTopLevelItem(item)));
        }
    });
    connect(variables_, &QTableWidget::cellChanged, this, &SequencePanel::onVariableChanged);
    connect(variables_, &QTableWidget::itemSelectionChanged, this, &SequencePanel::updateButtons);
    connect(run_, &QPushButton::clicked, this, &SequencePanel::runRequested);
    connect(stop_, &QPushButton::clicked, this, &SequencePanel::stopRequested);
    connect(add_, &QPushButton::clicked, this, &SequencePanel::addRequested);
    connect(newButton_, &QPushButton::clicked, this, &SequencePanel::newRequested);
    connect(open_, &QPushButton::clicked, this, &SequencePanel::openRequested);
    connect(save, &QPushButton::clicked, this, &SequencePanel::saveRequested);
    connect(update_, &QPushButton::clicked, this, [this] {
        if (const auto index = selectedStep()) {
            emit updateRequested(*index);
        }
    });
    connect(load_, &QPushButton::clicked, this, [this] {
        if (const auto index = selectedStep()) {
            emit loadRequested(*index);
        }
    });
    connect(up_, &QPushButton::clicked, this, [this] {
        if (const auto index = selectedStep()) {
            moveStep(*index, -1);
        }
    });
    connect(down_, &QPushButton::clicked, this, [this] {
        if (const auto index = selectedStep()) {
            moveStep(*index, 1);
        }
    });
    connect(remove_, &QPushButton::clicked, this, [this] {
        if (const auto index = selectedStep()) {
            removeStep(*index);
        }
    });
    connect(addVariable_, &QPushButton::clicked, this, [this] {
        std::size_t number = sequence_.variables.size() + 1;
        std::string name = "var" + std::to_string(number);
        while (std::ranges::contains(sequence_.variables, name, &std::pair<std::string, std::string>::first)) {
            name = "var" + std::to_string(++number);
        }
        sequence_.variables.emplace_back(name, std::string{});
        rebuildVariables();
        variables_->setCurrentCell(variables_->rowCount() - 1, VariableValueColumn);
        notifyChanged();
    });
    connect(removeVariable_, &QPushButton::clicked, this, [this] {
        const int row = variables_->currentRow();
        if (row >= 0 && std::cmp_less(row, sequence_.variables.size())) {
            sequence_.variables.erase(sequence_.variables.begin() + row);
            rebuildVariables();
            notifyChanged();
        }
    });

    setSequence({});
}

void SequencePanel::setSequence(Sequence sequence) {
    sequence_ = std::move(sequence);
    name_->setText(QString::fromStdString(sequence_.name));
    rebuildSteps();
    rebuildVariables();
}

void SequencePanel::addStep(const CommandInfo& command, std::string requestTemplate) {
    std::string base = command.name;
    if (base.starts_with("CId_")) {
        base.erase(0, 4);
    }
    sequence_.steps.push_back(SequenceStep{.label = uniqueStepLabel(sequence_, base),
                                           .commandName = command.name,
                                           .requestTemplate = std::move(requestTemplate),
                                           .enabled = true,
                                           .continueOnError = false});
    rebuildSteps();
    selectStep(sequence_.steps.size() - 1);
    notifyChanged();
}

void SequencePanel::updateStep(std::size_t index, const CommandInfo& command, std::string requestTemplate) {
    if (index >= sequence_.steps.size()) {
        return;
    }
    sequence_.steps[index].commandName = command.name;
    sequence_.steps[index].requestTemplate = std::move(requestTemplate);
    rebuildSteps();
    selectStep(index);
    notifyChanged();
}

void SequencePanel::moveStep(std::size_t index, int offset) {
    const auto target = static_cast<std::ptrdiff_t>(index) + offset;
    if (index >= sequence_.steps.size() || target < 0 || target >= std::ssize(sequence_.steps)) {
        return;
    }
    std::swap(sequence_.steps[index], sequence_.steps[static_cast<std::size_t>(target)]);
    rebuildSteps();
    selectStep(static_cast<std::size_t>(target));
    notifyChanged();
}

void SequencePanel::removeStep(std::size_t index) {
    if (index >= sequence_.steps.size()) {
        return;
    }
    sequence_.steps.erase(sequence_.steps.begin() + static_cast<std::ptrdiff_t>(index));
    rebuildSteps();
    if (!sequence_.steps.empty()) {
        selectStep(std::min(index, sequence_.steps.size() - 1));
    }
    notifyChanged();
}

void SequencePanel::selectStep(std::size_t index) {
    if (QTreeWidgetItem* item = steps_->topLevelItem(static_cast<int>(index))) {
        steps_->setCurrentItem(item);
    }
}

std::optional<std::size_t> SequencePanel::selectedStep() const {
    QTreeWidgetItem* item = steps_->currentItem();
    if (item == nullptr || !item->isSelected()) {
        return std::nullopt;
    }
    const int index = steps_->indexOfTopLevelItem(item);
    return index < 0 ? std::nullopt : std::optional<std::size_t>(static_cast<std::size_t>(index));
}

void SequencePanel::setStepResult(std::size_t index, const StepResult& result) {
    QTreeWidgetItem* item = steps_->topLevelItem(static_cast<int>(index));
    if (item == nullptr) {
        return;
    }
    const QSignalBlocker blocker(steps_);
    item->setText(StatusColumn, result.error.empty()
                                    ? stepStatusLabel(result.status)
                                    : QStringLiteral("%1: %2").arg(stepStatusLabel(result.status),
                                                                   QString::fromStdString(result.error)));
    item->setToolTip(StatusColumn, QString::fromStdString(result.error));
}

void SequencePanel::clearResults() {
    const QSignalBlocker blocker(steps_);
    for (int i = 0; i < steps_->topLevelItemCount(); ++i) {
        steps_->topLevelItem(i)->setText(StatusColumn, {});
        steps_->topLevelItem(i)->setToolTip(StatusColumn, {});
    }
}

void SequencePanel::setRunState(bool running, bool canRun) {
    running_ = running;
    canRun_ = canRun;
    updateButtons();
}

void SequencePanel::rebuildSteps() {
    rebuilding_ = true;
    steps_->clear();
    for (std::size_t i = 0; i < sequence_.steps.size(); ++i) {
        const SequenceStep& step = sequence_.steps[i];
        auto* item = new QTreeWidgetItem(steps_);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable);
        item->setText(NumberColumn, QString::number(i + 1));
        item->setCheckState(NumberColumn, checkState(step.enabled));
        item->setText(LabelColumn, QString::fromStdString(step.label));
        const CommandInfo* command = catalog_.findByName(step.commandName);
        item->setText(CommandColumn, command != nullptr
                                         ? QString::fromStdString(command->displayName)
                                         : tr("%1 (unknown)").arg(QString::fromStdString(step.commandName)));
        item->setToolTip(CommandColumn, QStringLiteral("%1\n\n%2")
                                            .arg(QString::fromStdString(step.commandName),
                                                 QString::fromStdString(step.requestTemplate)));
        item->setCheckState(ContinueColumn, checkState(step.continueOnError));
    }
    for (int column = 0; column < StatusColumn; ++column) {
        steps_->resizeColumnToContents(column);
    }
    rebuilding_ = false;
    updateButtons();
}

void SequencePanel::rebuildVariables() {
    rebuilding_ = true;
    variables_->setRowCount(static_cast<int>(sequence_.variables.size()));
    for (int row = 0; row < variables_->rowCount(); ++row) {
        const auto& [name, value] = sequence_.variables[static_cast<std::size_t>(row)];
        auto* nameItem = new QTableWidgetItem(QString::fromStdString(name));
        nameItem->setToolTip(isValidLabel(name) ? tr("Use as {{vars.%1}}").arg(QString::fromStdString(name))
                                                : tr("Names use letters, digits and _, and start with a letter"));
        if (!isValidLabel(name)) {
            nameItem->setForeground(Qt::red);
        }
        variables_->setItem(row, VariableNameColumn, nameItem);
        variables_->setItem(row, VariableValueColumn, new QTableWidgetItem(QString::fromStdString(value)));
    }
    rebuilding_ = false;
    updateButtons();
}

void SequencePanel::onStepItemChanged(QTreeWidgetItem* item, int column) {
    if (rebuilding_) {
        return;
    }
    const int row = steps_->indexOfTopLevelItem(item);
    if (row < 0 || row >= std::ssize(sequence_.steps)) {
        return;
    }
    SequenceStep& step = sequence_.steps[static_cast<std::size_t>(row)];
    if (column == NumberColumn) {
        step.enabled = item->checkState(NumberColumn) == Qt::Checked;
    } else if (column == ContinueColumn) {
        step.continueOnError = item->checkState(ContinueColumn) == Qt::Checked;
    } else if (column == LabelColumn) {
        const std::string label = item->text(LabelColumn).trimmed().toStdString();
        const bool taken = std::ranges::any_of(
            sequence_.steps, [&](const SequenceStep& other) { return &other != &step && other.label == label; });
        if (isValidLabel(label) && !taken) {
            step.label = label;
        }
        const QSignalBlocker blocker(steps_);
        item->setText(LabelColumn, QString::fromStdString(step.label));
    } else {
        return;
    }
    notifyChanged();
}

void SequencePanel::onVariableChanged(int row, int column) {
    if (rebuilding_ || row < 0 || row >= std::ssize(sequence_.variables)) {
        return;
    }
    auto& [name, value] = sequence_.variables[static_cast<std::size_t>(row)];
    const QString text = variables_->item(row, column) != nullptr ? variables_->item(row, column)->text() : QString();
    if (column == VariableNameColumn) {
        name = text.trimmed().toStdString();
        QTableWidgetItem* item = variables_->item(row, column);
        const QSignalBlocker blocker(variables_);
        item->setForeground(isValidLabel(name) ? palette().text() : QBrush(Qt::red));
    } else {
        value = text.toStdString();
    }
    notifyChanged();
}

void SequencePanel::updateButtons() {
    const bool selected = selectedStep().has_value();
    const bool editable = !running_;
    run_->setVisible(!running_);
    stop_->setVisible(running_);
    run_->setEnabled(canRun_ && !sequence_.steps.empty());
    for (QPushButton* item : {add_, newButton_, open_, addVariable_}) {
        item->setEnabled(editable);
    }
    for (QPushButton* item : {update_, up_, down_, remove_}) {
        item->setEnabled(editable && selected);
    }
    load_->setEnabled(selected);
    removeVariable_->setEnabled(editable && variables_->currentRow() >= 0);
    name_->setReadOnly(running_);
    steps_->setEditTriggers(running_ ? QAbstractItemView::NoEditTriggers
                                     : QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    variables_->setEditTriggers(running_ ? QAbstractItemView::NoEditTriggers : QAbstractItemView::AllEditTriggers);
}

void SequencePanel::notifyChanged() {
    updateButtons();
    emit changed();
}

} // namespace ptslgui::ui
