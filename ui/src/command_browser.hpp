#pragma once

#include <ptslgui/catalog.hpp>

#include <QWidget>

#include <optional>

class QComboBox;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace ptslgui::ui {

/// Searchable list of commands grouped by category.
class CommandBrowser final : public QWidget {
    Q_OBJECT

public:
    explicit CommandBrowser(const CommandCatalog& catalog, QWidget* parent = nullptr);

    void select(int commandId);
    void setHostVersion(std::optional<Version> version);

signals:
    void commandSelected(int commandId);

private:
    void rebuild();
    void onCurrentItemChanged(QTreeWidgetItem* current);

    const CommandCatalog& catalog_;
    QLineEdit* search_ = nullptr;
    QComboBox* category_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    std::optional<Version> hostVersion_;
    int selectedId_ = -1;
};

[[nodiscard]] QString categoryLabel(const std::string& category);

} // namespace ptslgui::ui
