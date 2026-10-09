#include "response_view.hpp"

#include <ptslgui/protocol.hpp>

#include <QClipboard>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace ptslgui::ui {
namespace {

constexpr int maxProgress = 100;

QString outcomeText(Outcome outcome) {
    switch (outcome) {
    case Outcome::Pending:
        return QObject::tr("Sent, waiting for a response…");
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

QString scalarText(const QJsonValue& value) {
    switch (value.type()) {
    case QJsonValue::Null:
        return QStringLiteral("null");
    case QJsonValue::Bool:
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QJsonValue::Double:
        return QString::number(value.toDouble(), 'g', 17);
    case QJsonValue::String:
        return value.toString();
    case QJsonValue::Array:
        return QObject::tr("[%1 items]").arg(value.toArray().size());
    case QJsonValue::Object:
        return QObject::tr("{%1 fields}").arg(value.toObject().size());
    case QJsonValue::Undefined:
        break;
    }
    return {};
}

// NOLINTNEXTLINE(misc-no-recursion)
QTreeWidgetItem* makeItem(QTreeWidget* tree, QTreeWidgetItem* parent, const QString& key, const QJsonValue& value) {
    auto* item = parent != nullptr ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
    item->setText(0, key);
    item->setText(1, scalarText(value));
    addJsonItems(tree, item, value);
    return item;
}

} // namespace

QString prettyJson(const std::string& json) {
    const QByteArray bytes = QByteArray::fromStdString(json);
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || document.isNull()) {
        return QString::fromStdString(json);
    }
    return QString::fromUtf8(document.toJson(QJsonDocument::Indented)).trimmed();
}

// NOLINTNEXTLINE(misc-no-recursion)
void addJsonItems(QTreeWidget* tree, QTreeWidgetItem* parent, const QJsonValue& value) {
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            makeItem(tree, parent, it.key(), it.value());
        }
    } else if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (qsizetype i = 0; i < array.size(); ++i) {
            makeItem(tree, parent, QStringLiteral("[%1]").arg(i), array.at(i));
        }
    }
}

ResponseView::ResponseView(QWidget* parent)
    : QWidget(parent)
    , status_(new QLabel(this))
    , progress_(new QProgressBar(this))
    , meta_(new QLabel(this))
    , copy_(new QPushButton(tr("Copy JSON"), this))
    , tabs_(new QTabWidget(this))
    , tree_(new QTreeWidget(this))
    , raw_(new QPlainTextEdit(this))
    , errors_(new QPlainTextEdit(this)) {
    status_->setObjectName("responseStatus");
    QFont statusFont = status_->font();
    statusFont.setBold(true);
    status_->setFont(statusFont);

    progress_->setObjectName("responseProgress");
    progress_->setRange(0, maxProgress);
    progress_->setTextVisible(true);

    meta_->setObjectName("responseMeta");
    meta_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    tree_->setObjectName("responseTree");
    tree_->setColumnCount(2);
    tree_->setHeaderLabels({tr("Field"), tr("Value")});
    tree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);

    const QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    for (QPlainTextEdit* text : {raw_, errors_}) {
        text->setReadOnly(true);
        text->setFont(fixed);
    }
    raw_->setObjectName("responseRaw");
    errors_->setObjectName("responseErrors");

    tabs_->setObjectName("responseTabs");
    tabs_->addTab(tree_, tr("Tree"));
    tabs_->addTab(raw_, tr("JSON"));
    tabs_->addTab(errors_, tr("Errors"));

    copy_->setObjectName("copyResponse");
    copy_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    copy_->setToolTip(tr("Copy the response body to the clipboard"));
    connect(copy_, &QPushButton::clicked, this, [this] { QGuiApplication::clipboard()->setText(raw_->toPlainText()); });

    auto* header = new QHBoxLayout;
    header->addWidget(status_);
    header->addWidget(progress_, 1);
    header->addWidget(copy_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(header);
    layout->addWidget(meta_);
    layout->addWidget(tabs_, 1);

    clear();
}

void ResponseView::clear() {
    status_->setText(tr("No response"));
    progress_->setValue(0);
    progress_->setVisible(false);
    meta_->clear();
    tree_->clear();
    raw_->clear();
    errors_->clear();
    copy_->setEnabled(false);
    tabs_->setTabText(2, tr("Errors"));
}

void ResponseView::showEntry(const HistoryEntry& entry) {
    status_->setText(outcomeText(entry.outcome));
    const bool running = entry.outcome == Outcome::Pending || entry.outcome == Outcome::InProgress;
    progress_->setVisible(running);
    progress_->setValue(std::clamp(entry.progress, 0, maxProgress));

    QStringList meta;
    meta << QString::fromStdString(entry.commandName);
    if (!entry.taskId.empty()) {
        meta << tr("Task %1").arg(QString::fromStdString(entry.taskId));
    }
    if (entry.duration) {
        meta << tr("%1 ms").arg(entry.duration->count());
    }
    meta_->setText(meta.join(QStringLiteral(" · ")));

    raw_->setPlainText(prettyJson(entry.responseJson));
    copy_->setEnabled(!entry.responseJson.empty());
    tree_->clear();
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(entry.responseJson));
    if (document.isObject()) {
        addJsonItems(tree_, nullptr, document.object());
    } else if (document.isArray()) {
        addJsonItems(tree_, nullptr, document.array());
    }
    tree_->expandToDepth(1);

    const auto errors = protocol::errorsFromJson(entry.errorJson);
    QStringList lines;
    for (const auto& error : errors) {
        const QString prefix = error.isWarning ? tr("Warning") : tr("Error");
        lines << (error.type.empty()
                      ? QStringLiteral("%1: %2").arg(prefix, QString::fromStdString(error.message))
                      : QStringLiteral("%1 [%2]: %3")
                            .arg(prefix, QString::fromStdString(error.type), QString::fromStdString(error.message)));
    }
    if (!entry.errorJson.empty()) {
        lines << QString() << prettyJson(entry.errorJson);
    }
    errors_->setPlainText(lines.join('\n'));
    tabs_->setTabText(2, errors.empty() ? tr("Errors") : tr("Errors (%1)").arg(errors.size()));
    if (entry.outcome == Outcome::Completed) {
        tabs_->setCurrentIndex(0);
    } else if (!errors.empty() && entry.responseJson.empty()) {
        tabs_->setCurrentIndex(2);
    }
}

} // namespace ptslgui::ui
