#include <ptslgui/controller.hpp>

#include <format>
#include <utility>

namespace ptslgui {
namespace {

bool isBlankOrEmptyObject(std::string_view json) {
    std::string compact;
    for (const char c : json) {
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            compact += c;
        }
    }
    return compact.empty() || compact == "{}";
}

} // namespace

struct RequestController::Alive {};

RequestController::RequestController(const CommandCatalog& catalog, const ProtoSchema& schema, IPtslSession& session,
                                     History& history, Dispatcher dispatcher, Clock clock)
    : catalog_(catalog)
    , schema_(schema)
    , session_(session)
    , history_(history)
    , dispatcher_(dispatcher ? std::move(dispatcher) : Dispatcher([](const std::function<void()>& task) { task(); }))
    , clock_(clock ? std::move(clock) : Clock([] { return std::chrono::system_clock::now(); }))
    , alive_(std::make_shared<Alive>()) {}

RequestController::~RequestController() = default;

std::expected<std::string, std::string> RequestController::prepare(int commandId, std::string_view requestJson) const {
    const CommandInfo* command = catalog_.findById(commandId);
    if (command == nullptr) {
        return std::unexpected(std::format("unknown command id {}", commandId));
    }
    if (!command->requestType) {
        if (!isBlankOrEmptyObject(requestJson)) {
            return std::unexpected(std::format("{} does not take a request body", command->displayName));
        }
        return std::string{};
    }
    return schema_.normalizeJson(*command->requestType, requestJson);
}

std::expected<std::uint64_t, std::string> RequestController::send(int commandId, std::string_view requestJson,
                                                                  EntryCallback onUpdate) {
    if (session_.state() != ConnectionState::Connected) {
        return std::unexpected("not connected");
    }
    auto prepared = prepare(commandId, requestJson);
    if (!prepared) {
        return std::unexpected(prepared.error());
    }
    const CommandInfo* command = catalog_.findById(commandId);
    const std::uint64_t sequence = history_.add(commandId, command->name, *prepared, clock_());

    const std::weak_ptr<Alive> alive = alive_;
    // NOLINTBEGIN(bugprone-exception-escape)
    session_.send(commandId, std::move(*prepared),
                  [this, alive, sequence, dispatcher = dispatcher_,
                   onUpdate = std::move(onUpdate)](RequestId, const Response& response) {
                      dispatcher([this, alive, sequence, onUpdate, response] {
                          if (alive.expired()) {
                              return;
                          }
                          const HistoryEntry* entry = history_.apply(sequence, response, clock_());
                          if (entry != nullptr && onUpdate) {
                              onUpdate(*entry);
                          }
                      });
                  });
    // NOLINTEND(bugprone-exception-escape)
    return sequence;
}

} // namespace ptslgui
