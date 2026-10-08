#include "test_support.hpp"

#include <ptslgui/schema.hpp>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>

using namespace ptslgui;
using nlohmann::json;

namespace {

const FieldSpec& field(const MessageSpec& message, std::string_view name) {
    const auto it = std::ranges::find(message.fields, name, &FieldSpec::name);
    if (it == message.fields.end()) {
        throw std::runtime_error("no field " + std::string(name));
    }
    return *it;
}

} // namespace

TEST_CASE("Schema parses the fixture proto") {
    const auto& schema = test::fixtureSchema();
    CHECK(schema.packageName() == "ptsl");
    CHECK(schema.hasMessage("MakeWidgetRequestBody"));
    CHECK(schema.hasMessage("ptsl.MakeWidgetRequestBody"));
    CHECK_FALSE(schema.hasMessage("NoSuchMessage"));
    const auto names = schema.messageNames();
    CHECK(std::ranges::find(names, "ptsl.Geometry") != names.end());
}

TEST_CASE("Schema describes every field kind") {
    const auto spec = test::fixtureSchema().message("MakeWidgetRequestBody");
    REQUIRE(spec.has_value());
    CHECK(spec->fullName == "ptsl.MakeWidgetRequestBody");
    CHECK(spec->name == "MakeWidgetRequestBody");
    CHECK(spec->comment == "Request for making a widget.");
    CHECK(spec->oneofs == std::vector<std::string>{"placement"});
    REQUIRE(spec->fields.size() == 13);

    const auto& name = field(*spec, "name");
    CHECK(name.kind == FieldKind::String);
    CHECK(name.number == 1);
    CHECK(name.comment == "Display name of the widget.");
    CHECK_FALSE(name.repeated);
    CHECK_FALSE(name.hasPresence);

    CHECK(field(*spec, "size").kind == FieldKind::Int32);
    CHECK(field(*spec, "size").comment == "Edge length in units.");
    CHECK(field(*spec, "visible").kind == FieldKind::Bool);
    CHECK(field(*spec, "slot").kind == FieldKind::Int64);
    CHECK(field(*spec, "count").kind == FieldKind::UInt32);
    CHECK(field(*spec, "opacity").kind == FieldKind::Float);
    CHECK(field(*spec, "payload").kind == FieldKind::Bytes);

    const auto& colour = field(*spec, "colour");
    CHECK(colour.kind == FieldKind::Enum);
    CHECK(colour.typeName == "ptsl.WidgetColour");
    REQUIRE(colour.enumValues.size() == 3);
    CHECK(colour.enumValues[1].name == "WColour_Red");
    CHECK(colour.enumValues[1].number == 1);

    const auto& geometry = field(*spec, "geometry");
    CHECK(geometry.kind == FieldKind::Message);
    CHECK(geometry.typeName == "ptsl.Geometry");
    CHECK(geometry.hasPresence);

    CHECK(field(*spec, "tags").repeated);
    CHECK(field(*spec, "parts").repeated);
    CHECK(field(*spec, "parts").kind == FieldKind::Message);

    CHECK(field(*spec, "folder").oneof == "placement");
    CHECK(field(*spec, "slot").oneof == "placement");
    CHECK(field(*spec, "opacity").oneof.empty());
    CHECK(field(*spec, "opacity").hasPresence);

    const auto& attributes = field(*spec, "attributes");
    CHECK(attributes.isMap);
    CHECK(attributes.repeated);
    REQUIRE(attributes.mapEntry.size() == 2);
    CHECK(attributes.mapEntry[0].name == "key");
    CHECK(attributes.mapEntry[1].kind == FieldKind::String);
}

TEST_CASE("Schema exposes enums by name") {
    const auto values = test::fixtureSchema().enumValues("WidgetColour");
    REQUIRE(values.has_value());
    CHECK(values->size() == 3);
    CHECK_FALSE(test::fixtureSchema().enumValues("Nope").has_value());
}

TEST_CASE("normalizeJson round-trips and drops defaults") {
    const auto& schema = test::fixtureSchema();
    const auto result = schema.normalizeJson("MakeWidgetRequestBody", R"({
        "name": "w", "size": 0, "colour": "WColour_Green", "tags": ["a", "b"],
        "geometry": {"width": 1.5}, "slot": "42", "attributes": {"k": "v"}, "opacity": 0
    })");
    REQUIRE(result.has_value());
    CHECK(json::parse(*result) == json::parse(R"({
        "name": "w", "colour": "WColour_Green", "tags": ["a", "b"], "geometry": {"width": 1.5},
        "slot": "42", "attributes": {"k": "v"}, "opacity": 0
    })"));
}

TEST_CASE("normalizeJson accepts json names, enum numbers and blank input") {
    const auto& schema = test::fixtureSchema();
    CHECK(schema.normalizeJson("MakeWidgetRequestBody", "").value() == "{}");
    CHECK(schema.normalizeJson("MakeWidgetRequestBody", "  \n").value() == "{}");
    CHECK(json::parse(schema.normalizeJson("ListWidgetsRequestBody", R"({"limit": 5})").value()) == json{{"limit", 5}});
    CHECK(json::parse(schema.normalizeJson("MakeWidgetRequestBody", R"({"colour": 1})").value()) ==
          json{{"colour", "WColour_Red"}});
}

TEST_CASE("normalizeJson pretty prints") {
    const auto result = test::fixtureSchema().normalizeJson("MakeWidgetRequestBody", R"({"name":"w","size":2})",
                                                            JsonFormat{.pretty = true});
    REQUIRE(result.has_value());
    CHECK(result->contains('\n'));
    CHECK(result->front() == '{');
    CHECK(result->back() == '}');
}

TEST_CASE("normalizeJson reports errors") {
    const auto& schema = test::fixtureSchema();
    CHECK_FALSE(schema.normalizeJson("NoSuchMessage", "{}").has_value());
    CHECK_FALSE(schema.normalizeJson("MakeWidgetRequestBody", "{").has_value());
    CHECK_FALSE(schema.normalizeJson("MakeWidgetRequestBody", R"({"unknown_field": 1})").has_value());
    CHECK_FALSE(schema.normalizeJson("MakeWidgetRequestBody", R"({"size": "big"})").has_value());
    CHECK_FALSE(schema.normalizeJson("MakeWidgetRequestBody", R"({"colour": "WColour_Purple"})").has_value());
    CHECK_FALSE(schema.normalizeJson("MakeWidgetRequestBody", "[]").has_value());
}

TEST_CASE("Schema reports parse and build errors") {
    const auto syntax = ProtoSchema::fromProtoText("syntax = \"proto3\"; message {", "bad.proto");
    REQUIRE_FALSE(syntax.has_value());
    CHECK(syntax.error().contains("bad.proto"));

    const auto unresolved = ProtoSchema::fromProtoText("syntax = \"proto3\"; message A { Missing m = 1; }");
    REQUIRE_FALSE(unresolved.has_value());
    CHECK(unresolved.error().contains("Missing"));
}

TEST_CASE("Schema is movable") {
    auto first = ProtoSchema::fromProtoText("syntax = \"proto3\"; package p; message A { int32 x = 1; }");
    REQUIRE(first.has_value());
    const ProtoSchema moved = std::move(*first);
    CHECK(moved.hasMessage("A"));
    CHECK(moved.normalizeJson("p.A", R"({"x": 3})").value() == R"({"x":3})");
}
