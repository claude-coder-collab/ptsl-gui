#include "command_browser.hpp"

#include <QComboBox>
#include <QHeaderView>
#include <QLineEdit>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <map>

namespace ptslgui::ui {
namespace {

constexpr int commandIdRole = Qt::UserRole + 1;

} // namespace

QString categoryLabel(const std::string& category) {
    QString label = QString::fromStdString(category);
    if (label == QStringLiteral("io") || label.startsWith(QStringLiteral("io_"))) {
        label.replace(0, 2, QStringLiteral("IO"));
    }
    label.replace(u'_', u' ');
    if (!label.isEmpty()) {
        label[0] = label[0].toUpper();
    }
    return label;
}

CommandBrowser::CommandBrowser(const CommandCatalog& catalog, QWidget* parent)
    : QWidget(parent)
    , catalog_(catalog)
    , search_(new QLineEdit(this))
    , category_(new QComboBox(this))
    , tree_(new QTreeWidget(this)) {
    search_->setObjectName("commandSearch");
    search_->setPlaceholderText(tr("Search commands"));
    search_->setClearButtonEnabled(true);

    category_->setObjectName("categoryFilter");
    category_->addItem(tr("All categories"), QString());
    for (const auto& category : catalog_.categories()) {
        category_->addItem(categoryLabel(category), QString::fromStdString(category));
    }

    tree_->setObjectName("commandTree");
    tree_->setHeaderHidden(true);
    tree_->setUniformRowHeights(true);
    tree_->header()->setStretchLastSection(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(search_);
    layout->addWidget(category_);
    layout->addWidget(tree_);

    connect(search_, &QLineEdit::textChanged, this, &CommandBrowser::rebuild);
    connect(category_, &QComboBox::currentIndexChanged, this, &CommandBrowser::rebuild);
    connect(tree_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* current, QTreeWidgetItem*) { onCurrentItemChanged(current); });
    rebuild();
}

void CommandBrowser::select(int commandId) {
    for (QTreeWidgetItemIterator it(tree_); *it != nullptr; ++it) {
        if ((*it)->data(0, commandIdRole).toInt() == commandId && (*it)->data(0, commandIdRole).isValid()) {
            tree_->setCurrentItem(*it);
            return;
        }
    }
    selectedId_ = commandId;
}

void CommandBrowser::setHostVersion(std::optional<Version> version) {
    hostVersion_ = version;
    rebuild();
}

void CommandBrowser::rebuild() {
    const QSignalBlocker blocker(tree_);
    tree_->clear();

    const CommandFilter filter{
        .text = search_->text().toStdString(),
        .category = category_->currentData().toString().toStdString(),
        .includeDeprecated = true,
    };
    const auto commands = catalog_.search(filter);
    const bool flat = !filter.category.empty() || !filter.text.empty();

    std::map<QString, QTreeWidgetItem*> groups;
    QTreeWidgetItem* selected = nullptr;
    for (const CommandInfo* command : commands) {
        const auto addItem = [&](QTreeWidgetItem* parent) {
            auto* item = parent != nullptr ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree_);
            item->setText(0, QString::fromStdString(command->displayName));
            item->setData(0, commandIdRole, command->id);
            QString tooltip = QString::fromStdString(command->name);
            if (command->deprecated) {
                QFont font = item->font(0);
                font.setStrikeOut(true);
                item->setFont(0, font);
                tooltip += tr("\nDeprecated: %1").arg(QString::fromStdString(*command->deprecated));
            }
            if (hostVersion_ && command->isUnsupportedBy(*hostVersion_)) {
                item->setForeground(0, item->foreground(0).color().lighter(200));
                tooltip += tr("\nRequires Pro Tools %1").arg(QString::fromStdString(command->since->toString()));
            }
            item->setToolTip(0, tooltip);
            if (command->id == selectedId_ && selected == nullptr) {
                selected = item;
            }
        };

        if (flat) {
            addItem(nullptr);
            continue;
        }
        const auto& categories = command->categories;
        const std::vector<std::string> names = categories.empty() ? std::vector<std::string>{"other"} : categories;
        for (const auto& name : names) {
            const QString label = name == "other" ? QStringLiteral("Other") : categoryLabel(name);
            auto [it, inserted] = groups.try_emplace(label, nullptr);
            if (inserted) {
                it->second = new QTreeWidgetItem(tree_);
                it->second->setText(0, label);
                it->second->setFlags(Qt::ItemIsEnabled);
            }
            addItem(it->second);
        }
    }
    tree_->sortItems(0, Qt::AscendingOrder);
    if (!flat) {
        tree_->expandAll();
    }
    if (selected != nullptr) {
        tree_->setCurrentItem(selected);
        tree_->scrollToItem(selected);
    }
}

void CommandBrowser::onCurrentItemChanged(QTreeWidgetItem* current) {
    if (current == nullptr || !current->data(0, commandIdRole).isValid()) {
        return;
    }
    const int id = current->data(0, commandIdRole).toInt();
    if (id == selectedId_) {
        return;
    }
    selectedId_ = id;
    emit commandSelected(id);
}

} // namespace ptslgui::ui
