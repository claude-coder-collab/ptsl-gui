#include <ptslgui/catalog.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <ranges>

namespace ptslgui {
namespace {

using nlohmann::json;

std::string lower(std::string_view text) {
    std::string result(text);
    std::ranges::transform(result, result.begin(),
                           [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    return result;
}

std::optional<std::string> optionalString(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) {
        return std::nullopt;
    }
    return it->get<std::string>();
}

std::vector<std::string> stringList(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) {
        return {};
    }
    return it->get<std::vector<std::string>>();
}

std::vector<Example> examples(const json& object, const char* key) {
    std::vector<Example> result;
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) {
        return result;
    }
    for (const auto& item : *it) {
        result.push_back(Example{
            .text = item.at("text").get<std::string>(),
            .caption = item.value("caption", std::string{}),
            .comments = stringList(item, "comments"),
            .valid = item.value("valid", true),
            .repaired = item.value("repaired", false),
        });
    }
    return result;
}

CommandInfo commandFromJson(const json& item) {
    CommandInfo command{
        .id = item.at("id").get<int>(),
        .name = item.at("name").get<std::string>(),
        .displayName = item.value("display_name", std::string{}),
        .description = item.value("description", std::string{}),
        .notes = stringList(item, "notes"),
        .categories = stringList(item, "categories"),
        .requestType = optionalString(item, "request_type"),
        .responseType = optionalString(item, "response_type"),
        .requestExamples = examples(item, "request_examples"),
        .responseExamples = examples(item, "response_examples"),
        .since = std::nullopt,
        .deprecated = optionalString(item, "deprecated"),
        .deprecatedAliases = stringList(item, "deprecated_aliases"),
    };
    if (const auto since = optionalString(item, "since")) {
        command.since = Version::parse(*since);
    }
    if (command.displayName.empty()) {
        command.displayName = command.name;
    }
    return command;
}

bool matchesAllTerms(const CommandInfo& command, const std::vector<std::string>& terms) {
    if (terms.empty()) {
        return true;
    }
    const std::string haystack = lower(command.name + '\n' + command.displayName + '\n' + command.description);
    return std::ranges::all_of(terms, [&](const std::string& term) { return haystack.contains(term); });
}

} // namespace

bool CommandInfo::hasCategory(std::string_view category) const {
    return std::ranges::find(categories, category) != categories.end();
}

bool CommandInfo::isMutating() const {
    static constexpr std::array<std::string_view, 4> mutatingCategories{"session_write", "session_file", "editing",
                                                                        "export"};
    return std::ranges::any_of(mutatingCategories, [this](std::string_view category) { return hasCategory(category); });
}

bool CommandInfo::isUnsupportedBy(const Version& hostVersion) const {
    return since.has_value() && hostVersion < *since;
}

std::expected<CommandCatalog, std::string> CommandCatalog::fromJson(std::string_view text) {
    try {
        const json document = json::parse(text);
        const int schemaVersion = document.at("schema_version").get<int>();
        if (schemaVersion != supportedSchemaVersion) {
            return std::unexpected(std::format("unsupported catalog schema version {}", schemaVersion));
        }
        CommandCatalog catalog;
        catalog.categories_ = stringList(document, "categories");
        for (const auto& item : document.at("commands")) {
            catalog.commands_.push_back(commandFromJson(item));
        }
        return catalog;
    } catch (const json::exception& error) {
        return std::unexpected(std::format("invalid command catalog: {}", error.what()));
    }
}

const CommandInfo* CommandCatalog::findById(int id) const {
    const auto it = std::ranges::find(commands_, id, &CommandInfo::id);
    return it == commands_.end() ? nullptr : &*it;
}

const CommandInfo* CommandCatalog::findByName(std::string_view name) const {
    const std::string prefixed = name.starts_with("CId_") ? std::string(name) : std::format("CId_{}", name);
    for (const auto& command : commands_) {
        if (command.name == prefixed ||
            std::ranges::find(command.deprecatedAliases, name) != command.deprecatedAliases.end()) {
            return &command;
        }
    }
    return nullptr;
}

std::vector<const CommandInfo*> CommandCatalog::search(const CommandFilter& filter) const {
    std::vector<std::string> terms;
    for (const auto word : lower(filter.text) | std::views::split(' ')) {
        if (!word.empty()) {
            terms.emplace_back(word.begin(), word.end());
        }
    }
    std::vector<const CommandInfo*> result;
    for (const auto& command : commands_) {
        if (!filter.includeDeprecated && command.deprecated) {
            continue;
        }
        if (!filter.category.empty() && !command.hasCategory(filter.category)) {
            continue;
        }
        if (matchesAllTerms(command, terms)) {
            result.push_back(&command);
        }
    }
    return result;
}

} // namespace ptslgui
