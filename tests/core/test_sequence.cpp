#include "test_support.hpp"

#include <ptslgui/fake_session.hpp>
#include <ptslgui/sequence.hpp>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <deque>

using namespace ptslgui;
using nlohmann::json;

namespace {

constexpr int makeWidget = 0;
constexpr int listWidgets = 1;
constexpr int ping = 2;

SubstitutionContext context() {
    return SubstitutionContext{
        .responses = {{"list", R"({"widgets":[{"name":"w1","size":3},{"name":"w2","size":5}],"total":2})"},
                      {"empty", ""}},
        .variables = {{"n", "4"}, {"title", "My \"widget\""}, {"tags", R"(["a","b"])"}}};
}

Response completed(std::string body) {
    return Response{.status = ResponseStatus::Completed,
                    .progress = 100,
                    .taskId = {},
                    .bodyJson = std::move(body),
                    .errorJson = {}};
}

Response failed() {
    return Response{.status = ResponseStatus::Failed,
                    .progress = 0,
                    .taskId = {},
                    .bodyJson = {},
                    .errorJson = R"({"errors":[{"command_error_message":"boom"}]})"};
}

SequenceStep step(std::string label, std::string command, std::string request = "{}") {
    return SequenceStep{.label = std::move(label),
                        .commandName = std::move(command),
                        .requestTemplate = std::move(request),
                        .enabled = true,
                        .continueOnError = false};
}

struct Fixture {
    FakePtslSession session;
    History history;
    std::deque<std::function<void()>> posted;
    Dispatcher dispatcher = [this](std::function<void()> task) { posted.push_back(std::move(task)); };
    RequestController controller{test::fixtureCatalog(), test::fixtureSchema(), session, history, dispatcher};
    SequenceRunner runner{test::fixtureCatalog(), controller, dispatcher};
    std::vector<std::pair<std::size_t, StepStatus>> steps;
    std::vector<std::string> errors;
    std::optional<SequenceOutcome> outcome;

    Fixture() {
        session.setAutoDeliver(true);
        REQUIRE(session.connect({}).has_value());
    }

    bool start(Sequence sequence) {
        return runner.start(
            std::move(sequence),
            [this](std::size_t index, const StepResult& result, const HistoryEntry*) {
                if (steps.empty() || steps.back() != std::pair{index, result.status}) {
                    steps.emplace_back(index, result.status);
                }
                if (!result.error.empty()) {
                    errors.push_back(result.error);
                }
            },
            [this](SequenceOutcome result) { outcome = result; });
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

TEST_CASE("Sequences round-trip through JSON") {
    Sequence sequence{
        .name = "Setup",
        .variables = {{"b", "2"}, {"a", "x"}},
        .steps = {step("make", "CId_MakeWidget", R"({"name": "{{vars.a}}"})"), SequenceStep{.label = "ping",
                                                                                            .commandName = "CId_Ping",
                                                                                            .requestTemplate = "",
                                                                                            .enabled = false,
                                                                                            .continueOnError = true}}};
    const auto loaded = sequenceFromJson(sequenceToJson(sequence));
    REQUIRE(loaded.has_value());
    CHECK(loaded->name == "Setup");
    CHECK(loaded->variables == sequence.variables);
    REQUIRE(loaded->steps.size() == 2);
    CHECK(loaded->steps[0].requestTemplate == R"({"name": "{{vars.a}}"})");
    CHECK_FALSE(loaded->steps[1].enabled);
    CHECK(loaded->steps[1].continueOnError);
}

TEST_CASE("Invalid sequence files are rejected") {
    CHECK_FALSE(sequenceFromJson("[").has_value());
    CHECK_FALSE(sequenceFromJson(R"({"version": 2, "steps": []})").has_value());
    CHECK_FALSE(sequenceFromJson(R"({"version": 1})").has_value());
    CHECK_FALSE(sequenceFromJson(R"({"version": 1, "steps": [{"label": "a", "command": "CId_Ping"}]})").has_value());
    CHECK_FALSE(
        sequenceFromJson(R"({"version": 1, "steps": [{"label": "1a", "command": "c", "request": ""}]})").has_value());
    CHECK(sequenceFromJson(R"({"version": 1, "steps": [{"label": "a", "command": "c", "request": ""},
                                                       {"label": "a", "command": "c", "request": ""}]})")
              .error()
              .contains("duplicate"));
    CHECK_FALSE(sequenceFromJson(R"({"version": 1, "variables": {"x": 1}, "steps": []})").has_value());
    const auto minimal =
        sequenceFromJson(R"({"version": 1, "steps": [{"label": "a", "command": "c", "request": ""}]})");
    REQUIRE(minimal.has_value());
    CHECK(minimal->steps[0].enabled);
}

TEST_CASE("Step labels are validated and made unique") {
    CHECK(isValidLabel("tracks_2"));
    CHECK(isValidLabel("_x"));
    CHECK_FALSE(isValidLabel(""));
    CHECK_FALSE(isValidLabel("2x"));
    CHECK_FALSE(isValidLabel("a.b"));
    CHECK_FALSE(isValidLabel("vars"));

    const Sequence sequence{
        .name = {}, .variables = {}, .steps = {step("GetTrackList", "c"), step("GetTrackList_2", "c")}};
    CHECK(uniqueStepLabel(sequence, "Ping") == "Ping");
    CHECK(uniqueStepLabel(sequence, "GetTrackList") == "GetTrackList_3");
    CHECK(uniqueStepLabel(sequence, "Get Track") == "Get_Track");
    CHECK(uniqueStepLabel(sequence, "1st") == "step_1st");
    CHECK(uniqueStepLabel(sequence, "") == "step_");
}

TEST_CASE("Placeholders are detected") {
    CHECK(hasPlaceholders(R"({"a": "{{x.y}}"})"));
    CHECK_FALSE(hasPlaceholders(R"({"a": "{x}"})"));
    CHECK_FALSE(hasPlaceholders("{{"));
}

TEST_CASE("Whole-value placeholders keep the JSON type") {
    const auto ctx = context();
    CHECK(json::parse(substitute(R"({"size": "{{list.widgets[1].size}}"})", ctx).value()) == json{{"size", 5}});
    CHECK(json::parse(substitute(R"({"size": {{ list.widgets[0].size }}})", ctx).value()) == json{{"size", 3}});
    CHECK(json::parse(substitute(R"({"w": "{{list.widgets[0]}}"})", ctx).value()) ==
          json::parse(R"({"w": {"name": "w1", "size": 3}})"));
    CHECK(json::parse(substitute(R"({"n": "{{vars.n}}", "tags": {{vars.tags}}})", ctx).value()) ==
          json::parse(R"({"n": 4, "tags": ["a", "b"]})"));
    CHECK(json::parse(substitute(R"({"name": "{{vars.title}}"})", ctx).value()) == json{{"name", "My \"widget\""}});
    CHECK(json::parse(substitute(R"({"first": "{{vars.tags[0]}}"})", ctx).value()) == json{{"first", "a"}});
}

TEST_CASE("Placeholders inside longer strings insert text") {
    const auto ctx = context();
    CHECK(json::parse(substitute(R"x({"name": "copy of {{list.widgets[0].name}} ({{list.total}})"})x", ctx).value()) ==
          json{{"name", "copy of w1 (2)"}});
    CHECK(json::parse(substitute(R"({"name": "x {{vars.title}}"})", ctx).value()) == json{{"name", "x My \"widget\""}});
    CHECK(json::parse(substitute(R"({"name": "\"{{vars.n}}\""})", ctx).value()) == json{{"name", "\"4\""}});
    CHECK(substitute(R"({"name": "plain"})", ctx).value() == R"({"name": "plain"})");
}

TEST_CASE("Substitution reports unresolved placeholders") {
    const auto ctx = context();
    CHECK(substitute(R"({"a": "{{later.x}}"})", ctx).error().contains("no response from step 'later'"));
    CHECK(substitute(R"({"a": "{{list.missing}}"})", ctx).error().contains("no field 'missing'"));
    CHECK(substitute(R"({"a": "{{list.widgets[9]}}"})", ctx).error().contains("no item [9]"));
    CHECK(substitute(R"({"a": "{{list.widgets[x]}}"})", ctx).error().contains("index"));
    CHECK(substitute(R"({"a": "{{vars.nope}}"})", ctx).error().contains("unknown variable"));
    CHECK(substitute(R"({"a": "{{vars}}"})", ctx).error().contains("vars.<name>"));
    CHECK(substitute(R"({"a": "{{}}"})", ctx).has_value() == false);
    CHECK(substitute(R"({"a": "{{list.widgets"})", ctx).error().contains("unterminated"));
    CHECK(substitute(R"({"a": "{{empty.x}}"})", ctx).error().contains("no field 'x'"));
}

TEST_CASE("Runner sends steps in order and substitutes earlier responses") {
    Fixture fixture;
    fixture.session.script(listWidgets, {completed(R"({"widgets":[{"name":"first"}]})")});
    Sequence sequence{
        .name = "s",
        .variables = {{"size", "7"}},
        .steps = {step("list", "CId_ListWidgets", R"({"limit": 1})"),
                  step("make", "CId_MakeWidget", R"({"name": "{{list.widgets[0].name}}", "size": "{{vars.size}}"})")}};
    const bool started = fixture.start(std::move(sequence));
    REQUIRE(started);
    CHECK(fixture.runner.running());
    CHECK_FALSE(fixture.start(Sequence{}));
    fixture.runPosted();

    CHECK(fixture.outcome == SequenceOutcome::Completed);
    CHECK_FALSE(fixture.runner.running());
    const auto sent = fixture.session.sent();
    REQUIRE(sent.size() == 2);
    CHECK(sent[0].commandId == listWidgets);
    CHECK(sent[1].commandId == makeWidget);
    const auto made = json::parse(sent[1].requestJson);
    CHECK(made["name"] == "first");
    CHECK(made["size"] == 7);
    CHECK(fixture.history.entries().size() == 2);
    CHECK(fixture.steps == std::vector<std::pair<std::size_t, StepStatus>>{{0, StepStatus::Running},
                                                                           {0, StepStatus::Completed},
                                                                           {1, StepStatus::Running},
                                                                           {1, StepStatus::Completed}});
}

TEST_CASE("Runner skips disabled steps") {
    Fixture fixture;
    Sequence sequence{.name = {}, .variables = {}, .steps = {step("a", "CId_Ping"), step("b", "CId_Ping")}};
    sequence.steps[0].enabled = false;
    const bool started = fixture.start(std::move(sequence));
    REQUIRE(started);
    fixture.runPosted();
    CHECK(fixture.outcome == SequenceOutcome::Completed);
    CHECK(fixture.session.sent().size() == 1);
    CHECK(fixture.steps.front() == std::pair<std::size_t, StepStatus>{0, StepStatus::Skipped});
}

TEST_CASE("Runner stops at a failed step unless it continues on error") {
    Fixture fixture;
    fixture.session.script(ping, {failed()});
    fixture.session.script(listWidgets, {failed()});
    Sequence sequence{.name = {},
                      .variables = {},
                      .steps = {step("a", "CId_Ping"), step("b", "CId_ListWidgets"), step("c", "CId_MakeWidget")}};
    sequence.steps[0].continueOnError = true;
    const bool started = fixture.start(std::move(sequence));
    REQUIRE(started);
    fixture.runPosted();
    CHECK(fixture.outcome == SequenceOutcome::Failed);
    CHECK(fixture.session.sent().size() == 2);
    CHECK(fixture.steps.back() == std::pair<std::size_t, StepStatus>{1, StepStatus::Failed});
}

TEST_CASE("Runner fails steps that cannot be prepared") {
    Fixture fixture;
    REQUIRE(fixture.start(Sequence{
        .name = {}, .variables = {}, .steps = {step("a", "CId_Nope"), step("b", "CId_Ping", R"({"x": "{{a.y}}"})")}}));
    fixture.runPosted();
    CHECK(fixture.outcome == SequenceOutcome::Failed);
    REQUIRE(fixture.errors.size() == 1);
    CHECK(fixture.errors[0].contains("unknown command"));

    fixture.errors.clear();
    Sequence sequence{.name = {},
                      .variables = {},
                      .steps = {step("a", "CId_ListWidgets", R"({"bogus": 1})"),
                                step("b", "CId_ListWidgets", R"({"limit": "{{a.y}}"})")}};
    sequence.steps[0].continueOnError = true;
    const bool started = fixture.start(std::move(sequence));
    REQUIRE(started);
    fixture.runPosted();
    CHECK(fixture.outcome == SequenceOutcome::Failed);
    REQUIRE(fixture.errors.size() == 2);
    CHECK(fixture.errors[1].contains("no response from step 'a'"));
    CHECK(fixture.session.sent().empty());
}

TEST_CASE("Runner stops after the running step when asked") {
    Fixture fixture;
    fixture.session.setAutoDeliver(false);
    REQUIRE(
        fixture.start(Sequence{.name = {}, .variables = {}, .steps = {step("a", "CId_Ping"), step("b", "CId_Ping")}}));
    fixture.runPosted();
    REQUIRE(fixture.session.sent().size() == 1);
    fixture.runner.stop();
    fixture.session.deliverPending();
    fixture.runPosted();
    CHECK(fixture.outcome == SequenceOutcome::Stopped);
    CHECK(fixture.session.sent().size() == 1);
    CHECK(fixture.steps.back() == std::pair<std::size_t, StepStatus>{0, StepStatus::Completed});
}

TEST_CASE("Runner treats a cancelled step as stopped") {
    Fixture fixture;
    fixture.session.setAutoDeliver(false);
    REQUIRE(
        fixture.start(Sequence{.name = {}, .variables = {}, .steps = {step("a", "CId_Ping"), step("b", "CId_Ping")}}));
    fixture.runPosted();
    fixture.session.cancelAll();
    fixture.runPosted();
    CHECK(fixture.outcome == SequenceOutcome::Stopped);
    CHECK(fixture.steps.back() == std::pair<std::size_t, StepStatus>{0, StepStatus::Cancelled});
}

TEST_CASE("Runner reports the history entry of a sent step") {
    Fixture fixture;
    fixture.session.setAutoDeliver(false);
    std::optional<std::uint64_t> reported;
    REQUIRE(fixture.runner.start(Sequence{.name = {}, .variables = {}, .steps = {step("a", "CId_Ping")}},
                                 [&](std::size_t, const StepResult& result, const HistoryEntry*) {
                                     if (result.historySequence) {
                                         reported = result.historySequence;
                                     }
                                 },
                                 {}));
    fixture.runPosted();
    REQUIRE(reported.has_value());
    CHECK(fixture.history.find(*reported) != nullptr);
}

TEST_CASE("Runner ignores responses after destruction") {
    FakePtslSession session;
    REQUIRE(session.connect({}).has_value());
    History history;
    std::deque<std::function<void()>> posted;
    const Dispatcher dispatcher = [&posted](std::function<void()> task) { posted.push_back(std::move(task)); };
    RequestController controller{test::fixtureCatalog(), test::fixtureSchema(), session, history, dispatcher};
    bool called = false;
    {
        SequenceRunner runner{test::fixtureCatalog(), controller, dispatcher};
        REQUIRE(runner.start(Sequence{.name = {}, .variables = {}, .steps = {step("a", "CId_Ping")}},
                             [&](std::size_t, const StepResult&, const HistoryEntry*) { called = true; }, {}));
    }
    while (!posted.empty()) {
        const auto task = std::move(posted.front());
        posted.pop_front();
        task();
    }
    CHECK_FALSE(called);
}
