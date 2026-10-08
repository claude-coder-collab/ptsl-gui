#include <ptslgui/protocol.hpp>

#include <nlohmann/json.hpp>

namespace ptslgui::protocol {
namespace {

using nlohmann::json;

constexpr int taskQueued = 0;
constexpr int taskPending = 1;
constexpr int taskInProgress = 2;
constexpr int taskCompleted = 3;
constexpr int taskWaitingForUserInput = 5;
constexpr int taskCompletedWithBadResponse = 6;

std::optional<json> parseObject(std::string_view text) {
    json document = json::parse(text, nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        return std::nullopt;
    }
    return document;
}

std::string stringOrNumber(const json& value) {
    if (value.is_string()) {
        return value.get<std::string>();
    }
    if (value.is_null()) {
        return {};
    }
    return value.dump();
}

CommandError errorFromObject(const json& object) {
    return CommandError{
        .type = stringOrNumber(object.value("command_error_type", json())),
        .message = object.value("command_error_message", std::string{}),
        .isWarning = object.value("is_warning", false),
    };
}

} // namespace

ResponseStatus statusFromTaskStatus(int taskStatus) {
    switch (taskStatus) {
    case taskQueued:
    case taskPending:
    case taskInProgress:
    case taskWaitingForUserInput:
        return ResponseStatus::InProgress;
    case taskCompleted:
    case taskCompletedWithBadResponse:
        return ResponseStatus::Completed;
    default:
        return ResponseStatus::Failed;
    }
}

std::optional<Version> versionFromResponse(std::string_view bodyJson) {
    const auto document = parseObject(bodyJson);
    if (!document || !document->contains("version") || !(*document)["version"].is_number_integer()) {
        return std::nullopt;
    }
    try {
        return Version{
            .year = document->at("version").get<int>(),
            .minor = document->value("version_minor", 0),
            .revision = document->value("version_revision", 0),
        };
    } catch (const json::exception&) {
        return std::nullopt;
    }
}

std::optional<bool> hostReadyFromResponse(std::string_view bodyJson) {
    const auto document = parseObject(bodyJson);
    if (!document) {
        return std::nullopt;
    }
    const auto it = document->find("is_host_ready");
    if (it == document->end()) {
        return false;
    }
    if (!it->is_boolean()) {
        return std::nullopt;
    }
    return it->get<bool>();
}

std::string registerConnectionBody(std::string_view companyName, std::string_view applicationName) {
    return json{{"company_name", companyName}, {"application_name", applicationName}}.dump();
}

std::vector<CommandError> errorsFromJson(std::string_view errorJson) {
    if (errorJson.find_first_not_of(" \t\r\n") == std::string_view::npos) {
        return {};
    }
    const json document = json::parse(errorJson, nullptr, false);
    std::vector<CommandError> errors;
    try {
        if (document.is_object() && document.contains("errors") && document["errors"].is_array()) {
            for (const auto& item : document["errors"]) {
                if (item.is_object()) {
                    errors.push_back(errorFromObject(item));
                }
            }
            return errors;
        }
        if (document.is_object() && document.contains("command_error_message")) {
            return {errorFromObject(document)};
        }
        if (document.is_object() && document.contains("message")) {
            return {CommandError{.type = {}, .message = stringOrNumber(document["message"]), .isWarning = false}};
        }
    } catch (const json::exception&) {
        errors.clear();
    }
    return {CommandError{.type = {}, .message = std::string(errorJson), .isWarning = false}};
}

} // namespace ptslgui::protocol
