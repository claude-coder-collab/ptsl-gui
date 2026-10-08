#pragma once

#include <ptslgui/catalog.hpp>
#include <ptslgui/schema.hpp>

#include <QWidget>

#include <expected>
#include <functional>
#include <string>
#include <string_view>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QTabWidget;
class QTextBrowser;
class QTimer;

namespace ptslgui::ui {

class MessageEditor;

/// Shows a command's documentation and edits its request body in a generated form or as JSON.
class RequestEditor final : public QWidget {
    Q_OBJECT

public:
    using Validator = std::function<std::expected<std::string, std::string>(int, std::string_view)>;

    RequestEditor(const ProtoSchema& schema, Validator validator, QWidget* parent = nullptr);

    void setCommand(const CommandInfo* command);
    [[nodiscard]] std::string requestText() const;
    void setRequestText(const std::string& text);
    void setSendEnabled(bool enabled);
    void showError(const QString& message);

signals:
    void sendRequested();
    void formatRequested();

private:
    void validateNow();
    void loadSelectedExample();
    void onFormChanged();
    void onTabChanged(int index);
    bool loadFormFromText();

    const ProtoSchema& schema_;
    Validator validator_;
    const CommandInfo* command_ = nullptr;
    QLabel* title_ = nullptr;
    QTextBrowser* info_ = nullptr;
    QComboBox* examples_ = nullptr;
    QPushButton* loadExample_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QScrollArea* formArea_ = nullptr;
    MessageEditor* form_ = nullptr;
    QPlainTextEdit* editor_ = nullptr;
    QLabel* validation_ = nullptr;
    QPushButton* format_ = nullptr;
    QPushButton* send_ = nullptr;
    QTimer* validationTimer_ = nullptr;
    bool sendEnabled_ = false;
    bool syncing_ = false;
};

[[nodiscard]] QString commandMarkdown(const CommandInfo& command);

} // namespace ptslgui::ui
