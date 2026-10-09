#pragma once

#include <optional>
#include <string_view>

namespace ptslgui {

/// Text format of a time location for one PTSL time unit.
struct TimeFormat {
    std::string_view unit;
    std::string_view placeholder;
    std::string_view description;
    /// ECMAScript regular expression the whole (trimmed) text must match.
    std::string_view pattern;
};

/// Format for a unit enum value such as TLType_MinSecs or TOOptions_TimeCode (the prefix up to the first '_' is
/// ignored); nullopt for unknown units.
[[nodiscard]] std::optional<TimeFormat> timeFormatFor(std::string_view enumValueName);

/// Whether a field of this enum type (fully qualified or not) selects the unit of time fields in the same message.
[[nodiscard]] bool isTimeUnitEnum(std::string_view enumTypeName);

/// Whether a string field with this name holds a time location: location, location_value or *_time.
[[nodiscard]] bool isTimeLocationField(std::string_view fieldName);

/// Whether text is empty or matches the format, ignoring surrounding whitespace.
[[nodiscard]] bool matchesTimeFormat(const TimeFormat& format, std::string_view text);

} // namespace ptslgui
