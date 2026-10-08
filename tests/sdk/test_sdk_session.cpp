#include "../core/test_support.hpp"

#include <ptslgui/schema.hpp>
#include <ptslgui/sdk_session.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <future>

using namespace ptslgui;
using namespace std::chrono_literals;

namespace {

SdkSessionOptions fastOptions() {
    return SdkSessionOptions{.readyTimeout = 300ms, .launchReadyTimeout = 300ms, .pollInterval = 50ms};
}

} // namespace

TEST_CASE("SDK session reports an unreachable host", "[sdk]") {
    SdkPtslSession session(fastOptions());
    ConnectionSettings settings;
    settings.address = "127.0.0.1:1";
    const auto start = std::chrono::steady_clock::now();
    const auto result = session.connect(settings);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().contains("not ready at 127.0.0.1:1"));
    CHECK(std::chrono::steady_clock::now() - start < 30s);
    CHECK(session.state() == ConnectionState::Disconnected);
}

TEST_CASE("SDK session fails requests while disconnected", "[sdk]") {
    SdkPtslSession session(fastOptions());
    std::promise<Response> received;
    session.send(13, "", [&](RequestId, const Response& response) { received.set_value(response); });
    const Response response = received.get_future().get();
    CHECK(response.status == ResponseStatus::Failed);
    CHECK(response.errorJson.contains("not connected"));
    session.cancelAll();
    session.disconnect();
}

TEST_CASE("Runtime schema and SDK framework coexist in one process", "[sdk]") {
    const auto schema = ProtoSchema::fromProtoText(test::readFile(PTSLGUI_SDK_PROTO));
    REQUIRE(schema.has_value());
    SdkPtslSession session(fastOptions());
    ConnectionSettings settings;
    settings.address = "127.0.0.1:1";
    CHECK_FALSE(session.connect(settings).has_value());
    CHECK(schema->normalizeJson("RegisterConnectionRequestBody", R"({"company_name":"a"})").value() ==
          R"({"company_name":"a"})");
}
