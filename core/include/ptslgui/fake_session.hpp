#pragma once

#include <ptslgui/session.hpp>

#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <vector>

namespace ptslgui {

struct SentRequest {
    RequestId id = 0;
    int commandId = 0;
    std::string requestJson;
};

/// Scriptable in-memory PTSL session for tests and demo mode. Thread-safe.
class FakePtslSession final : public IPtslSession {
public:
    /// Responses sent for every request of commandId, in order. Without a script a request completes with "{}".
    void script(int commandId, std::vector<Response> responses);

    /// When set, connect() fails with this message.
    void setConnectError(std::optional<std::string> error);

    /// When true (default false), responses are delivered synchronously from send().
    void setAutoDeliver(bool autoDeliver);

    /// Delivers all queued responses; returns how many were delivered.
    std::size_t deliverPending();

    [[nodiscard]] std::vector<SentRequest> sent() const;
    [[nodiscard]] ConnectionSettings lastSettings() const;

    std::expected<void, std::string> connect(const ConnectionSettings& settings) override;
    void disconnect() override;
    [[nodiscard]] ConnectionState state() const override;
    RequestId send(int commandId, std::string requestJson, ResponseSink sink) override;
    void cancelAll() override;

private:
    struct Pending {
        RequestId id;
        ResponseSink sink;
        std::deque<Response> responses;
    };

    mutable std::mutex mutex_;
    std::map<int, std::vector<Response>> scripts_;
    std::optional<std::string> connectError_;
    bool autoDeliver_ = false;
    ConnectionState state_ = ConnectionState::Disconnected;
    ConnectionSettings settings_;
    RequestId nextId_ = 1;
    std::vector<SentRequest> sent_;
    std::deque<Pending> pending_;
};

} // namespace ptslgui
