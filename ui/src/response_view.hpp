#pragma once

#include <ptslgui/history.hpp>

#include <QWidget>

class QJsonValue;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace ptslgui::ui {

/// Shows the status, body and errors of one history entry.
class ResponseView final : public QWidget {
    Q_OBJECT

public:
    explicit ResponseView(QWidget* parent = nullptr);

    void showEntry(const HistoryEntry& entry);
    void clear();

private:
    QLabel* status_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QLabel* meta_ = nullptr;
    QPushButton* copy_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    QPlainTextEdit* raw_ = nullptr;
    QPlainTextEdit* errors_ = nullptr;
};

/// Pretty-prints JSON text; returns the input unchanged if it is not valid JSON.
[[nodiscard]] QString prettyJson(const std::string& json);

/// Adds children describing a JSON value under parent (or as top-level items when parent is null).
void addJsonItems(QTreeWidget* tree, QTreeWidgetItem* parent, const QJsonValue& value);

} // namespace ptslgui::ui
