#pragma once

#include <ptslgui/catalog.hpp>
#include <ptslgui/history.hpp>
#include <ptslgui/schema.hpp>
#include <ptslgui/session.hpp>

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace ptslgui {

/// Runs a task on the thread that owns the controller (for Qt: a queued invoke onto the GUI thread).
using Dispatcher = std::function<void(std::function<void()>)>;
using Clock = std::function<std::chrono::system_clock::time_point()>;
using EntryCallback = std::function<void(const HistoryEntry&)>;

/// Validates requests against the schema, sends them through a session and records them in the history.
/// All public methods and callbacks run on the owning thread; session callbacks are re-posted via the dispatcher.
class RequestController {
public:
    RequestController(const CommandCatalog& catalog, const ProtoSchema& schema, IPtslSession& session, History& history,
                      Dispatcher dispatcher = {}, Clock clock = {});
    RequestController(const RequestController&) = delete;
    RequestController& operator=(const RequestController&) = delete;
    RequestController(RequestController&&) = delete;
    RequestController& operator=(RequestController&&) = delete;
    ~RequestController();

    /// Returns the normalised request JSON for a command, or a validation error.
    [[nodiscard]] std::expected<std::string, std::string> prepare(int commandId, std::string_view requestJson) const;

    /// Validates and sends; returns the history sequence number. onUpdate is called for each response.
    std::expected<std::uint64_t, std::string> send(int commandId, std::string_view requestJson,
                                                   EntryCallback onUpdate = {});

private:
    struct Alive;

    const CommandCatalog& catalog_;
    const ProtoSchema& schema_;
    IPtslSession& session_;
    History& history_;
    Dispatcher dispatcher_;
    Clock clock_;
    std::shared_ptr<Alive> alive_;
};

} // namespace ptslgui
