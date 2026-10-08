#include <ptslgui/history.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <utility>

namespace ptslgui {
namespace {

using nlohmann::json;

constexpr int historyFormatVersion = 1;

constexpr std::array<std::pair<Outcome, std::string_view>, 5> outcomeNames{{
    {Outcome::Pending, "pending"},
    {Outcome::InProgress, "in_progress"},
    {Outcome::Completed, "completed"},
    {Outcome::Failed, "failed"},
    {Outcome::Cancelled, "cancelled"},
}};

Outcome outcomeOf(ResponseStatus status) {
    switch (status) {
    case ResponseStatus::InProgress:
        return Outcome::InProgress;
    case ResponseStatus::Completed:
        return Outcome::Completed;
    case ResponseStatus::Failed:
        return Outcome::Failed;
    case ResponseStatus::Cancelled:
        return Outcome::Cancelled;
    }
    return Outcome::Failed;
}

std::int64_t toMilliseconds(std::chrono::system_clock::time_point time) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count();
}

} // namespace

std::string_view toString(Outcome outcome) {
    const auto* it = std::ranges::find(outcomeNames, outcome, &std::pair<Outcome, std::string_view>::first);
    return it == outcomeNames.end() ? "failed" : it->second;
}

std::optional<Outcome> outcomeFromString(std::string_view text) {
    const auto* it = std::ranges::find(outcomeNames, text, &std::pair<Outcome, std::string_view>::second);
    if (it == outcomeNames.end()) {
        return std::nullopt;
    }
    return it->first;
}

History::History(std::size_t maxEntries) : maxEntries_(std::max<std::size_t>(maxEntries, 1)) {}

std::uint64_t History::add(int commandId, std::string commandName, std::string requestJson,
                           std::chrono::system_clock::time_point sentAt) {
    const std::uint64_t sequence = nextSequence_++;
    entries_.push_back(HistoryEntry{
        .sequence = sequence,
        .commandId = commandId,
        .commandName = std::move(commandName),
        .requestJson = std::move(requestJson),
        .sentAt = sentAt,
        .duration = std::nullopt,
        .outcome = Outcome::Pending,
        .progress = 0,
        .taskId = {},
        .responseJson = {},
        .errorJson = {},
    });
    while (entries_.size() > maxEntries_) {
        entries_.pop_front();
    }
    return sequence;
}

const HistoryEntry* History::apply(std::uint64_t sequence, const Response& response,
                                   std::chrono::system_clock::time_point receivedAt) {
    HistoryEntry* entry = findMutable(sequence);
    if (entry == nullptr) {
        return nullptr;
    }
    entry->outcome = outcomeOf(response.status);
    entry->progress = response.progress;
    if (!response.taskId.empty()) {
        entry->taskId = response.taskId;
    }
    if (!response.bodyJson.empty()) {
        entry->responseJson = response.bodyJson;
    }
    if (!response.errorJson.empty()) {
        entry->errorJson = response.errorJson;
    }
    if (response.isFinal()) {
        entry->duration = std::chrono::duration_cast<std::chrono::milliseconds>(receivedAt - entry->sentAt);
    }
    return entry;
}

const HistoryEntry* History::find(std::uint64_t sequence) const {
    const auto it = std::ranges::find(entries_, sequence, &HistoryEntry::sequence);
    return it == entries_.end() ? nullptr : &*it;
}

HistoryEntry* History::findMutable(std::uint64_t sequence) {
    const auto it = std::ranges::find(entries_, sequence, &HistoryEntry::sequence);
    return it == entries_.end() ? nullptr : &*it;
}

void History::clear() {
    entries_.clear();
}

std::string History::toJson() const {
    json items = json::array();
    for (const auto& entry : entries_) {
        items.push_back({
            {"sequence", entry.sequence},
            {"command_id", entry.commandId},
            {"command_name", entry.commandName},
            {"request", entry.requestJson},
            {"sent_at_ms", toMilliseconds(entry.sentAt)},
            {"duration_ms", entry.duration ? json(entry.duration->count()) : json(nullptr)},
            {"outcome", toString(entry.outcome)},
            {"progress", entry.progress},
            {"task_id", entry.taskId},
            {"response", entry.responseJson},
            {"error", entry.errorJson},
        });
    }
    return json{{"version", historyFormatVersion}, {"entries", std::move(items)}}.dump(1);
}

std::expected<void, std::string> History::loadJson(std::string_view text) {
    try {
        const json document = json::parse(text);
        if (document.at("version").get<int>() != historyFormatVersion) {
            return std::unexpected("unsupported history format version");
        }
        std::deque<HistoryEntry> loaded;
        std::uint64_t highest = 0;
        for (const auto& item : document.at("entries")) {
            const auto outcome = outcomeFromString(item.at("outcome").get<std::string>());
            if (!outcome) {
                return std::unexpected(std::format("unknown outcome {}", item.at("outcome").dump()));
            }
            HistoryEntry entry{
                .sequence = item.at("sequence").get<std::uint64_t>(),
                .commandId = item.at("command_id").get<int>(),
                .commandName = item.at("command_name").get<std::string>(),
                .requestJson = item.at("request").get<std::string>(),
                .sentAt = std::chrono::system_clock::time_point(
                    std::chrono::milliseconds(item.at("sent_at_ms").get<std::int64_t>())),
                .duration = std::nullopt,
                .outcome = *outcome,
                .progress = item.value("progress", 0),
                .taskId = item.value("task_id", std::string{}),
                .responseJson = item.value("response", std::string{}),
                .errorJson = item.value("error", std::string{}),
            };
            if (const auto it = item.find("duration_ms"); it != item.end() && !it->is_null()) {
                entry.duration = std::chrono::milliseconds(it->get<std::int64_t>());
            }
            highest = std::max(highest, entry.sequence);
            loaded.push_back(std::move(entry));
        }
        while (loaded.size() > maxEntries_) {
            loaded.pop_front();
        }
        entries_ = std::move(loaded);
        nextSequence_ = highest + 1;
        return {};
    } catch (const json::exception& error) {
        return std::unexpected(std::format("invalid history: {}", error.what()));
    }
}

} // namespace ptslgui
