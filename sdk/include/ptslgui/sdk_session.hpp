#pragma once

#include <ptslgui/session.hpp>

#include <chrono>
#include <memory>

namespace ptslgui {

struct SdkSessionOptions {
    /// How long to wait for Pro Tools to report ready when it is expected to be running.
    std::chrono::milliseconds readyTimeout{3'000};
    /// How long to wait for Pro Tools to report ready after asking it to launch.
    std::chrono::milliseconds launchReadyTimeout{120'000};
    std::chrono::milliseconds pollInterval{500};
};

/// IPtslSession implemented with the Avid PTSL C++ SDK (CppPTSLClient). Thread-safe.
/// connect() blocks while Pro Tools is contacted; call it off the GUI thread.
class SdkPtslSession final : public IPtslSession {
public:
    explicit SdkPtslSession(SdkSessionOptions options = {});
    ~SdkPtslSession() override;
    SdkPtslSession(const SdkPtslSession&) = delete;
    SdkPtslSession& operator=(const SdkPtslSession&) = delete;
    SdkPtslSession(SdkPtslSession&&) = delete;
    SdkPtslSession& operator=(SdkPtslSession&&) = delete;

    std::expected<void, std::string> connect(const ConnectionSettings& settings) override;
    void disconnect() override;
    [[nodiscard]] ConnectionState state() const override;
    RequestId send(int commandId, std::string requestJson, ResponseSink sink) override;
    void cancelAll() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ptslgui
