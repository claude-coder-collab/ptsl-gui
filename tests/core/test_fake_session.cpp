#include <ptslgui/fake_session.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>
#include <vector>

using namespace ptslgui;

namespace {

struct Recorder {
    std::vector<std::pair<RequestId, Response>> responses;

    ResponseSink sink() {
        return [this](RequestId id, const Response& response) { responses.emplace_back(id, response); };
    }
};

} // namespace

TEST_CASE("FakePtslSession connects and records settings") {
    FakePtslSession session;
    CHECK(session.state() == ConnectionState::Disconnected);
    ConnectionSettings settings;
    settings.address = "host:1";
    REQUIRE(session.connect(settings).has_value());
    CHECK(session.state() == ConnectionState::Connected);
    CHECK(session.lastSettings().address == "host:1");
    session.disconnect();
    CHECK(session.state() == ConnectionState::Disconnected);
}

TEST_CASE("FakePtslSession connect can fail") {
    FakePtslSession session;
    session.setConnectError("refused");
    const auto result = session.connect({});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == "refused");
    CHECK(session.state() == ConnectionState::Disconnected);
}

TEST_CASE("FakePtslSession queues default and scripted responses") {
    FakePtslSession session;
    REQUIRE(session.connect({}).has_value());
    session.script(
        7,
        {Response{.status = ResponseStatus::InProgress, .progress = 50, .taskId = "t", .bodyJson = {}, .errorJson = {}},
         Response{.status = ResponseStatus::Completed,
                  .progress = 100,
                  .taskId = "t",
                  .bodyJson = R"({"x":1})",
                  .errorJson = {}}});
    Recorder recorder;
    const RequestId plain = session.send(1, "{}", recorder.sink());
    const RequestId scripted = session.send(7, R"({"a":2})", recorder.sink());
    CHECK(plain != scripted);
    CHECK(recorder.responses.empty());

    CHECK(session.deliverPending() == 3);
    REQUIRE(recorder.responses.size() == 3);
    CHECK(recorder.responses[0].first == plain);
    CHECK(recorder.responses[0].second.status == ResponseStatus::Completed);
    CHECK(recorder.responses[0].second.bodyJson == "{}");
    CHECK(recorder.responses[1].second.progress == 50);
    CHECK(recorder.responses[2].second.bodyJson == R"({"x":1})");
    CHECK(session.deliverPending() == 0);

    const auto sent = session.sent();
    REQUIRE(sent.size() == 2);
    CHECK(sent[1].commandId == 7);
    CHECK(sent[1].requestJson == R"({"a":2})");
}

TEST_CASE("FakePtslSession auto-delivers synchronously") {
    FakePtslSession session;
    REQUIRE(session.connect({}).has_value());
    session.setAutoDeliver(true);
    Recorder recorder;
    session.send(1, "", recorder.sink());
    CHECK(recorder.responses.size() == 1);
}

TEST_CASE("FakePtslSession fails requests when disconnected") {
    FakePtslSession session;
    Recorder recorder;
    session.send(1, "", recorder.sink());
    session.deliverPending();
    REQUIRE(recorder.responses.size() == 1);
    CHECK(recorder.responses[0].second.status == ResponseStatus::Failed);
    CHECK(recorder.responses[0].second.errorJson.contains("not connected"));
}

TEST_CASE("FakePtslSession cancels pending requests") {
    FakePtslSession session;
    REQUIRE(session.connect({}).has_value());
    Recorder recorder;
    session.send(1, "", recorder.sink());
    session.send(2, "", recorder.sink());
    session.cancelAll();
    REQUIRE(recorder.responses.size() == 2);
    CHECK(recorder.responses[0].second.status == ResponseStatus::Cancelled);
    CHECK(session.deliverPending() == 0);

    session.send(3, "", recorder.sink());
    session.disconnect();
    CHECK(recorder.responses.size() == 3);
    CHECK(recorder.responses[2].second.status == ResponseStatus::Cancelled);
}

TEST_CASE("FakePtslSession is safe to use from several threads") {
    FakePtslSession session;
    REQUIRE(session.connect({}).has_value());
    std::atomic<int> delivered = 0;
    const ResponseSink sink = [&](RequestId, const Response&) { ++delivered; };
    {
        std::vector<std::jthread> threads;
        threads.reserve(4);
        for (int t = 0; t < 4; ++t) {
            threads.emplace_back([&] {
                for (int i = 0; i < 100; ++i) {
                    session.send(i, "", sink);
                    if (i % 10 == 0) {
                        session.deliverPending();
                    }
                }
            });
        }
    }
    session.deliverPending();
    CHECK(delivered == 400);
    CHECK(session.sent().size() == 400);
}
