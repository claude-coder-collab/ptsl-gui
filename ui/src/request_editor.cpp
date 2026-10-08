#include "request_editor.hpp"

#include "command_browser.hpp"
#include "form_editor.hpp"

#include <QComboBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>

namespace ptslgui::ui {
namespace {

constexpr int validationDelayMs = 250;
constexpr int formTab = 0;
constexpr int jsonTab = 1;
constexpr int jsonIndent = 2;

QString code(const std::string& text) {
    return QStringLiteral("`%1`").arg(QString::fromStdString(text));
}

} // namespace

QString commandMarkdown(const CommandInfo& command) {
    QString markdown = QString::fromStdString(command.description);
    if (!command.notes.empty()) {
        markdown += QStringLiteral("\n\n");
        for (const auto& note : command.notes) {
            markdown += QStringLiteral("> %1\n>\n").arg(QString::fromStdString(note).replace('\n', ' '));
        }
    }

    QStringList facts;
    facts << code(command.name);
    if (command.requestType) {
        facts << QObject::tr("Request: %1").arg(code(*command.requestType));
    }
    if (command.responseType) {
        facts << QObject::tr("Response: %1").arg(code(*command.responseType));
    }
    if (command.since) {
        facts << QObject::tr("Since %1").arg(QString::fromStdString(command.since->toString()));
    }
    if (!command.categories.empty()) {
        QStringList categories;
        for (const auto& category : command.categories) {
            categories << categoryLabel(category);
        }
        facts << categories.join(QStringLiteral(", "));
    }
    if (command.isMutating()) {
        facts << QObject::tr("**Modifies the session**");
    }
    markdown += QStringLiteral("\n\n") + facts.join(QStringLiteral(" · "));
    if (command.deprecated) {
        markdown += QObject::tr("\n\n**Deprecated:** %1").arg(QString::fromStdString(*command.deprecated));
    }
    return markdown;
}

RequestEditor::RequestEditor(const ProtoSchema& schema, Validator validator, QWidget* parent)
    : QWidget(parent)
    , schema_(schema)
    , validator_(std::move(validator))
    , title_(new QLabel(this))
    , info_(new QTextBrowser(this))
    , examples_(new QComboBox(this))
    , loadExample_(new QPushButton(tr("Load example"), this))
    , tabs_(new QTabWidget(this))
    , formArea_(new QScrollArea(this))
    , editor_(new QPlainTextEdit(this))
    , validation_(new QLabel(this))
    , format_(new QPushButton(tr("Format"), this))
    , send_(new QPushButton(tr("Send"), this))
    , validationTimer_(new QTimer(this)) {
    title_->setObjectName("commandTitle");
    QFont titleFont = title_->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.4);
    titleFont.setBold(true);
    title_->setFont(titleFont);

    info_->setObjectName("commandInfo");
    info_->setOpenExternalLinks(true);
    info_->setMaximumHeight(220);

    examples_->setObjectName("exampleSelector");
    loadExample_->setObjectName("loadExample");

    editor_->setObjectName("requestEditor");
    editor_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    editor_->setTabChangesFocus(false);
    editor_->setPlaceholderText(tr("Select a command"));

    formArea_->setObjectName("requestFormArea");
    formArea_->setWidgetResizable(true);
    tabs_->setObjectName("requestTabs");
    tabs_->addTab(formArea_, tr("Form"));
    tabs_->addTab(editor_, tr("JSON"));

    validation_->setObjectName("validationLabel");
    validation_->setWordWrap(true);
    validation_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    format_->setObjectName("formatButton");
    send_->setObjectName("sendButton");
    send_->setDefault(true);
    send_->setToolTip(tr("Send (%1)").arg(QKeySequence(Qt::CTRL | Qt::Key_Return).toString(QKeySequence::NativeText)));

    auto* exampleRow = new QHBoxLayout;
    exampleRow->addWidget(examples_, 1);
    exampleRow->addWidget(loadExample_);

    auto* buttonRow = new QHBoxLayout;
    buttonRow->addWidget(validation_, 1);
    buttonRow->addWidget(format_);
    buttonRow->addWidget(send_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(title_);
    layout->addWidget(info_);
    layout->addLayout(exampleRow);
    layout->addWidget(tabs_, 1);
    layout->addLayout(buttonRow);

    validationTimer_->setSingleShot(true);
    validationTimer_->setInterval(validationDelayMs);

    connect(validationTimer_, &QTimer::timeout, this, &RequestEditor::validateNow);
    connect(editor_, &QPlainTextEdit::textChanged, validationTimer_, qOverload<>(&QTimer::start));
    connect(tabs_, &QTabWidget::currentChanged, this, &RequestEditor::onTabChanged);
    connect(loadExample_, &QPushButton::clicked, this, &RequestEditor::loadSelectedExample);
    connect(format_, &QPushButton::clicked, this, &RequestEditor::formatRequested);
    connect(send_, &QPushButton::clicked, this, &RequestEditor::sendRequested);
    const auto* shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
    connect(shortcut, &QShortcut::activated, this, [this] {
        if (send_->isEnabled()) {
            emit sendRequested();
        }
    });

    setCommand(nullptr);
}

void RequestEditor::setCommand(const CommandInfo* command) {
    command_ = command;
    examples_->clear();
    validation_->clear();
    form_ = nullptr;
    auto* formPage = new QLabel(formArea_);
    formPage->setAlignment(Qt::AlignCenter);
    formArea_->setWidget(formPage);
    if (command == nullptr) {
        title_->setText(tr("No command selected"));
        info_->clear();
        formPage->setText(tr("Select a command"));
        editor_->clear();
        editor_->setEnabled(false);
        examples_->setEnabled(false);
        loadExample_->setEnabled(false);
        format_->setEnabled(false);
        send_->setEnabled(false);
        return;
    }

    title_->setText(QString::fromStdString(command->displayName));
    info_->setMarkdown(commandMarkdown(*command));

    for (std::size_t i = 0; i < command->requestExamples.size(); ++i) {
        const auto& example = command->requestExamples[i];
        QString label = example.caption.empty() ? tr("Example %1").arg(i + 1) : QString::fromStdString(example.caption);
        if (!example.valid) {
            label += tr(" (invalid JSON)");
        }
        examples_->addItem(label, static_cast<int>(i));
    }
    const bool hasExamples = examples_->count() > 0;
    examples_->setEnabled(hasExamples);
    loadExample_->setEnabled(hasExamples);
    if (!hasExamples) {
        examples_->addItem(tr("No documented examples"));
    }

    const bool takesBody = command->requestType.has_value();
    if (takesBody) {
        form_ = new MessageEditor(schema_, *command->requestType);
        form_->setObjectName("requestForm");
        auto* page = new QWidget;
        auto* pageLayout = new QVBoxLayout(page);
        pageLayout->addWidget(form_);
        pageLayout->addStretch(1);
        formArea_->setWidget(page);
        connect(form_, &FieldEditor::changed, this, &RequestEditor::onFormChanged);
    } else {
        formPage->setText(tr("This command takes no request body"));
    }
    editor_->setEnabled(takesBody);
    editor_->setPlaceholderText(takesBody ? QString() : tr("This command takes no request body"));
    format_->setEnabled(takesBody);
    syncing_ = true;
    editor_->setPlainText(takesBody ? QStringLiteral("{}") : QString());
    syncing_ = false;
    send_->setEnabled(sendEnabled_);
    validateNow();
}

std::string RequestEditor::requestText() const {
    return editor_->toPlainText().toStdString();
}

void RequestEditor::setRequestText(const std::string& text) {
    syncing_ = true;
    editor_->setPlainText(QString::fromStdString(text));
    syncing_ = false;
    if (tabs_->currentIndex() == formTab && !loadFormFromText()) {
        tabs_->setCurrentIndex(jsonTab);
    }
    validateNow();
}

void RequestEditor::onFormChanged() {
    if (syncing_ || form_ == nullptr) {
        return;
    }
    syncing_ = true;
    editor_->setPlainText(QString::fromStdString(form_->json().dump(jsonIndent)));
    syncing_ = false;
}

void RequestEditor::onTabChanged(int index) {
    if (index != formTab || syncing_) {
        return;
    }
    if (!loadFormFromText()) {
        const QSignalBlocker blocker(tabs_);
        tabs_->setCurrentIndex(jsonTab);
    }
}

bool RequestEditor::loadFormFromText() {
    if (form_ == nullptr || command_ == nullptr || !command_->requestType) {
        return true;
    }
    const auto normalized = schema_.normalizeJson(*command_->requestType, requestText());
    if (!normalized) {
        showError(tr("Fix the JSON before switching to the form: %1").arg(QString::fromStdString(normalized.error())));
        return false;
    }
    syncing_ = true;
    form_->setJson(FormJson::parse(*normalized));
    syncing_ = false;
    return true;
}

void RequestEditor::setSendEnabled(bool enabled) {
    sendEnabled_ = enabled;
    send_->setEnabled(enabled && command_ != nullptr);
}

void RequestEditor::showError(const QString& message) {
    validation_->setStyleSheet(QStringLiteral("color: palette(bright-text); background: #b3261e; padding: 2px;"));
    validation_->setText(message);
}

void RequestEditor::validateNow() {
    validationTimer_->stop();
    if (command_ == nullptr) {
        validation_->clear();
        return;
    }
    const auto result = validator_(command_->id, requestText());
    if (result) {
        validation_->setStyleSheet({});
        validation_->setText(tr("✓ Valid request"));
    } else {
        showError(QString::fromStdString(result.error()));
    }
}

void RequestEditor::loadSelectedExample() {
    if (command_ == nullptr || !examples_->currentData().isValid()) {
        return;
    }
    const auto index = static_cast<std::size_t>(examples_->currentData().toInt());
    if (index < command_->requestExamples.size()) {
        setRequestText(command_->requestExamples[index].text);
    }
}

} // namespace ptslgui::ui
