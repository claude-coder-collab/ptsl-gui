#include <ptslgui/time_format.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace ptslgui;

TEST_CASE("Time formats are found by unit enum value") {
    REQUIRE(timeFormatFor("TLType_MinSecs").has_value());
    CHECK(timeFormatFor("TLType_MinSecs")->placeholder == "0:00.000");
    CHECK(timeFormatFor("TOOptions_TimeCode")->unit == "TimeCode");
    CHECK(timeFormatFor("BarsBeats")->unit == "BarsBeats");
    CHECK_FALSE(timeFormatFor("TLType_Unknown").has_value());
    CHECK_FALSE(timeFormatFor("").has_value());
}

TEST_CASE("Time formats validate text") {
    const auto check = [](std::string_view unit, std::string_view text) {
        return matchesTimeFormat(*timeFormatFor(unit), text);
    };
    CHECK(check("TLType_Samples", "48000"));
    CHECK_FALSE(check("TLType_Samples", "1:00"));
    CHECK(check("TLType_MinSecs", "0:00.000"));
    CHECK(check("TLType_MinSecs", "12:30.5"));
    CHECK(check("TLType_MinSecs", " 1:05 "));
    CHECK_FALSE(check("TLType_MinSecs", "1:5"));
    CHECK(check("TLType_TimeCode", "01:00:00:00"));
    CHECK(check("TLType_TimeCode", "01:00:00;29"));
    CHECK_FALSE(check("TLType_TimeCode", "01:00:00"));
    CHECK(check("TLType_BarsBeats", "1|1|000"));
    CHECK(check("TLType_BarsBeats", "12|3"));
    CHECK_FALSE(check("TLType_BarsBeats", "12"));
    CHECK(check("TLType_FeetFrames", "10+04"));
    CHECK(check("TLType_Seconds", "1.25"));
    CHECK(check("TLType_Seconds", ""));
}

TEST_CASE("Time unit enums and time location fields are recognised") {
    CHECK(isTimeUnitEnum("ptsl.TimelineLocationType"));
    CHECK(isTimeUnitEnum("TrackOffsetOptions"));
    CHECK_FALSE(isTimeUnitEnum("ptsl.TimeProperties"));
    CHECK(isTimeLocationField("in_time"));
    CHECK(isTimeLocationField("location"));
    CHECK(isTimeLocationField("location_value"));
    CHECK_FALSE(isTimeLocationField("session_location"));
    CHECK_FALSE(isTimeLocationField("timeline"));
}
