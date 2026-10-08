#include <ptslgui/protocol.hpp>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using namespace ptslgui;
using namespace ptslgui::protocol;

TEST_CASE("Task status maps to response status") {
    CHECK(statusFromTaskStatus(0) == ResponseStatus::InProgress);
    CHECK(statusFromTaskStatus(1) == ResponseStatus::InProgress);
    CHECK(statusFromTaskStatus(2) == ResponseStatus::InProgress);
    CHECK(statusFromTaskStatus(5) == ResponseStatus::InProgress);
    CHECK(statusFromTaskStatus(3) == ResponseStatus::Completed);
    CHECK(statusFromTaskStatus(6) == ResponseStatus::Completed);
    CHECK(statusFromTaskStatus(4) == ResponseStatus::Failed);
    CHECK(statusFromTaskStatus(7) == ResponseStatus::Failed);
    CHECK(statusFromTaskStatus(-1) == ResponseStatus::Failed);
    CHECK(statusFromTaskStatus(99) == ResponseStatus::Failed);
}

TEST_CASE("Version is read from a GetPTSLVersion response") {
    CHECK(versionFromResponse(R"({"version": 2025, "version_minor": 10, "version_revision": 1})") ==
          Version{.year = 2025, .minor = 10, .revision = 1});
    CHECK(versionFromResponse(R"({"version": 2024})") == Version{.year = 2024, .minor = 0, .revision = 0});
    CHECK_FALSE(versionFromResponse("{}").has_value());
    CHECK_FALSE(versionFromResponse(R"({"version": "2025"})").has_value());
    CHECK_FALSE(versionFromResponse(R"({"version": 2025, "version_minor": "x"})").has_value());
    CHECK_FALSE(versionFromResponse("not json").has_value());
    CHECK_FALSE(versionFromResponse("[]").has_value());
}

TEST_CASE("Host readiness is read from a HostReadyCheck response") {
    CHECK(hostReadyFromResponse(R"({"is_host_ready": true})") == true);
    CHECK(hostReadyFromResponse(R"({"is_host_ready": false})") == false);
    CHECK(hostReadyFromResponse("{}") == false);
    CHECK_FALSE(hostReadyFromResponse(R"({"is_host_ready": 1})").has_value());
    CHECK_FALSE(hostReadyFromResponse("").has_value());
}

TEST_CASE("RegisterConnection body escapes names") {
    const auto body = nlohmann::json::parse(registerConnectionBody("Acme \"Audio\"", "Tool"));
    CHECK(body == nlohmann::json{{"company_name", "Acme \"Audio\""}, {"application_name", "Tool"}});
}

TEST_CASE("Errors are parsed from the supported shapes") {
    CHECK(errorsFromJson("").empty());
    CHECK(errorsFromJson("  ").empty());

    const auto list = errorsFromJson(R"({"errors": [
        {"command_error_type": "PT_UnknownError", "command_error_message": "boom", "is_warning": false},
        {"command_error_type": 3, "command_error_message": "careful", "is_warning": true},
        "ignored"
    ]})");
    REQUIRE(list.size() == 2);
    CHECK(list[0].type == "PT_UnknownError");
    CHECK(list[0].message == "boom");
    CHECK_FALSE(list[0].isWarning);
    CHECK(list[1].type == "3");
    CHECK(list[1].isWarning);

    const auto single = errorsFromJson(R"({"command_error_type": "X", "command_error_message": "one"})");
    REQUIRE(single.size() == 1);
    CHECK(single[0].message == "one");

    const auto plain = errorsFromJson(R"({"message": "not connected"})");
    REQUIRE(plain.size() == 1);
    CHECK(plain[0].message == "not connected");

    const auto text = errorsFromJson("gRPC failure");
    REQUIRE(text.size() == 1);
    CHECK(text[0].message == "gRPC failure");
}
