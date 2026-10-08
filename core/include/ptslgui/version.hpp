#pragma once

#include <compare>
#include <optional>
#include <string>
#include <string_view>

namespace ptslgui {

/// A PTSL / Pro Tools version such as 2025.10 or 2024.6.1.
struct Version {
    int year = 0;
    int minor = 0;
    int revision = 0;

    /// Parses "YYYY.M" or "YYYY.M.R"; surrounding text is not accepted.
    [[nodiscard]] static std::optional<Version> parse(std::string_view text);

    [[nodiscard]] std::string toString() const;

    friend auto operator<=>(const Version&, const Version&) = default;
};

} // namespace ptslgui
