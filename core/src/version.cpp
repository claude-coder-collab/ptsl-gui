#include <ptslgui/version.hpp>

#include <array>
#include <charconv>
#include <format>

namespace ptslgui {

std::optional<Version> Version::parse(std::string_view text) {
    std::array<int, 3> parts{};
    std::size_t count = 0;
    const char* cursor = text.data();
    const char* const end = text.data() + text.size();
    while (count < parts.size()) {
        const auto [next, error] = std::from_chars(cursor, end, parts.at(count));
        if (error != std::errc{} || next == cursor || parts.at(count) < 0) {
            return std::nullopt;
        }
        ++count;
        cursor = next;
        if (cursor == end) {
            break;
        }
        if (*cursor != '.') {
            return std::nullopt;
        }
        ++cursor;
        if (cursor == end) {
            return std::nullopt;
        }
    }
    if (cursor != end || count < 2) {
        return std::nullopt;
    }
    return Version{.year = parts[0], .minor = parts[1], .revision = parts[2]};
}

std::string Version::toString() const {
    if (revision == 0) {
        return std::format("{}.{}", year, minor);
    }
    return std::format("{}.{}.{}", year, minor, revision);
}

} // namespace ptslgui
