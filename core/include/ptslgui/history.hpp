#pragma once

#include <ptslgui/session.hpp>

#include <chrono>
#include <cstdint>
#include <deque>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace ptslgui {

enum class Outcome { Pending, InProgress, Completed, Failed, Cancelled };

struct HistoryEntry {
    std::uint64_t sequence = 0;
    int commandId = 0;
    std::string commandName;
    std::string requestJson;
    std::chrono::system_clock::time_point sentAt;
    std::optional<std::chrono::milliseconds> duration;
    Outcome outcome = Outcome::Pending;
    int progress = 0;
    std::string taskId;
    std::string responseJson;
    std::string errorJson;
};

/// Log of requests and their responses, oldest first, bounded in size. Not thread-safe.
class History {
public:
    explicit History(std::size_t maxEntries = 1000);

    std::uint64_t add(int commandId, std::string commandName, std::string requestJson,
                      std::chrono::system_clock::time_point sentAt);

    /// Applies a response; returns the updated entry, or nullptr if the entry has been evicted.
    const HistoryEntry* apply(std::uint64_t sequence, const Response& response,
                              std::chrono::system_clock::time_point receivedAt);

    [[nodiscard]] const HistoryEntry* find(std::uint64_t sequence) const;

    [[nodiscard]] const std::deque<HistoryEntry>& entries() const { return entries_; }

    void clear();

    [[nodiscard]] std::string toJson() const;

    /// Replaces the contents with entries from toJson() output; sequences continue after the highest loaded one.
    [[nodiscard]] std::expected<void, std::string> loadJson(std::string_view text);

private:
    HistoryEntry* findMutable(std::uint64_t sequence);

    std::size_t maxEntries_;
    std::uint64_t nextSequence_ = 1;
    std::deque<HistoryEntry> entries_;
};

[[nodiscard]] std::string_view toString(Outcome outcome);
[[nodiscard]] std::optional<Outcome> outcomeFromString(std::string_view text);

} // namespace ptslgui
