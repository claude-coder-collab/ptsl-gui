#pragma once

#include <ptslgui/version.hpp>

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ptslgui {

/// An example request or response body taken from the protocol documentation.
struct Example {
    std::string text;
    std::string caption;
    std::vector<std::string> comments;
    bool valid = true;
    bool repaired = false;
};

/// One PTSL command as described by the generated command catalog.
struct CommandInfo {
    int id = 0;
    std::string name;
    std::string displayName;
    std::string description;
    std::vector<std::string> notes;
    std::vector<std::string> categories;
    std::optional<std::string> requestType;
    std::optional<std::string> responseType;
    std::vector<Example> requestExamples;
    std::vector<Example> responseExamples;
    std::optional<Version> since;
    std::optional<std::string> deprecated;
    std::vector<std::string> deprecatedAliases;

    [[nodiscard]] bool hasCategory(std::string_view category) const;

    /// True for commands that change the session or write files (session_write, session_file, editing, export).
    [[nodiscard]] bool isMutating() const;

    /// True when the host version is known to be older than the version the command was introduced in.
    [[nodiscard]] bool isUnsupportedBy(const Version& hostVersion) const;
};

/// Filter for CommandCatalog::search.
struct CommandFilter {
    std::string text;
    std::string category;
    bool includeDeprecated = true;
};

/// The list of PTSL commands, loaded from the JSON produced by tools/gen_catalog.py.
class CommandCatalog {
public:
    static constexpr int supportedSchemaVersion = 1;

    [[nodiscard]] static std::expected<CommandCatalog, std::string> fromJson(std::string_view text);

    [[nodiscard]] const std::vector<CommandInfo>& commands() const { return commands_; }

    [[nodiscard]] const std::vector<std::string>& categories() const { return categories_; }

    [[nodiscard]] const CommandInfo* findById(int id) const;

    /// Accepts the enum name with or without the "CId_" prefix, or a deprecated alias.
    [[nodiscard]] const CommandInfo* findByName(std::string_view name) const;

    /// Case-insensitive match of every whitespace-separated term against name, display name and description.
    [[nodiscard]] std::vector<const CommandInfo*> search(const CommandFilter& filter) const;

private:
    std::vector<CommandInfo> commands_;
    std::vector<std::string> categories_;
};

} // namespace ptslgui
