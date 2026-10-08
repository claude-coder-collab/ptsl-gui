#include <ptslgui/history.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace ptslgui;
using namespace std::chrono_literals;

namespace {

constexpr std::chrono::system_clock::time_point t0{std::chrono::milliseconds(1'700'000'000'000)};

Response response(ResponseStatus status, int progress = 0, std::string body = {}, std::string error = {}) {
    return Response{.status = status,
                    .progress = progress,
                    .taskId = "task-1",
                    .bodyJson = std::move(body),
                    .errorJson = std::move(error)};
}

} // namespace

TEST_CASE("History records requests and applies responses") {
    History history;
    const auto sequence = history.add(3, "CId_GetTrackList", R"({"a":1})", t0);
    CHECK(sequence == 1);
    REQUIRE(history.find(sequence) != nullptr);
    CHECK(history.find(sequence)->outcome == Outcome::Pending);

    const HistoryEntry* entry = history.apply(sequence, response(ResponseStatus::InProgress, 40), t0 + 100ms);
    REQUIRE(entry != nullptr);
    CHECK(entry->outcome == Outcome::InProgress);
    CHECK(entry->progress == 40);
    CHECK(entry->taskId == "task-1");
    CHECK_FALSE(entry->duration.has_value());

    entry = history.apply(sequence, response(ResponseStatus::Completed, 100, R"({"ok":true})"), t0 + 250ms);
    REQUIRE(entry != nullptr);
    CHECK(entry->outcome == Outcome::Completed);
    CHECK(entry->responseJson == R"({"ok":true})");
    CHECK(entry->duration == 250ms);
}

TEST_CASE("History keeps earlier body when a later response has none") {
    History history;
    const auto sequence = history.add(1, "CId_X", "", t0);
    history.apply(sequence, response(ResponseStatus::InProgress, 10, R"({"partial":1})"), t0);
    const HistoryEntry* entry = history.apply(sequence, response(ResponseStatus::Failed, 0, {}, R"({"e":1})"), t0);
    REQUIRE(entry != nullptr);
    CHECK(entry->responseJson == R"({"partial":1})");
    CHECK(entry->errorJson == R"({"e":1})");
    CHECK(entry->outcome == Outcome::Failed);
}

TEST_CASE("History evicts oldest entries beyond its limit") {
    History history(2);
    const auto first = history.add(1, "A", "", t0);
    history.add(2, "B", "", t0);
    const auto third = history.add(3, "C", "", t0);
    REQUIRE(history.entries().size() == 2);
    CHECK(history.entries().front().commandName == "B");
    CHECK(history.find(first) == nullptr);
    CHECK(history.apply(first, response(ResponseStatus::Completed), t0) == nullptr);
    CHECK(history.find(third) != nullptr);
    history.clear();
    CHECK(history.entries().empty());
}

TEST_CASE("History round-trips through JSON") {
    History history;
    const auto done = history.add(3, "CId_GetTrackList", R"({"a":1})", t0);
    history.apply(done, response(ResponseStatus::Completed, 100, R"({"ok":true})"), t0 + 5ms);
    history.add(4, "CId_Other", "", t0 + 1s);

    History loaded;
    REQUIRE(loaded.loadJson(history.toJson()).has_value());
    REQUIRE(loaded.entries().size() == 2);
    const HistoryEntry& entry = loaded.entries().front();
    CHECK(entry.sequence == done);
    CHECK(entry.commandId == 3);
    CHECK(entry.commandName == "CId_GetTrackList");
    CHECK(entry.requestJson == R"({"a":1})");
    CHECK(entry.sentAt == t0);
    CHECK(entry.duration == 5ms);
    CHECK(entry.outcome == Outcome::Completed);
    CHECK(entry.taskId == "task-1");
    CHECK(entry.responseJson == R"({"ok":true})");
    CHECK(loaded.entries().back().outcome == Outcome::Pending);
    CHECK_FALSE(loaded.entries().back().duration.has_value());
    CHECK(loaded.add(1, "Next", "", t0) == 3);
}

TEST_CASE("History rejects invalid JSON without changing contents") {
    History history;
    history.add(1, "Keep", "", t0);
    CHECK_FALSE(history.loadJson("nope").has_value());
    CHECK_FALSE(history.loadJson(R"({"version": 2, "entries": []})").has_value());
    CHECK_FALSE(history.loadJson(R"({"version": 1, "entries": [{"sequence": 1}]})").has_value());
    const auto badOutcome = history.loadJson(
        R"({"version":1,"entries":[{"sequence":1,"command_id":1,"command_name":"A","request":"","sent_at_ms":0,"outcome":"weird"}]})");
    REQUIRE_FALSE(badOutcome.has_value());
    CHECK(badOutcome.error().contains("weird"));
    REQUIRE(history.entries().size() == 1);
    CHECK(history.entries().front().commandName == "Keep");
}

TEST_CASE("Outcome names round-trip") {
    for (const Outcome outcome :
         {Outcome::Pending, Outcome::InProgress, Outcome::Completed, Outcome::Failed, Outcome::Cancelled}) {
        CHECK(outcomeFromString(toString(outcome)) == outcome);
    }
    CHECK_FALSE(outcomeFromString("nope").has_value());
}
