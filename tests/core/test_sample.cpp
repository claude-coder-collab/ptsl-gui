#include "test_support.hpp"

#include <ptslgui/sample.hpp>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <set>

using namespace ptslgui;
using nlohmann::json;

TEST_CASE("Samples are valid for every fixture message") {
    const auto& schema = test::fixtureSchema();
    for (const auto& name : schema.messageNames()) {
        for (std::uint32_t seed = 0; seed < 50; ++seed) {
            const auto sample = sampleJson(schema, name, {.seed = seed, .maxDepth = 3, .maxElements = 3});
            INFO(name << " seed " << seed << ": " << sample);
            CHECK(schema.normalizeJson(name, sample).has_value());
        }
    }
}

TEST_CASE("Samples are deterministic and varied") {
    const auto& schema = test::fixtureSchema();
    const SampleOptions options{.seed = 7, .maxDepth = 3, .maxElements = 3};
    CHECK(sampleJson(schema, "MakeWidgetRequestBody", options) == sampleJson(schema, "MakeWidgetRequestBody", options));

    std::set<std::string> distinct;
    std::set<std::string> fieldsSeen;
    for (std::uint32_t seed = 0; seed < 50; ++seed) {
        const auto sample =
            sampleJson(schema, "MakeWidgetRequestBody", {.seed = seed, .maxDepth = 3, .maxElements = 3});
        distinct.insert(sample);
        const json parsed = json::parse(sample);
        for (const auto& [key, value] : parsed.items()) {
            fieldsSeen.insert(key);
        }
    }
    CHECK(distinct.size() > 40);
    CHECK(fieldsSeen.size() == 13);
}

TEST_CASE("Samples respect the depth limit and unknown types") {
    const auto& schema = test::fixtureSchema();
    for (std::uint32_t seed = 0; seed < 20; ++seed) {
        const auto sample =
            json::parse(sampleJson(schema, "MakeWidgetRequestBody", {.seed = seed, .maxDepth = 0, .maxElements = 3}));
        CHECK_FALSE(sample.contains("geometry"));
        CHECK_FALSE(sample.contains("parts"));
    }
    CHECK(sampleJson(schema, "NoSuchMessage") == "{}");
}

#ifdef PTSLGUI_SDK_PROTO
TEST_CASE("Samples are valid for every SDK message", "[sdk]") {
    const auto schema = ProtoSchema::fromProtoText(test::readFile(PTSLGUI_SDK_PROTO));
    REQUIRE(schema.has_value());
    std::size_t failures = 0;
    for (const auto& name : schema->messageNames()) {
        for (std::uint32_t seed = 0; seed < 5; ++seed) {
            const auto sample = sampleJson(*schema, name, {.seed = seed, .maxDepth = 3, .maxElements = 2});
            if (const auto result = schema->normalizeJson(name, sample); !result) {
                ++failures;
                WARN(name << ": " << result.error() << "\n" << sample);
            }
        }
    }
    CHECK(failures == 0);
}
#endif
