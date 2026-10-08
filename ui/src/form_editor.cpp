#include "form_editor.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <limits>

namespace ptslgui::ui {
namespace {

QString shortTypeName(const std::string& fullName) {
    const auto dot = fullName.rfind('.');
    return QString::fromStdString(dot == std::string::npos ? fullName : fullName.substr(dot + 1));
}

QString kindName(const FieldSpec& field) {
    switch (field.kind) {
    case FieldKind::Bool:
        return QStringLiteral("bool");
    case FieldKind::Int32:
        return QStringLiteral("int32");
    case FieldKind::Int64:
        return QStringLiteral("int64");
    case FieldKind::UInt32:
        return QStringLiteral("uint32");
    case FieldKind::UInt64:
        return QStringLiteral("uint64");
    case FieldKind::Float:
        return QStringLiteral("float");
    case FieldKind::Double:
        return QStringLiteral("double");
    case FieldKind::String:
        return QStringLiteral("string");
    case FieldKind::Bytes:
        return QStringLiteral("bytes (base64)");
    case FieldKind::Enum:
        return QStringLiteral("enum %1").arg(shortTypeName(field.typeName));
    case FieldKind::Message:
        return QStringLiteral("message %1").arg(shortTypeName(field.typeName));
    }
    return {};
}

QString typeDescription(const FieldSpec& field) {
    if (field.isMap && field.mapEntry.size() == 2) {
        return QStringLiteral("map<%1, %2>").arg(kindName(field.mapEntry[0]), kindName(field.mapEntry[1]));
    }
    QString description = kindName(field);
    if (field.repeated) {
        description.prepend(QStringLiteral("repeated "));
    } else if (field.hasPresence && field.kind != FieldKind::Message && field.oneof.empty()) {
        description.prepend(QStringLiteral("optional "));
    }
    return description;
}

QString fieldToolTip(const FieldSpec& field) {
    QString tooltip = QStringLiteral("%1 (%2)").arg(QString::fromStdString(field.name), typeDescription(field));
    if (!field.comment.empty()) {
        tooltip += QStringLiteral("\n\n") + QString::fromStdString(field.comment);
    }
    return tooltip;
}

bool isPathField(const FieldSpec& field) {
    const QString name = QString::fromStdString(field.name).toLower();
    return field.kind == FieldKind::String &&
           (name.contains(QStringLiteral("path")) || name.contains(QStringLiteral("location")) ||
            name.contains(QStringLiteral("folder")) || name.contains(QStringLiteral("directory")));
}

bool isDirectoryField(const FieldSpec& field) {
    const QString name = QString::fromStdString(field.name).toLower();
    return name.contains(QStringLiteral("folder")) || name.contains(QStringLiteral("directory")) ||
           name.contains(QStringLiteral("location"));
}

constexpr int comboMinimumCharacters = 12;

void allowNarrowCombo(QComboBox* combo) {
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo->setMinimumContentsLength(comboMinimumCharacters);
}

QHBoxLayout* compactRow(QWidget* parent) {
    auto* layout = new QHBoxLayout(parent);
    layout->setContentsMargins(0, 0, 0, 0);
    return layout;
}

class BoolEditor final : public FieldEditor {
public:
    explicit BoolEditor(QWidget* parent) : FieldEditor(parent), check_(new QCheckBox(this)) {
        compactRow(this)->addWidget(check_);
        connect(check_, &QCheckBox::toggled, this, &FieldEditor::changed);
    }

    void setJson(const FormJson& value) override {
        check_->setChecked(value.is_boolean() ? value.get<bool>() : value == "true");
    }

    void reset() override { check_->setChecked(false); }

    [[nodiscard]] FormJson json() const override { return check_->isChecked(); }

    [[nodiscard]] bool isDefault() const override { return !check_->isChecked(); }

private:
    QCheckBox* check_;
};

class LineEditor : public FieldEditor {
public:
    explicit LineEditor(QWidget* parent) : FieldEditor(parent), line_(new QLineEdit(this)) {
        compactRow(this)->addWidget(line_);
        connect(line_, &QLineEdit::textChanged, this, &FieldEditor::changed);
    }

    void reset() override { line_->clear(); }

protected:
    [[nodiscard]] QLineEdit* line() const { return line_; }

private:
    QLineEdit* line_;
};

class IntegerEditor final : public LineEditor {
public:
    IntegerEditor(FieldKind kind, QWidget* parent) : LineEditor(parent), kind_(kind) {
        const bool isSigned = kind == FieldKind::Int32 || kind == FieldKind::Int64;
        line()->setValidator(new QRegularExpressionValidator(
            QRegularExpression(isSigned ? QStringLiteral("-?\\d*") : QStringLiteral("\\d*")), line()));
        line()->setPlaceholderText(QStringLiteral("0"));
    }

    void setJson(const FormJson& value) override {
        if (value.is_number_integer()) {
            line()->setText(QString::fromStdString(value.dump()));
        } else if (value.is_string()) {
            line()->setText(QString::fromStdString(value.get<std::string>()));
        } else {
            reset();
        }
    }

    [[nodiscard]] FormJson json() const override {
        const QString text = line()->text().isEmpty() ? QStringLiteral("0") : line()->text();
        if (kind_ == FieldKind::Int64 || kind_ == FieldKind::UInt64) {
            return text.toStdString();
        }
        bool ok = false;
        const qlonglong number = text.toLongLong(&ok);
        if (!ok) {
            return text.toStdString();
        }
        return static_cast<std::int64_t>(number);
    }

    [[nodiscard]] bool isDefault() const override {
        const QString text = line()->text();
        return text.isEmpty() || text == QStringLiteral("0") || text == QStringLiteral("-0");
    }

private:
    FieldKind kind_;
};

class FloatEditor final : public LineEditor {
public:
    explicit FloatEditor(QWidget* parent) : LineEditor(parent) {
        line()->setValidator(new QRegularExpressionValidator(
            QRegularExpression(QStringLiteral("-?(\\d+\\.?\\d*|\\.\\d+)?([eE][-+]?\\d*)?")), line()));
        line()->setPlaceholderText(QStringLiteral("0"));
    }

    void setJson(const FormJson& value) override {
        if (value.is_number()) {
            line()->setText(QString::number(value.get<double>(), 'g', std::numeric_limits<double>::max_digits10));
        } else if (value.is_string()) {
            line()->setText(QString::fromStdString(value.get<std::string>()));
        } else {
            reset();
        }
    }

    [[nodiscard]] FormJson json() const override {
        bool ok = false;
        const double number = line()->text().toDouble(&ok);
        return ok ? number : 0.0;
    }

    [[nodiscard]] bool isDefault() const override {
        bool ok = false;
        const double number = line()->text().toDouble(&ok);
        return !ok || number == 0.0;
    }
};

class StringEditor final : public LineEditor {
public:
    StringEditor(const FieldSpec& field, QWidget* parent) : LineEditor(parent) {
        if (field.kind == FieldKind::Bytes) {
            line()->setPlaceholderText(QObject::tr("base64"));
        }
        if (isPathField(field)) {
            auto* browse = new QToolButton(this);
            browse->setText(QStringLiteral("…"));
            browse->setToolTip(QObject::tr("Browse"));
            browse->setObjectName("browseButton");
            layout()->addWidget(browse);
            const bool directory = isDirectoryField(field);
            connect(browse, &QToolButton::clicked, this, [this, directory] {
                const QString path = directory ? QFileDialog::getExistingDirectory(this, QString(), line()->text())
                                               : QFileDialog::getOpenFileName(this, QString(), line()->text());
                if (!path.isEmpty()) {
                    line()->setText(path);
                }
            });
        }
    }

    void setJson(const FormJson& value) override {
        line()->setText(value.is_string() ? QString::fromStdString(value.get<std::string>()) : QString());
    }

    [[nodiscard]] FormJson json() const override { return line()->text().toStdString(); }

    [[nodiscard]] bool isDefault() const override { return line()->text().isEmpty(); }
};

class EnumEditor final : public FieldEditor {
public:
    EnumEditor(const FieldSpec& field, QWidget* parent) : FieldEditor(parent), combo_(new QComboBox(this)) {
        compactRow(this)->addWidget(combo_);
        allowNarrowCombo(combo_);
        for (const auto& value : field.enumValues) {
            const QString name = QString::fromStdString(value.name);
            combo_->addItem(value.number == 0 ? QObject::tr("%1 (default)").arg(name) : name, value.number);
            combo_->setItemData(combo_->count() - 1, name, Qt::UserRole + 1);
        }
        reset();
        connect(combo_, &QComboBox::currentIndexChanged, this, &FieldEditor::changed);
    }

    void setJson(const FormJson& value) override {
        int index = -1;
        if (value.is_string()) {
            index = combo_->findData(QString::fromStdString(value.get<std::string>()), Qt::UserRole + 1);
        } else if (value.is_number_integer()) {
            index = combo_->findData(value.get<int>());
        }
        if (index < 0) {
            reset();
        } else {
            combo_->setCurrentIndex(index);
        }
    }

    void reset() override { combo_->setCurrentIndex(std::max(0, combo_->findData(0))); }

    [[nodiscard]] FormJson json() const override {
        if (combo_->currentIndex() < 0) {
            return 0;
        }
        return combo_->currentData(Qt::UserRole + 1).toString().toStdString();
    }

    [[nodiscard]] bool isDefault() const override {
        return combo_->currentIndex() < 0 || combo_->currentData().toInt() == 0;
    }

private:
    QComboBox* combo_;
};

QToolButton* removeButton(QWidget* parent) {
    auto* button = new QToolButton(parent);
    button->setText(QStringLiteral("−"));
    button->setToolTip(QObject::tr("Remove"));
    button->setObjectName("removeButton");
    return button;
}

QPushButton* addButton(const QString& text, QWidget* parent) {
    auto* button = new QPushButton(text, parent);
    button->setObjectName("addButton");
    return button;
}

} // namespace

std::string mapKeyString(const FormJson& key) {
    if (key.is_string()) {
        return key.get<std::string>();
    }
    return key.dump();
}

// NOLINTNEXTLINE(misc-no-recursion)
FieldEditor* createValueEditor(const ProtoSchema& schema, const FieldSpec& field, QWidget* parent) {
    switch (field.kind) {
    case FieldKind::Bool:
        return new BoolEditor(parent);
    case FieldKind::Int32:
    case FieldKind::Int64:
    case FieldKind::UInt32:
    case FieldKind::UInt64:
        return new IntegerEditor(field.kind, parent);
    case FieldKind::Float:
    case FieldKind::Double:
        return new FloatEditor(parent);
    case FieldKind::String:
    case FieldKind::Bytes:
        return new StringEditor(field, parent);
    case FieldKind::Enum:
        return new EnumEditor(field, parent);
    case FieldKind::Message:
        return new MessageEditor(schema, field.typeName, parent);
    }
    return new StringEditor(field, parent);
}

// NOLINTNEXTLINE(misc-no-recursion)
FieldEditor* createFieldEditor(const ProtoSchema& schema, const FieldSpec& field, QWidget* parent) {
    if (field.isMap) {
        return new MapEditor(schema, field, parent);
    }
    if (field.repeated) {
        return new RepeatedEditor(schema, field, parent);
    }
    if (field.kind == FieldKind::Message) {
        return new OptionalMessageEditor(schema, field, parent);
    }
    return createValueEditor(schema, field, parent);
}

// NOLINTNEXTLINE(misc-no-recursion)
MessageEditor::MessageEditor(const ProtoSchema& schema, const std::string& messageName, QWidget* parent)
    : FieldEditor(parent)
    , schema_(schema)
    , spec_(schema.message(messageName).value_or(MessageSpec{})) {
    setObjectName(QString::fromStdString(spec_.name));
    auto* layout = new QFormLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->setRowWrapPolicy(QFormLayout::DontWrapRows);
    if (!spec_.comment.empty()) {
        setToolTip(QString::fromStdString(spec_.comment));
    }

    for (const auto& oneofName : spec_.oneofs) {
        Oneof oneof;
        oneof.name = oneofName;
        for (const auto& field : spec_.fields) {
            if (field.oneof == oneofName) {
                oneof.members.push_back(&field);
            }
        }
        oneof.editors.assign(oneof.members.size(), nullptr);
        oneofs_.push_back(std::move(oneof));
    }

    std::vector<std::string> placedOneofs;
    for (const auto& field : spec_.fields) {
        if (!field.oneof.empty()) {
            if (std::ranges::find(placedOneofs, field.oneof) != placedOneofs.end()) {
                continue;
            }
            placedOneofs.push_back(field.oneof);
            const auto oneofIndex =
                static_cast<std::size_t>(std::ranges::find(oneofs_, field.oneof, &Oneof::name) - oneofs_.begin());
            auto* container = new QWidget(this);
            auto* containerLayout = new QVBoxLayout(container);
            containerLayout->setContentsMargins(0, 0, 0, 0);
            auto* selector = new QComboBox(container);
            selector->setObjectName(QStringLiteral("oneof_%1").arg(QString::fromStdString(field.oneof)));
            allowNarrowCombo(selector);
            selector->addItem(tr("(none)"));
            QStringList tooltip{tr("One of:")};
            for (const FieldSpec* member : oneofs_[oneofIndex].members) {
                selector->addItem(QString::fromStdString(member->name));
                tooltip << fieldToolTip(*member);
            }
            selector->setToolTip(tooltip.join('\n'));
            auto* stack = new QStackedWidget(container);
            stack->addWidget(new QWidget(stack));
            for (std::size_t i = 0; i < oneofs_[oneofIndex].members.size(); ++i) {
                stack->addWidget(new QWidget(stack));
            }
            containerLayout->addWidget(selector);
            containerLayout->addWidget(stack);
            oneofs_[oneofIndex].selector = selector;
            oneofs_[oneofIndex].stack = stack;
            connect(selector, &QComboBox::currentIndexChanged, this, [this, oneofIndex](int index) {
                Oneof& oneof = oneofs_[oneofIndex];
                if (index > 0) {
                    oneofEditor(oneof, static_cast<std::size_t>(index - 1));
                }
                oneof.stack->setCurrentIndex(std::max(index, 0));
                emit changed();
            });
            auto* label = new QLabel(QStringLiteral("<i>%1</i>").arg(QString::fromStdString(field.oneof)), this);
            label->setToolTip(selector->toolTip());
            layout->addRow(label, container);
            continue;
        }

        Row row{.field = &field, .editor = nullptr, .presence = nullptr};
        const bool optionalScalar = field.hasPresence && field.kind != FieldKind::Message && !field.repeated;
        QWidget* widget = nullptr;
        if (optionalScalar) {
            widget = new QWidget(this);
            auto* rowLayout = compactRow(widget);
            row.presence = new QCheckBox(widget);
            row.presence->setToolTip(tr("Include this optional field"));
            row.presence->setObjectName(QStringLiteral("set_%1").arg(QString::fromStdString(field.name)));
            row.editor = createFieldEditor(schema_, field, widget);
            row.editor->setEnabled(false);
            rowLayout->addWidget(row.presence);
            rowLayout->addWidget(row.editor, 1);
            connect(row.presence, &QCheckBox::toggled, row.editor, &QWidget::setEnabled);
            connect(row.presence, &QCheckBox::toggled, this, &FieldEditor::changed);
        } else {
            row.editor = createFieldEditor(schema_, field, this);
            widget = row.editor;
        }
        row.editor->setObjectName(QStringLiteral("field_%1").arg(QString::fromStdString(field.name)));
        connect(row.editor, &FieldEditor::changed, this, &FieldEditor::changed);
        auto* label = new QLabel(QString::fromStdString(field.name), this);
        label->setToolTip(fieldToolTip(field));
        widget->setToolTip(fieldToolTip(field));
        layout->addRow(label, widget);
        rows_.push_back(row);
    }
    if (spec_.fields.empty()) {
        layout->addRow(new QLabel(tr("<i>No fields</i>"), this));
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
FieldEditor* MessageEditor::oneofEditor(Oneof& oneof, std::size_t member) {
    if (oneof.editors[member] == nullptr) {
        const FieldSpec& field = *oneof.members[member];
        FieldEditor* editor = field.isMap || field.repeated ? createFieldEditor(schema_, field, oneof.stack)
                                                            : createValueEditor(schema_, field, oneof.stack);
        editor->setObjectName(QStringLiteral("field_%1").arg(QString::fromStdString(field.name)));
        editor->setToolTip(fieldToolTip(field));
        const int page = static_cast<int>(member) + 1;
        QWidget* placeholder = oneof.stack->widget(page);
        oneof.stack->removeWidget(placeholder);
        placeholder->deleteLater();
        oneof.stack->insertWidget(page, editor);
        connect(editor, &FieldEditor::changed, this, &FieldEditor::changed);
        oneof.editors[member] = editor;
    }
    return oneof.editors[member];
}

// NOLINTNEXTLINE(misc-no-recursion)
void MessageEditor::setJson(const FormJson& value) {
    if (!value.is_object()) {
        reset();
        return;
    }
    const auto find = [&value](const FieldSpec& field) -> const FormJson* {
        for (const auto& key : {field.name, field.jsonName}) {
            if (const auto it = value.find(key); it != value.end() && !it->is_null()) {
                return &*it;
            }
        }
        return nullptr;
    };
    for (const auto& row : rows_) {
        const FormJson* item = find(*row.field);
        if (row.presence != nullptr) {
            row.presence->setChecked(item != nullptr);
        }
        if (item != nullptr) {
            row.editor->setJson(*item);
        } else {
            row.editor->reset();
        }
    }
    for (auto& oneof : oneofs_) {
        int selected = 0;
        for (std::size_t i = 0; i < oneof.members.size(); ++i) {
            if (const FormJson* item = find(*oneof.members[i])) {
                oneofEditor(oneof, i)->setJson(*item);
                selected = static_cast<int>(i) + 1;
                break;
            }
        }
        oneof.selector->setCurrentIndex(selected);
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void MessageEditor::reset() {
    for (const auto& row : rows_) {
        if (row.presence != nullptr) {
            row.presence->setChecked(false);
        }
        row.editor->reset();
    }
    for (const auto& oneof : oneofs_) {
        oneof.selector->setCurrentIndex(0);
        for (FieldEditor* editor : oneof.editors) {
            if (editor != nullptr) {
                editor->reset();
            }
        }
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
FormJson MessageEditor::json() const {
    FormJson object = FormJson::object();
    for (const auto& field : spec_.fields) {
        if (!field.oneof.empty()) {
            const auto& oneof = *std::ranges::find(oneofs_, field.oneof, &Oneof::name);
            const int selected = oneof.selector->currentIndex();
            if (selected > 0 && oneof.members[static_cast<std::size_t>(selected - 1)] == &field) {
                object[field.name] = oneof.editors[static_cast<std::size_t>(selected - 1)]->json();
            }
            continue;
        }
        const auto& row = *std::ranges::find(rows_, &field, &Row::field);
        const bool include = row.presence != nullptr ? row.presence->isChecked() : !row.editor->isDefault();
        if (include) {
            object[field.name] = row.editor->json();
        }
    }
    return object;
}

// NOLINTNEXTLINE(misc-no-recursion)
bool MessageEditor::isDefault() const {
    return json().empty();
}

OptionalMessageEditor::OptionalMessageEditor(const ProtoSchema& schema, const FieldSpec& field, QWidget* parent)
    : FieldEditor(parent)
    , schema_(schema)
    , typeName_(field.typeName)
    , group_(new QGroupBox(shortTypeName(field.typeName), this))
    , groupLayout_(new QVBoxLayout(group_)) {
    group_->setCheckable(true);
    group_->setChecked(false);
    group_->setFlat(true);
    group_->setObjectName("messageGroup");
    groupLayout_->setContentsMargins(8, 4, 4, 4);
    compactRow(this)->addWidget(group_);
    connect(group_, &QGroupBox::toggled, this, [this](bool checked) {
        group_->setFlat(!checked);
        if (checked) {
            ensureEditor()->setVisible(true);
        } else if (editor_ != nullptr) {
            editor_->setVisible(false);
        }
        emit changed();
    });
}

// NOLINTNEXTLINE(misc-no-recursion)
MessageEditor* OptionalMessageEditor::ensureEditor() {
    if (editor_ == nullptr) {
        editor_ = new MessageEditor(schema_, typeName_, group_);
        groupLayout_->addWidget(editor_);
        connect(editor_, &FieldEditor::changed, this, &FieldEditor::changed);
    }
    return editor_;
}

// NOLINTNEXTLINE(misc-no-recursion)
void OptionalMessageEditor::setJson(const FormJson& value) {
    group_->setChecked(true);
    ensureEditor()->setJson(value);
}

// NOLINTNEXTLINE(misc-no-recursion)
void OptionalMessageEditor::reset() {
    if (editor_ != nullptr) {
        editor_->reset();
    }
    group_->setChecked(false);
}

// NOLINTNEXTLINE(misc-no-recursion)
FormJson OptionalMessageEditor::json() const {
    return editor_ != nullptr ? editor_->json() : FormJson::object();
}

bool OptionalMessageEditor::isDefault() const {
    return !group_->isChecked();
}

RepeatedEditor::RepeatedEditor(const ProtoSchema& schema, FieldSpec field, QWidget* parent)
    : FieldEditor(parent)
    , schema_(schema)
    , field_(std::move(field))
    , itemsLayout_(new QVBoxLayout) {
    field_.repeated = false;
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(itemsLayout_);
    auto* add = addButton(tr("Add %1").arg(kindName(field_)), this);
    layout->addWidget(add, 0, Qt::AlignLeft);
    connect(add, &QPushButton::clicked, this, [this] {
        addItem();
        emit changed();
    });
}

// NOLINTNEXTLINE(misc-no-recursion)
FieldEditor* RepeatedEditor::addItem() {
    auto* row = new QWidget(this);
    auto* rowLayout = compactRow(row);
    FieldEditor* editor = createValueEditor(schema_, field_, row);
    auto* remove = removeButton(row);
    if (field_.kind == FieldKind::Message) {
        auto* frame = new QGroupBox(tr("[%1]").arg(items_.size()), row);
        auto* frameLayout = new QVBoxLayout(frame);
        frameLayout->setContentsMargins(8, 4, 4, 4);
        editor->setParent(frame);
        frameLayout->addWidget(editor);
        rowLayout->addWidget(frame, 1);
    } else {
        rowLayout->addWidget(editor, 1);
    }
    rowLayout->addWidget(remove, 0, Qt::AlignTop);
    itemsLayout_->addWidget(row);
    items_.push_back(Item{.row = row, .editor = editor});
    connect(editor, &FieldEditor::changed, this, &FieldEditor::changed);
    connect(remove, &QToolButton::clicked, this, [this, row] {
        const auto it = std::ranges::find(items_, row, &Item::row);
        if (it != items_.end()) {
            removeItem(static_cast<int>(it - items_.begin()));
            emit changed();
        }
    });
    return editor;
}

void RepeatedEditor::removeItem(int index) {
    if (index < 0 || index >= count()) {
        return;
    }
    const auto it = items_.begin() + index;
    it->row->hide();
    it->row->deleteLater();
    items_.erase(it);
}

// NOLINTNEXTLINE(misc-no-recursion)
void RepeatedEditor::setJson(const FormJson& value) {
    reset();
    if (!value.is_array()) {
        return;
    }
    for (const auto& item : value) {
        addItem()->setJson(item);
    }
}

void RepeatedEditor::reset() {
    while (!items_.empty()) {
        removeItem(count() - 1);
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
FormJson RepeatedEditor::json() const {
    FormJson array = FormJson::array();
    for (const auto& item : items_) {
        array.push_back(item.editor->json());
    }
    return array;
}

bool RepeatedEditor::isDefault() const {
    return items_.empty();
}

MapEditor::MapEditor(const ProtoSchema& schema, FieldSpec field, QWidget* parent)
    : FieldEditor(parent)
    , schema_(schema)
    , field_(std::move(field))
    , entriesLayout_(new QVBoxLayout) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(entriesLayout_);
    auto* add = addButton(tr("Add entry"), this);
    layout->addWidget(add, 0, Qt::AlignLeft);
    connect(add, &QPushButton::clicked, this, [this] {
        addEntry();
        emit changed();
    });
}

// NOLINTNEXTLINE(misc-no-recursion)
void MapEditor::addEntry() {
    if (field_.mapEntry.size() != 2) {
        return;
    }
    auto* row = new QWidget(this);
    auto* rowLayout = compactRow(row);
    FieldEditor* key = createValueEditor(schema_, field_.mapEntry[0], row);
    FieldEditor* value = createValueEditor(schema_, field_.mapEntry[1], row);
    key->setObjectName("mapKey");
    value->setObjectName("mapValue");
    auto* remove = removeButton(row);
    rowLayout->addWidget(key, 1);
    rowLayout->addWidget(new QLabel(QStringLiteral("→"), row));
    rowLayout->addWidget(value, 2);
    rowLayout->addWidget(remove, 0, Qt::AlignTop);
    entriesLayout_->addWidget(row);
    entries_.push_back(Entry{.row = row, .key = key, .value = value});
    connect(key, &FieldEditor::changed, this, &FieldEditor::changed);
    connect(value, &FieldEditor::changed, this, &FieldEditor::changed);
    connect(remove, &QToolButton::clicked, this, [this, row] {
        const auto it = std::ranges::find(entries_, row, &Entry::row);
        if (it != entries_.end()) {
            removeEntry(static_cast<int>(it - entries_.begin()));
            emit changed();
        }
    });
}

void MapEditor::removeEntry(int index) {
    if (index < 0 || index >= count()) {
        return;
    }
    const auto it = entries_.begin() + index;
    it->row->hide();
    it->row->deleteLater();
    entries_.erase(it);
}

// NOLINTNEXTLINE(misc-no-recursion)
void MapEditor::setJson(const FormJson& value) {
    reset();
    if (!value.is_object()) {
        return;
    }
    for (const auto& [key, item] : value.items()) {
        addEntry();
        entries_.back().key->setJson(FormJson(key));
        entries_.back().value->setJson(item);
    }
}

void MapEditor::reset() {
    while (!entries_.empty()) {
        removeEntry(count() - 1);
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
FormJson MapEditor::json() const {
    FormJson object = FormJson::object();
    for (const auto& entry : entries_) {
        object[mapKeyString(entry.key->json())] = entry.value->json();
    }
    return object;
}

bool MapEditor::isDefault() const {
    return entries_.empty();
}

} // namespace ptslgui::ui
