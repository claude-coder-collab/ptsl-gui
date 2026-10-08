#include "../core/test_support.hpp"
#include "form_editor.hpp"
#include "ui_support.hpp"

#include <ptslgui/fake_session.hpp>
#include <ptslgui/sample.hpp>
#include <ptslgui/ui/main_window.hpp>

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTest>
#include <catch2/catch_test_macros.hpp>

#include <set>

using namespace ptslgui;
using namespace ptslgui::ui;

namespace {

constexpr int makeWidget = 0;

std::string normalized(const ProtoSchema& schema, const std::string& message, const std::string& json) {
    auto result = schema.normalizeJson(message, json);
    REQUIRE(result.has_value());
    return *result;
}

void checkRoundTrip(const ProtoSchema& schema, const std::string& message, std::uint32_t seeds) {
    MessageEditor editor(schema, message);
    for (std::uint32_t seed = 0; seed < seeds; ++seed) {
        const auto expected =
            normalized(schema, message, sampleJson(schema, message, {.seed = seed, .maxDepth = 3, .maxElements = 2}));
        editor.setJson(FormJson::parse(expected));
        const auto actual = editor.json().dump();
        INFO(message << " seed " << seed << "\nexpected " << expected << "\nform     " << actual);
        const auto reparsed = schema.normalizeJson(message, actual);
        REQUIRE(reparsed.has_value());
        CHECK(*reparsed == expected);
    }
}

template <typename T>
T* find(QWidget& root, const QString& name) {
    auto* widget = root.findChild<T*>(name);
    REQUIRE(widget != nullptr);
    return widget;
}

QLineEdit* lineIn(QWidget& root, const char* field) {
    return find<QLineEdit>(*find<FieldEditor>(root, QStringLiteral("field_%1").arg(field)), {});
}

} // namespace

TEST_CASE("Form round-trips samples for every fixture message") {
    const auto& schema = test::fixtureSchema();
    for (const auto& name : schema.messageNames()) {
        checkRoundTrip(schema, name, 60);
    }
}

#if defined(PTSLGUI_SDK_PROTO) && defined(PTSLGUI_SDK_CATALOG)
TEST_CASE("Form round-trips samples for every SDK request type", "[sdk]") {
    const auto schema = ProtoSchema::fromProtoText(test::readFile(PTSLGUI_SDK_PROTO));
    const auto catalog = CommandCatalog::fromJson(test::readFile(PTSLGUI_SDK_CATALOG));
    REQUIRE(schema.has_value());
    REQUIRE(catalog.has_value());
    std::set<std::string> types;
    for (const auto& command : catalog->commands()) {
        if (command.requestType) {
            types.insert(*command.requestType);
        }
    }
    REQUIRE(types.size() > 50);
    for (const auto& type : types) {
        checkRoundTrip(*schema, type, 4);
    }
}
#endif

TEST_CASE("Empty form produces an empty object") {
    const MessageEditor editor(test::fixtureSchema(), "MakeWidgetRequestBody");
    CHECK(editor.json() == FormJson::object());
    CHECK(editor.isDefault());
}

TEST_CASE("Scalar fields are omitted at their defaults") {
    MessageEditor editor(test::fixtureSchema(), "MakeWidgetRequestBody");
    lineIn(editor, "name")->setText("w");
    lineIn(editor, "size")->setText("0");
    CHECK(editor.json() == FormJson{{"name", "w"}});
    lineIn(editor, "size")->setText("12");
    lineIn(editor, "count")->setText("3");
    CHECK(editor.json() == FormJson{{"name", "w"}, {"size", 12}, {"count", 3}});

    auto* colour = find<QComboBox>(*find<FieldEditor>(editor, "field_colour"), {});
    CHECK(colour->currentText() == "WColour_None (default)");
    colour->setCurrentIndex(colour->findText("WColour_Red"));
    CHECK(editor.json()["colour"] == "WColour_Red");

    find<QCheckBox>(*find<FieldEditor>(editor, "field_visible"), {})->setChecked(true);
    CHECK(editor.json()["visible"] == true);
}

TEST_CASE("Integer fields reject non-numeric input and 64-bit values are strings") {
    MessageEditor editor(test::fixtureSchema(), "MakeWidgetRequestBody");
    auto* size = lineIn(editor, "size");
    QTest::keyClicks(size, "1a2-");
    CHECK(size->text() == "12");
    auto* count = lineIn(editor, "count");
    QTest::keyClicks(count, "-5");
    CHECK(count->text() == "5");
}

TEST_CASE("Optional scalars are included only when set") {
    MessageEditor editor(test::fixtureSchema(), "MakeWidgetRequestBody");
    auto* presence = find<QCheckBox>(editor, "set_opacity");
    auto* opacity = lineIn(editor, "opacity");
    CHECK_FALSE(opacity->isEnabled());
    CHECK_FALSE(editor.json().contains("opacity"));
    presence->setChecked(true);
    CHECK(opacity->isEnabled());
    CHECK(editor.json()["opacity"] == 0.0);
    opacity->setText("0.5");
    CHECK(editor.json()["opacity"] == 0.5);
}

TEST_CASE("Oneof selector chooses one branch") {
    MessageEditor editor(test::fixtureSchema(), "MakeWidgetRequestBody");
    auto* selector = find<QComboBox>(editor, "oneof_placement");
    CHECK(selector->count() == 3);
    selector->setCurrentIndex(selector->findText("slot"));
    CHECK(editor.json() == FormJson{{"slot", "0"}});
    lineIn(editor, "slot")->setText("42");
    CHECK(editor.json() == FormJson{{"slot", "42"}});
    selector->setCurrentIndex(selector->findText("folder"));
    CHECK(editor.json() == FormJson{{"folder", ""}});
    selector->setCurrentIndex(0);
    CHECK(editor.json() == FormJson::object());

    editor.setJson(FormJson{{"slot", "7"}});
    CHECK(selector->currentText() == "slot");
    CHECK(lineIn(editor, "slot")->text() == "7");
}

TEST_CASE("Nested messages are created on demand") {
    MessageEditor editor(test::fixtureSchema(), "MakeWidgetRequestBody");
    auto* field = find<FieldEditor>(editor, "field_geometry");
    auto* group = find<QGroupBox>(*field, "messageGroup");
    CHECK(field->findChild<MessageEditor*>() == nullptr);
    group->setChecked(true);
    CHECK(editor.json() == FormJson{{"geometry", FormJson::object()}});
    lineIn(*field, "width")->setText("2.5");
    CHECK(editor.json() == FormJson{{"geometry", {{"width", 2.5}}}});
    group->setChecked(false);
    CHECK(editor.json() == FormJson::object());
}

TEST_CASE("Repeated fields add and remove items") {
    MessageEditor editor(test::fixtureSchema(), "MakeWidgetRequestBody");
    auto* tags = find<RepeatedEditor>(editor, "field_tags");
    find<QPushButton>(*tags, "addButton")->click();
    find<QPushButton>(*tags, "addButton")->click();
    CHECK(tags->count() == 2);
    CHECK(editor.json() == FormJson{{"tags", {"", ""}}});
    tags->removeItem(0);
    CHECK(editor.json() == FormJson{{"tags", {""}}});

    auto* parts = find<RepeatedEditor>(editor, "field_parts");
    FieldEditor* part = parts->addItem();
    lineIn(*part, "height")->setText("3");
    CHECK(editor.json()["parts"] == FormJson::array({{{"height", 3.0}}}));
}

TEST_CASE("Map fields edit key and value pairs") {
    MessageEditor editor(test::fixtureSchema(), "MakeWidgetRequestBody");
    auto* attributes = find<MapEditor>(editor, "field_attributes");
    attributes->addEntry();
    find<QLineEdit>(*find<FieldEditor>(*attributes, "mapKey"), {})->setText("k");
    find<QLineEdit>(*find<FieldEditor>(*attributes, "mapValue"), {})->setText("v");
    CHECK(editor.json() == FormJson{{"attributes", {{"k", "v"}}}});

    editor.setJson(FormJson{{"attributes", {{"a", "1"}, {"b", "2"}}}});
    CHECK(attributes->count() == 2);
    editor.reset();
    CHECK(attributes->count() == 0);
}

TEST_CASE("Field tooltips describe type and comment") {
    MessageEditor editor(test::fixtureSchema(), "MakeWidgetRequestBody");
    CHECK(find<FieldEditor>(editor, "field_name")->toolTip() == "name (string)\n\nDisplay name of the widget.");
    CHECK(find<FieldEditor>(editor, "field_attributes")->toolTip().startsWith("attributes (map<string, string>)"));
    CHECK(find<FieldEditor>(editor, "field_tags")->toolTip().startsWith("tags (repeated string)"));
    CHECK(mapKeyString(FormJson(true)) == "true");
    CHECK(mapKeyString(FormJson(5)) == "5");
}

TEST_CASE("Request editor keeps form and JSON in sync") {
    test::TemporarySettings settings;
    FakePtslSession session;
    MainWindow window(test::fixtureCatalog(), test::fixtureSchema(), session, *settings.settings);
    REQUIRE(window.selectCommand(makeWidget));
    auto* tabs = find<QTabWidget>(window, "requestTabs");
    auto* json = find<QPlainTextEdit>(window, "requestEditor");
    auto* form = find<MessageEditor>(window, "requestForm");
    CHECK(tabs->currentIndex() == 0);

    lineIn(*form, "name")->setText("from form");
    CHECK(FormJson::parse(json->toPlainText().toStdString()) == FormJson{{"name", "from form"}});

    tabs->setCurrentIndex(1);
    json->setPlainText(R"({"size": 9, "colour": "WColour_Green"})");
    tabs->setCurrentIndex(0);
    CHECK(tabs->currentIndex() == 0);
    CHECK(lineIn(*form, "size")->text() == "9");
    CHECK(lineIn(*form, "name")->text().isEmpty());
    CHECK(form->json() == FormJson{{"size", 9}, {"colour", "WColour_Green"}});

    tabs->setCurrentIndex(1);
    json->setPlainText(R"({"size": "big"})");
    tabs->setCurrentIndex(0);
    CHECK(tabs->currentIndex() == 1);
    CHECK(find<QLabel>(window, "validationLabel")->text().contains("Fix the JSON"));

    find<QPushButton>(window, "loadExample")->click();
    CHECK(json->toPlainText().contains("w1"));
}
