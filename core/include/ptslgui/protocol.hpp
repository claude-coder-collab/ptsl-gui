#pragma once

#include <ptslgui/session.hpp>
#include <ptslgui/version.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// Knowledge of the PTSL wire protocol that is independent of the SDK.
namespace ptslgui::protocol {

inline constexpr std::string_view hostReadyCheck = "CId_HostReadyCheck";
inline constexpr std::string_view registerConnection = "CId_RegisterConnection";
inline constexpr std::string_view getPtslVersion = "CId_GetPTSLVersion";

/// Maps a PTSL TaskStatus value (TStatus_*) to a response status.
[[nodiscard]] ResponseStatus statusFromTaskStatus(int taskStatus);

/// Reads version / version_minor / version_revision from a GetPTSLVersion response body.
[[nodiscard]] std::optional<Version> versionFromResponse(std::string_view bodyJson);

/// Reads is_host_ready from a HostReadyCheck response body.
[[nodiscard]] std::optional<bool> hostReadyFromResponse(std::string_view bodyJson);

[[nodiscard]] std::string registerConnectionBody(std::string_view companyName, std::string_view applicationName);

struct CommandError {
    std::string type;
    std::string message;
    bool isWarning = false;
};

/// Parses a ResponseError JSON ({"errors": [...]}) or a single error object; unparseable text becomes one error.
[[nodiscard]] std::vector<CommandError> errorsFromJson(std::string_view errorJson);

} // namespace ptslgui::protocol
