#include "test_support.hpp"

#include <ptslgui/controller.hpp>
#include <ptslgui/fake_session.hpp>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <deque>

using namespace ptslgui;
using namespace std::chrono_literals;
using nlohmann::json;

namespace {

constexpr int makeWidget = 0;
constexpr int listWidgets = 1;
constexpr int ping = 2;

struct Fixture {
    FakePtslSession session;
    History history;
    std::chrono::system_clock::time_point now{std::chrono::milliseconds(1'000)};
    std::deque<std::function<void()>> posted;

    Fixture() { REQUIRE(session.connect({}).has_value()); }

    RequestController controller() {
        return {test::fixtureCatalog(),
                test::fixtureSchema(),
                session,
                history,
                [this](std::function<void()> task) { posted.push_back(std::move(task)); },
                [this] { return now; }};
    }

    void runPosted() {
        while (!posted.empty()) {
            const auto task = std::move(posted.front());
            posted.pop_front();
            task();
        }
    }
};

} // namespace

TEST_CASE("Controller prepares requests against the schema") {
    Fixture fixture;
    const auto controller = fixture.controller();
    CHECK(json::parse(controller.prepare(makeWidget, R"({"name": "a", "size": 0})").value()) == json{{"name", "a"}});
    CHECK(controller.prepare(makeWidget, "").value() == "{}");
    CHECK(controller.prepare(listWidgets, R"({"limit": 2})").value() == R"({"limit":2})");
    CHECK(controller.prepare(ping, "").value().empty());
    CHECK(controller.prepare(ping, " { } ").value().empty());

    const auto bodyForPing = controller.prepare(ping, R"({"a": 1})");
    REQUIRE_FALSE(bodyForPing.has_value());
    CHECK(bodyForPing.error().contains("Ping"));
    CHECK_FALSE(controller.prepare(makeWidget, R"({"bogus": 1})").has_value());
    CHECK(controller.prepare(42, "").error().contains("42"));
}

TEST_CASE("Controller sends, records history and reports updates on the dispatcher") {
    Fixture fixture;
    auto controller = fixture.controller();
    fixture.session.script(
        makeWidget,
        {Response{
             .status = ResponseStatus::InProgress, .progress = 30, .taskId = "t1", .bodyJson = {}, .errorJson = {}},
         Response{.status = ResponseStatus::Completed,
                  .progress = 100,
                  .taskId = "t1",
                  .bodyJson = R"({"widget_id":"w"})",
                  .errorJson = {}}});
    std::vector<Outcome> updates;
    const auto sequence = controller.send(makeWidget, R"({"name": "a"})",
                                          [&](const HistoryEntry& entry) { updates.push_back(entry.outcome); });
    REQUIRE(sequence.has_value());

    const auto sent = fixture.session.sent();
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].commandId == makeWidget);
    CHECK(json::parse(sent[0].requestJson) == json::parse(R"({
        "name": "a", "size": 0, "colour": "WColour_None", "visible": false, "tags": [], "parts": [],
        "attributes": {}, "payload": "", "count": 0
    })"));
    REQUIRE(fixture.history.find(*sequence) != nullptr);
    CHECK(fixture.history.find(*sequence)->requestJson == sent[0].requestJson);
    CHECK(fixture.history.find(*sequence)->commandName == "CId_MakeWidget");
    CHECK(fixture.history.find(*sequence)->outcome == Outcome::Pending);

    fixture.session.deliverPending();
    CHECK(updates.empty());
    CHECK(fixture.posted.size() == 2);

    fixture.now += 40ms;
    fixture.runPosted();
    CHECK(updates == std::vector<Outcome>{Outcome::InProgress, Outcome::Completed});
    const HistoryEntry* entry = fixture.history.find(*sequence);
    REQUIRE(entry != nullptr);
    CHECK(entry->responseJson == R"({"widget_id":"w"})");
    CHECK(entry->duration == 40ms);
}

TEST_CASE("Controller rejects invalid requests without sending") {
    Fixture fixture;
    auto controller = fixture.controller();
    CHECK_FALSE(controller.send(makeWidget, "{", {}).has_value());
    CHECK(fixture.session.sent().empty());
    CHECK(fixture.history.entries().empty());
}

TEST_CASE("Controller refuses to send while disconnected") {
    Fixture fixture;
    fixture.session.disconnect();
    auto controller = fixture.controller();
    const auto result = controller.send(ping, "", {});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == "not connected");
}

TEST_CASE("Controller ignores responses after it is destroyed") {
    Fixture fixture;
    int updates = 0;
    {
        auto controller = fixture.controller();
        REQUIRE(controller.send(ping, "", [&](const HistoryEntry&) { ++updates; }).has_value());
        fixture.session.deliverPending();
    }
    fixture.runPosted();
    CHECK(updates == 0);
    CHECK(fixture.history.entries().front().outcome == Outcome::Pending);
}

TEST_CASE("Controller runs inline without a dispatcher") {
    FakePtslSession session;
    REQUIRE(session.connect({}).has_value());
    session.setAutoDeliver(true);
    History history;
    RequestController controller(test::fixtureCatalog(), test::fixtureSchema(), session, history);
    Outcome last = Outcome::Pending;
    REQUIRE(controller.send(ping, "", [&](const HistoryEntry& entry) { last = entry.outcome; }).has_value());
    CHECK(last == Outcome::Completed);
    CHECK(history.entries().front().duration.has_value());
}
