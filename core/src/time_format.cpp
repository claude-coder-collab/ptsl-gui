#include <ptslgui/time_format.hpp>

#include <algorithm>
#include <array>
#include <regex>
#include <string>

namespace ptslgui {
namespace {

constexpr std::array formats{
    TimeFormat{
        .unit = "Samples", .placeholder = "48000", .description = "whole number of samples", .pattern = R"(-?\d+)"},
    TimeFormat{.unit = "Ticks",
               .placeholder = "960000",
               .description = "whole number of ticks (960,000 per beat)",
               .pattern = R"(-?\d+)"},
    TimeFormat{
        .unit = "Frames", .placeholder = "0", .description = "whole number of video frames", .pattern = R"(-?\d+)"},
    TimeFormat{.unit = "MinSecs",
               .placeholder = "0:00.000",
               .description = "minutes and seconds, M:SS.mmm",
               .pattern = R"(-?\d+:\d{2}(\.\d{1,3})?)"},
    TimeFormat{.unit = "TimeCode",
               .placeholder = "00:00:00:00",
               .description = "timecode, HH:MM:SS:FF (; before the frames for drop-frame)",
               .pattern = R"(-?\d{1,2}:\d{2}:\d{2}[:;]\d{2}(\.\d{1,2})?)"},
    TimeFormat{.unit = "BarsBeats",
               .placeholder = "1|1|000",
               .description = "bars and beats, BARS|BEATS[|TICKS]",
               .pattern = R"(-?\d+\|\d+(\|\d+)?)"},
    TimeFormat{.unit = "FeetFrames",
               .placeholder = "0+00",
               .description = "feet and frames, FEET+FRAMES",
               .pattern = R"(-?\d+\+\d{1,2}(\.\d{1,2})?)"},
    TimeFormat{.unit = "Seconds", .placeholder = "0.0", .description = "seconds", .pattern = R"(-?(\d+\.?\d*|\.\d+))"},
};

std::string_view trimmed(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

} // namespace

std::optional<TimeFormat> timeFormatFor(std::string_view enumValueName) {
    const auto underscore = enumValueName.find('_');
    const std::string_view unit =
        underscore == std::string_view::npos ? enumValueName : enumValueName.substr(underscore + 1);
    const auto* const it = std::ranges::find(formats, unit, &TimeFormat::unit);
    if (it == formats.end()) {
        return std::nullopt;
    }
    return *it;
}

bool isTimeUnitEnum(std::string_view enumTypeName) {
    const auto dot = enumTypeName.rfind('.');
    const std::string_view name = dot == std::string_view::npos ? enumTypeName : enumTypeName.substr(dot + 1);
    return name == "TimelineLocationType" || name == "TrackOffsetOptions";
}

bool isTimeLocationField(std::string_view fieldName) {
    return fieldName == "location" || fieldName == "location_value" || fieldName.ends_with("_time");
}

bool matchesTimeFormat(const TimeFormat& format, std::string_view text) {
    const std::string_view value = trimmed(text);
    if (value.empty()) {
        return true;
    }
    const std::regex pattern{std::string(format.pattern)};
    return std::regex_match(value.begin(), value.end(), pattern);
}

} // namespace ptslgui
