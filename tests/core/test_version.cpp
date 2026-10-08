#include <ptslgui/version.hpp>

#include <catch2/catch_test_macros.hpp>

using ptslgui::Version;

TEST_CASE("Version parses two and three part versions") {
    CHECK(Version::parse("2025.10") == Version{.year = 2025, .minor = 10, .revision = 0});
    CHECK(Version::parse("2024.6.1") == Version{.year = 2024, .minor = 6, .revision = 1});
}

TEST_CASE("Version rejects malformed text") {
    for (const char* text : {"", "2025", "2025.", ".10", "2025.10.", "2025.10.1.2", "a.b", "2025.x", " 2025.10",
                             "2025.10 ", "-1.2", "2025.-1"}) {
        INFO(text);
        CHECK_FALSE(Version::parse(text).has_value());
    }
}

TEST_CASE("Version orders numerically") {
    CHECK(*Version::parse("2024.10") < *Version::parse("2025.6"));
    CHECK(*Version::parse("2025.6") < *Version::parse("2025.10"));
    CHECK(*Version::parse("2025.10") < *Version::parse("2025.10.1"));
    CHECK(*Version::parse("2025.10.0") == *Version::parse("2025.10"));
}

TEST_CASE("Version formats without a zero revision") {
    CHECK(Version{.year = 2025, .minor = 6, .revision = 0}.toString() == "2025.6");
    CHECK(Version{.year = 2025, .minor = 6, .revision = 2}.toString() == "2025.6.2");
}
