#pragma once

#include <ptslgui/schema.hpp>
#include <ptslgui/time_format.hpp>

#include <QWidget>
#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QGroupBox;
class QStackedWidget;
class QVBoxLayout;

namespace ptslgui::ui {

using FormJson = nlohmann::ordered_json;

/// Editor for one value of a protobuf field. Values use the protobuf JSON mapping.
class FieldEditor : public QWidget {
    Q_OBJECT

public:
    using QWidget::QWidget;

    /// Loads a value (never null); invalid values reset the editor.
    virtual void setJson(const FormJson& value) = 0;
    virtual void reset() = 0;
    [[nodiscard]] virtual FormJson json() const = 0;
    /// True when the value equals the proto3 default and would be omitted from JSON.
    [[nodiscard]] virtual bool isDefault() const = 0;

    /// Shows the expected format of a time location; only string editors use it.
    virtual void setTimeFormat(const std::optional<TimeFormat>& /*format*/) {}

signals:
    void changed();
};

/// Creates an editor for a single value of field (ignoring its repeated / map label).
[[nodiscard]] FieldEditor* createValueEditor(const ProtoSchema& schema, const FieldSpec& field, QWidget* parent);

/// Creates an editor for the whole field, including repeated and map fields.
[[nodiscard]] FieldEditor* createFieldEditor(const ProtoSchema& schema, const FieldSpec& field, QWidget* parent);

/// Form for all fields of a message. Its JSON omits default values, like ProtoSchema::normalizeJson.
class MessageEditor final : public FieldEditor {
    Q_OBJECT

public:
    MessageEditor(const ProtoSchema& schema, const std::string& messageName, QWidget* parent = nullptr);

    void setJson(const FormJson& value) override;
    void reset() override;
    [[nodiscard]] FormJson json() const override;
    [[nodiscard]] bool isDefault() const override;

private:
    struct Row {
        const FieldSpec* field = nullptr;
        FieldEditor* editor = nullptr;
        QCheckBox* presence = nullptr;
    };

    struct Oneof {
        std::string name;
        std::vector<const FieldSpec*> members;
        std::vector<FieldEditor*> editors;
        QComboBox* selector = nullptr;
        QStackedWidget* stack = nullptr;
    };

    FieldEditor* oneofEditor(Oneof& oneof, std::size_t member);
    void connectTimeUnit();
    void updateTimeFormats();

    const ProtoSchema& schema_;
    MessageSpec spec_;
    std::vector<Row> rows_;
    std::vector<Oneof> oneofs_;
    const Row* timeUnit_ = nullptr;
};

/// Singular message field: a checkable group that creates its nested editor on first use.
class OptionalMessageEditor final : public FieldEditor {
    Q_OBJECT

public:
    OptionalMessageEditor(const ProtoSchema& schema, const FieldSpec& field, QWidget* parent = nullptr);

    void setJson(const FormJson& value) override;
    void reset() override;
    [[nodiscard]] FormJson json() const override;
    [[nodiscard]] bool isDefault() const override;

private:
    MessageEditor* ensureEditor();

    const ProtoSchema& schema_;
    std::string typeName_;
    QGroupBox* group_ = nullptr;
    QVBoxLayout* groupLayout_ = nullptr;
    MessageEditor* editor_ = nullptr;
};

/// Repeated (non-map) field: a list of value editors with add and remove buttons.
class RepeatedEditor final : public FieldEditor {
    Q_OBJECT

public:
    RepeatedEditor(const ProtoSchema& schema, FieldSpec field, QWidget* parent = nullptr);

    void setJson(const FormJson& value) override;
    void reset() override;
    [[nodiscard]] FormJson json() const override;
    [[nodiscard]] bool isDefault() const override;

    [[nodiscard]] int count() const { return static_cast<int>(items_.size()); }

    FieldEditor* addItem();
    void removeItem(int index);

private:
    struct Item {
        QWidget* row = nullptr;
        FieldEditor* editor = nullptr;
    };

    const ProtoSchema& schema_;
    FieldSpec field_;
    QVBoxLayout* itemsLayout_ = nullptr;
    std::vector<Item> items_;
};

/// Map field: rows of key and value editors.
class MapEditor final : public FieldEditor {
    Q_OBJECT

public:
    MapEditor(const ProtoSchema& schema, FieldSpec field, QWidget* parent = nullptr);

    void setJson(const FormJson& value) override;
    void reset() override;
    [[nodiscard]] FormJson json() const override;
    [[nodiscard]] bool isDefault() const override;

    [[nodiscard]] int count() const { return static_cast<int>(entries_.size()); }

    void addEntry();
    void removeEntry(int index);

private:
    struct Entry {
        QWidget* row = nullptr;
        FieldEditor* key = nullptr;
        FieldEditor* value = nullptr;
    };

    const ProtoSchema& schema_;
    FieldSpec field_;
    QVBoxLayout* entriesLayout_ = nullptr;
    std::vector<Entry> entries_;
};

/// Converts a map key editor value to the JSON object key.
[[nodiscard]] std::string mapKeyString(const FormJson& key);

} // namespace ptslgui::ui
