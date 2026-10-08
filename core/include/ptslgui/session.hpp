#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <string>

namespace ptslgui {

enum class ResponseStatus { InProgress, Completed, Failed, Cancelled };

/// A response (intermediate or final) to a PTSL request.
struct Response {
    ResponseStatus status = ResponseStatus::Completed;
    int progress = 0;
    std::string taskId;
    std::string bodyJson;
    std::string errorJson;

    [[nodiscard]] bool isFinal() const { return status != ResponseStatus::InProgress; }
};

using RequestId = std::uint64_t;

/// Called once per response; may be invoked from any thread.
using ResponseSink = std::function<void(RequestId, const Response&)>;

struct ConnectionSettings {
    std::string address = "localhost:31416";
    std::string companyName = "PTSL GUI";
    std::string applicationName = "PTSL GUI";
    bool launchHost = false;
};

enum class ConnectionState { Disconnected, Connected };

/// A connection to a PTSL host. Implemented over the Avid SDK and by FakePtslSession for tests and demo mode.
class IPtslSession {
public:
    IPtslSession() = default;
    IPtslSession(const IPtslSession&) = delete;
    IPtslSession& operator=(const IPtslSession&) = delete;
    IPtslSession(IPtslSession&&) = delete;
    IPtslSession& operator=(IPtslSession&&) = delete;
    virtual ~IPtslSession() = default;

    virtual std::expected<void, std::string> connect(const ConnectionSettings& settings) = 0;
    virtual void disconnect() = 0;
    [[nodiscard]] virtual ConnectionState state() const = 0;

    /// Sends a command with a JSON request body. Every request eventually receives exactly one final response.
    virtual RequestId send(int commandId, std::string requestJson, ResponseSink sink) = 0;

    /// Cancels in-flight requests; each receives a Cancelled response if it has not completed.
    virtual void cancelAll() = 0;
};

} // namespace ptslgui
