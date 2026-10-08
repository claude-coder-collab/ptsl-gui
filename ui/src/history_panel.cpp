#include "history_panel.hpp"

#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace ptslgui::ui {
namespace {

constexpr int sequenceRole = Qt::UserRole + 1;

constexpr int SequenceColumn = 0;
constexpr int TimeColumn = 1;
constexpr int CommandColumn = 2;
constexpr int OutcomeColumn = 3;
constexpr int DurationColumn = 4;
constexpr int ColumnCount = 5;

void fill(QTreeWidgetItem* item, const HistoryEntry& entry) {
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(entry.sentAt.time_since_epoch()).count();
    item->setData(SequenceColumn, sequenceRole, QVariant::fromValue<qulonglong>(entry.sequence));
    item->setText(SequenceColumn, QString::number(entry.sequence));
    item->setText(TimeColumn, QDateTime::fromMSecsSinceEpoch(milliseconds).toString(QStringLiteral("HH:mm:ss")));
    item->setToolTip(TimeColumn, QDateTime::fromMSecsSinceEpoch(milliseconds).toString(Qt::ISODateWithMs));
    QString command = QString::fromStdString(entry.commandName);
    command.remove(QStringLiteral("CId_"));
    item->setText(CommandColumn, command);
    item->setText(OutcomeColumn, entry.outcome == Outcome::InProgress
                                     ? QObject::tr("%1 %2%").arg(outcomeLabel(entry.outcome)).arg(entry.progress)
                                     : outcomeLabel(entry.outcome));
    item->setText(DurationColumn, entry.duration ? QObject::tr("%1 ms").arg(entry.duration->count()) : QString());
    item->setTextAlignment(DurationColumn, Qt::AlignRight | Qt::AlignVCenter);
}

} // namespace

QString outcomeLabel(Outcome outcome) {
    switch (outcome) {
    case Outcome::Pending:
        return QObject::tr("Pending");
    case Outcome::InProgress:
        return QObject::tr("In progress");
    case Outcome::Completed:
        return QObject::tr("Completed");
    case Outcome::Failed:
        return QObject::tr("Failed");
    case Outcome::Cancelled:
        return QObject::tr("Cancelled");
    }
    return {};
}

HistoryPanel::HistoryPanel(QWidget* parent)
    : QWidget(parent)
    , list_(new QTreeWidget(this))
    , load_(new QPushButton(tr("Load"), this))
    , resend_(new QPushButton(tr("Resend"), this))
    , export_(new QPushButton(tr("Export…"), this))
    , clear_(new QPushButton(tr("Clear"), this)) {
    list_->setObjectName("historyList");
    list_->setColumnCount(ColumnCount);
    list_->setHeaderLabels({tr("#"), tr("Time"), tr("Command"), tr("Outcome"), tr("Duration")});
    list_->setRootIsDecorated(false);
    list_->setUniformRowHeights(true);
    list_->setAlternatingRowColors(true);
    list_->header()->setSectionResizeMode(CommandColumn, QHeaderView::Stretch);
    list_->header()->setStretchLastSection(false);

    load_->setObjectName("historyLoad");
    load_->setToolTip(tr("Load this request into the editor"));
    resend_->setObjectName("historyResend");
    export_->setObjectName("historyExport");
    auto* import = new QPushButton(tr("Import…"), this);
    import->setObjectName("historyImport");
    clear_->setObjectName("historyClear");

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(load_);
    buttons->addWidget(resend_);
    buttons->addStretch(1);
    buttons->addWidget(import);
    buttons->addWidget(export_);
    buttons->addWidget(clear_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(list_, 1);
    layout->addLayout(buttons);

    connect(list_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* current) {
        updateButtons();
        if (current != nullptr) {
            emit entrySelected(current->data(SequenceColumn, sequenceRole).toULongLong());
        }
    });
    connect(list_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        emit loadRequested(item->data(SequenceColumn, sequenceRole).toULongLong());
    });
    connect(load_, &QPushButton::clicked, this, [this] {
        if (const auto sequence = selectedSequence()) {
            emit loadRequested(*sequence);
        }
    });
    connect(resend_, &QPushButton::clicked, this, [this] {
        if (const auto sequence = selectedSequence()) {
            emit resendRequested(*sequence);
        }
    });
    connect(export_, &QPushButton::clicked, this, &HistoryPanel::exportRequested);
    connect(import, &QPushButton::clicked, this, &HistoryPanel::importRequested);
    connect(clear_, &QPushButton::clicked, this, &HistoryPanel::clearRequested);
    updateButtons();
}

void HistoryPanel::setEntries(const History& history) {
    const QSignalBlocker blocker(list_);
    list_->clear();
    for (const auto& entry : history.entries()) {
        auto* item = new QTreeWidgetItem;
        fill(item, entry);
        list_->insertTopLevelItem(0, item);
    }
    updateButtons();
}

void HistoryPanel::updateEntry(const HistoryEntry& entry) {
    QTreeWidgetItem* item = itemFor(entry.sequence);
    if (item == nullptr) {
        item = new QTreeWidgetItem;
        list_->insertTopLevelItem(0, item);
    }
    fill(item, entry);
    updateButtons();
}

void HistoryPanel::select(std::uint64_t sequence) {
    if (QTreeWidgetItem* item = itemFor(sequence)) {
        list_->setCurrentItem(item);
    }
}

std::optional<std::uint64_t> HistoryPanel::selectedSequence() const {
    const QTreeWidgetItem* item = list_->currentItem();
    if (item == nullptr) {
        return std::nullopt;
    }
    return item->data(SequenceColumn, sequenceRole).toULongLong();
}

int HistoryPanel::count() const {
    return list_->topLevelItemCount();
}

QTreeWidgetItem* HistoryPanel::itemFor(std::uint64_t sequence) const {
    for (int i = 0; i < list_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = list_->topLevelItem(i);
        if (item->data(SequenceColumn, sequenceRole).toULongLong() == sequence) {
            return item;
        }
    }
    return nullptr;
}

void HistoryPanel::updateButtons() {
    const bool selected = list_->currentItem() != nullptr;
    load_->setEnabled(selected);
    resend_->setEnabled(selected);
    export_->setEnabled(list_->topLevelItemCount() > 0);
    clear_->setEnabled(list_->topLevelItemCount() > 0);
}

} // namespace ptslgui::ui
