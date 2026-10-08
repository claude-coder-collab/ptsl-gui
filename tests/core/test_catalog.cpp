#include "test_support.hpp"

#include <ptslgui/catalog.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <ranges>

using namespace ptslgui;

namespace {

std::vector<std::string> names(const std::vector<const CommandInfo*>& commands) {
    std::vector<std::string> result;
    std::ranges::transform(commands, std::back_inserter(result), &CommandInfo::name);
    return result;
}

} // namespace

TEST_CASE("Catalog loads the generated fixture catalog") {
    const auto& catalog = test::fixtureCatalog();
    REQUIRE(catalog.commands().size() == 5);
    CHECK(catalog.categories() == std::vector<std::string>{"editing", "queries", "utility", "widgets"});

    const CommandInfo* make = catalog.findById(0);
    REQUIRE(make != nullptr);
    CHECK(make->name == "CId_MakeWidget");
    CHECK(make->displayName == "Make Widget");
    CHECK(make->requestType == "MakeWidgetRequestBody");
    CHECK(make->responseType == "MakeWidgetResponseBody");
    CHECK(make->since == Version{.year = 2023, .minor = 3, .revision = 0});
    REQUIRE(make->requestExamples.size() == 2);
    CHECK(make->requestExamples[0].caption == "Simple widget.");
    CHECK(make->requestExamples[1].repaired);
    CHECK(make->deprecatedAliases == std::vector<std::string>{"MakeWidget"});

    const CommandInfo* ping = catalog.findById(2);
    REQUIRE(ping != nullptr);
    CHECK_FALSE(ping->requestType.has_value());
    CHECK_FALSE(ping->since.has_value());
    CHECK(catalog.findById(99) == nullptr);
}

TEST_CASE("Catalog finds commands by name, short name and alias") {
    const auto& catalog = test::fixtureCatalog();
    CHECK(catalog.findByName("CId_ListWidgets")->id == 1);
    CHECK(catalog.findByName("ListWidgets")->id == 1);
    CHECK(catalog.findByName("OldDeleteWidget")->id == 3);
    CHECK(catalog.findByName("Nope") == nullptr);
}

TEST_CASE("Catalog search matches all terms case-insensitively") {
    const auto& catalog = test::fixtureCatalog();
    CHECK(names(catalog.search({})).size() == 5);
    CHECK(names(catalog.search({.text = "WIDGET", .category = {}, .includeDeprecated = true})) ==
          std::vector<std::string>{"CId_MakeWidget", "CId_ListWidgets", "CId_DeleteWidget"});
    CHECK(names(catalog.search({.text = "make   new", .category = {}, .includeDeprecated = true})) ==
          std::vector<std::string>{"CId_MakeWidget"});
    CHECK(names(catalog.search({.text = "host", .category = {}, .includeDeprecated = true})) ==
          std::vector<std::string>{"CId_Ping"});
    CHECK(catalog.search({.text = "widget zebra", .category = {}, .includeDeprecated = true}).empty());
}

TEST_CASE("Catalog search filters by category and deprecation") {
    const auto& catalog = test::fixtureCatalog();
    CHECK(names(catalog.search({.text = {}, .category = "queries", .includeDeprecated = true})) ==
          std::vector<std::string>{"CId_ListWidgets"});
    CHECK(names(catalog.search({.text = "widget", .category = "widgets", .includeDeprecated = false})) ==
          std::vector<std::string>{"CId_MakeWidget", "CId_ListWidgets"});
}

TEST_CASE("Command classification") {
    CommandInfo command;
    command.categories = {"session_read"};
    CHECK_FALSE(command.isMutating());
    command.categories = {"session_read", "editing"};
    CHECK(command.isMutating());
    CHECK(command.hasCategory("editing"));

    const Version host{.year = 2025, .minor = 6, .revision = 0};
    CHECK_FALSE(command.isUnsupportedBy(host));
    command.since = Version{.year = 2025, .minor = 10, .revision = 0};
    CHECK(command.isUnsupportedBy(host));
    command.since = Version{.year = 2025, .minor = 6, .revision = 0};
    CHECK_FALSE(command.isUnsupportedBy(host));
}

TEST_CASE("Catalog rejects invalid documents") {
    CHECK_FALSE(CommandCatalog::fromJson("not json").has_value());
    CHECK_FALSE(CommandCatalog::fromJson(R"({"commands": []})").has_value());
    const auto wrongVersion = CommandCatalog::fromJson(R"({"schema_version": 2, "commands": []})");
    REQUIRE_FALSE(wrongVersion.has_value());
    CHECK(wrongVersion.error().contains("schema version 2"));
    CHECK_FALSE(CommandCatalog::fromJson(R"({"schema_version": 1, "commands": [{"name": "CId_X"}]})").has_value());
}

TEST_CASE("Catalog tolerates minimal command entries") {
    const auto catalog =
        CommandCatalog::fromJson(R"({"schema_version": 1, "commands": [{"id": 7, "name": "CId_X", "since": "bad"}]})");
    REQUIRE(catalog.has_value());
    const CommandInfo* command = catalog->findById(7);
    REQUIRE(command != nullptr);
    CHECK(command->displayName == "CId_X");
    CHECK_FALSE(command->since.has_value());
    CHECK(command->requestExamples.empty());
}
