#if defined(PTSLGUI_SDK_PROTO) && defined(PTSLGUI_SDK_CATALOG)

#include "test_support.hpp"

#include <ptslgui/catalog.hpp>
#include <ptslgui/schema.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace ptslgui;

TEST_CASE("SDK proto: every catalog type resolves and examples validate", "[sdk]") {
    const auto schema = ProtoSchema::fromProtoText(test::readFile(PTSLGUI_SDK_PROTO));
    REQUIRE(schema.has_value());
    const auto catalog = CommandCatalog::fromJson(test::readFile(PTSLGUI_SDK_CATALOG));
    REQUIRE(catalog.has_value());
    REQUIRE(catalog->commands().size() > 100);

    std::size_t examples = 0;
    std::size_t rejected = 0;
    for (const auto& command : catalog->commands()) {
        for (const auto& type : {command.requestType, command.responseType}) {
            if (type) {
                INFO(command.name << " " << *type);
                CHECK(schema->hasMessage(*type));
            }
        }
        if (!command.requestType) {
            continue;
        }
        for (const auto& example : command.requestExamples) {
            if (!example.valid) {
                continue;
            }
            ++examples;
            if (const auto result = schema->normalizeJson(*command.requestType, example.text); !result) {
                ++rejected;
                WARN(command.name << ": " << result.error());
            }
        }
    }
    INFO(rejected << " of " << examples << " documented request examples rejected");
    CHECK(examples > 50);
    CHECK(rejected * 10 < examples);
}

#endif
