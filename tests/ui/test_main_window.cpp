#include "../core/test_support.hpp"
#include "command_browser.hpp"
#include "request_editor.hpp"
#include "response_view.hpp"
#include "ui_support.hpp"

#include <ptslgui/fake_session.hpp>
#include <ptslgui/ui/main_window.hpp>

#include <QAction>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QTabWidget>
#include <QTest>
#include <QTreeWidget>
#include <catch2/catch_test_macros.hpp>

using namespace ptslgui;
using namespace ptslgui::ui;

namespace {

constexpr int makeWidget = 0;
constexpr int ping = 2;
constexpr int getVersion = 4;
constexpr int undo = 5;
constexpr int redo = 6;
constexpr int waitMs = 5000;

Response completed(std::string body) {
    return Response{.status = ResponseStatus::Completed,
                    .progress = 100,
                    .taskId = "t",
                    .bodyJson = std::move(body),
                    .errorJson = {}};
}

struct Fixture {
    test::TemporarySettings settings;
    FakePtslSession session;
    MainWindow window{test::fixtureCatalog(), test::fixtureSchema(), session, *settings.settings};

    Fixture() {
        session.setAutoDeliver(true);
        session.script(getVersion, {completed(R"({"version":2026,"version_minor":4})")});
    }

    template <typename T>
    T* child(const char* name) {
        auto* widget = window.findChild<T*>(QString::fromLatin1(name));
        REQUIRE(widget != nullptr);
        return widget;
    }

    QString text(const char* label) { return child<QLabel>(label)->text(); }

    void connectSession() {
        child<QAction>("connectAction")->trigger();
        REQUIRE(QTest::qWaitFor([this] { return text("connectionStatus").startsWith("Connected · PTSL"); }, waitMs));
    }
};

int countCommandItems(QTreeWidget* tree) {
    int count = 0;
    for (QTreeWidgetItemIterator it(tree); *it != nullptr; ++it) {
        if ((*it)->data(0, Qt::UserRole + 1).isValid()) {
            ++count;
        }
    }
    return count;
}

} // namespace

TEST_CASE("Main window lists commands grouped by category") {
    Fixture fixture;
    auto* tree = fixture.child<QTreeWidget>("commandTree");
    CHECK(tree->topLevelItemCount() == 5);
    CHECK(countCommandItems(tree) == 10);
    CHECK(fixture.child<QComboBox>("categoryFilter")->count() == 5);
    CHECK(fixture.text("connectionStatus") == "Disconnected");
    CHECK_FALSE(fixture.child<QPushButton>("sendButton")->isEnabled());
}

TEST_CASE("Command search and category filter narrow the list") {
    Fixture fixture;
    const auto* tree = fixture.child<QTreeWidget>("commandTree");
    fixture.child<QLineEdit>("commandSearch")->setText("ping");
    CHECK(tree->topLevelItemCount() == 1);
    CHECK(tree->topLevelItem(0)->text(0) == "Ping");

    fixture.child<QLineEdit>("commandSearch")->clear();
    auto* category = fixture.child<QComboBox>("categoryFilter");
    category->setCurrentIndex(category->findData("queries"));
    CHECK(tree->topLevelItemCount() == 1);
    CHECK(tree->topLevelItem(0)->text(0) == "List Widgets");
}

TEST_CASE("Selecting a command shows its documentation and examples") {
    Fixture fixture;
    REQUIRE(fixture.window.selectCommand(makeWidget));
    CHECK_FALSE(fixture.window.selectCommand(99));
    CHECK(fixture.text("commandTitle") == "Make Widget");
    CHECK(fixture.child<QPlainTextEdit>("requestEditor")->toPlainText() == "{}");
    CHECK(fixture.child<QComboBox>("exampleSelector")->count() == 2);
    CHECK(fixture.text("validationLabel").contains("Valid"));

    fixture.child<QPushButton>("loadExample")->click();
    CHECK(fixture.child<QPlainTextEdit>("requestEditor")->toPlainText().contains("\"w1\""));

    REQUIRE(fixture.window.selectCommand(ping));
    CHECK_FALSE(fixture.child<QPlainTextEdit>("requestEditor")->isEnabled());
    CHECK_FALSE(fixture.child<QComboBox>("exampleSelector")->isEnabled());
}

TEST_CASE("Invalid request JSON is reported") {
    Fixture fixture;
    REQUIRE(fixture.window.selectCommand(makeWidget));
    fixture.child<QPlainTextEdit>("requestEditor")->setPlainText(R"({"size": "big"})");
    REQUIRE(QTest::qWaitFor([&] { return !fixture.text("validationLabel").contains("Valid"); }, waitMs));
    CHECK_FALSE(fixture.text("validationLabel").isEmpty());
}

TEST_CASE("Format button pretty prints the request") {
    Fixture fixture;
    REQUIRE(fixture.window.selectCommand(makeWidget));
    auto* editor = fixture.child<QPlainTextEdit>("requestEditor");
    editor->setPlainText(R"({"name":"a","size":2})");
    fixture.child<QPushButton>("formatButton")->click();
    CHECK(editor->toPlainText().contains('\n'));
    CHECK(editor->toPlainText().contains("\"name\""));
}

TEST_CASE("Connecting queries the host version and enables sending") {
    Fixture fixture;
    fixture.connectSession();
    CHECK(fixture.text("connectionStatus") == "Connected · PTSL 2026.4");
    CHECK(fixture.window.hostVersion() == Version{.year = 2026, .minor = 4, .revision = 0});
    REQUIRE(fixture.window.selectCommand(makeWidget));
    CHECK(fixture.child<QPushButton>("sendButton")->isEnabled());

    fixture.child<QAction>("connectAction")->trigger();
    CHECK(fixture.text("connectionStatus") == "Disconnected");
    CHECK_FALSE(fixture.child<QPushButton>("sendButton")->isEnabled());
    CHECK_FALSE(fixture.window.hostVersion().has_value());
}

TEST_CASE("A failed connection is reported") {
    Fixture fixture;
    fixture.session.setConnectError("refused");
    fixture.child<QAction>("connectAction")->trigger();
    REQUIRE(QTest::qWaitFor([&] { return fixture.window.statusBar()->currentMessage().contains("refused"); }, waitMs));
    CHECK(fixture.text("connectionStatus") == "Disconnected");
}

TEST_CASE("Sending shows the response") {
    Fixture fixture;
    fixture.session.script(makeWidget, {completed(R"({"widget_id":"abc","created":true})")});
    fixture.connectSession();
    REQUIRE(fixture.window.selectCommand(makeWidget));
    fixture.child<QPlainTextEdit>("requestEditor")->setPlainText(R"({"name":"w"})");
    fixture.child<QPushButton>("sendButton")->click();

    REQUIRE(QTest::qWaitFor([&] { return fixture.text("responseStatus") == "Completed"; }, waitMs));
    CHECK(fixture.child<QPlainTextEdit>("responseRaw")->toPlainText().contains("\"widget_id\""));
    CHECK(fixture.child<QTreeWidget>("responseTree")->topLevelItemCount() == 2);
    CHECK(fixture.text("responseMeta").contains("CId_MakeWidget"));
    const auto sent = fixture.session.sent();
    REQUIRE_FALSE(sent.empty());
    CHECK(sent.back().requestJson.starts_with(R"({"name":"w",)"));
}

TEST_CASE("Failed responses show their errors") {
    Fixture fixture;
    fixture.session.script(
        ping, {Response{.status = ResponseStatus::Failed,
                        .progress = 0,
                        .taskId = {},
                        .bodyJson = {},
                        .errorJson = R"({"errors":[{"command_error_type":"PT_X","command_error_message":"nope"}]})"}});
    fixture.connectSession();
    REQUIRE(fixture.window.selectCommand(ping));
    fixture.child<QPushButton>("sendButton")->click();

    REQUIRE(QTest::qWaitFor([&] { return fixture.text("responseStatus") == "Failed"; }, waitMs));
    CHECK(fixture.child<QPlainTextEdit>("responseErrors")->toPlainText().contains("Error [PT_X]: nope"));
    const auto* tabs = fixture.child<QTabWidget>("responseTabs");
    CHECK(tabs->tabText(2) == "Errors (1)");
    CHECK(tabs->currentIndex() == 2);
}

TEST_CASE("A successful response shows the tree") {
    Fixture fixture;
    fixture.session.script(
        ping, {Response{.status = ResponseStatus::Failed,
                        .progress = 0,
                        .taskId = {},
                        .bodyJson = {},
                        .errorJson = R"({"errors":[{"command_error_type":"PT_X","command_error_message":"nope"}]})"}});
    fixture.connectSession();
    REQUIRE(fixture.window.selectCommand(ping));
    fixture.child<QPushButton>("sendButton")->click();
    REQUIRE(QTest::qWaitFor([&] { return fixture.text("responseStatus") == "Failed"; }, waitMs));
    const auto* tabs = fixture.child<QTabWidget>("responseTabs");
    CHECK(tabs->currentIndex() == 2);

    fixture.session.script(ping, {completed("{}")});
    fixture.child<QPushButton>("sendButton")->click();
    REQUIRE(QTest::qWaitFor([&] { return fixture.text("responseStatus") == "Completed"; }, waitMs));
    CHECK(tabs->currentIndex() == 0);
}

TEST_CASE("Undo and redo shortcuts send the Pro Tools commands without confirmation") {
    Fixture fixture;
    auto* undoAction = fixture.child<QAction>("undoAction");
    auto* redoAction = fixture.child<QAction>("redoAction");
    CHECK(undoAction->shortcut() == QKeySequence(QKeySequence::Undo));
    CHECK(redoAction->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z));
    CHECK_FALSE(undoAction->isEnabled());

    bool asked = false;
    fixture.window.setConfirmHandler([&](const CommandInfo&, bool&) {
        asked = true;
        return true;
    });
    fixture.connectSession();
    REQUIRE(undoAction->isEnabled());
    undoAction->trigger();
    redoAction->trigger();
    CHECK_FALSE(asked);

    const auto sent = fixture.session.sent();
    REQUIRE(sent.size() >= 2);
    CHECK(sent[sent.size() - 2].commandId == undo);
    CHECK(sent[sent.size() - 2].requestJson == R"({"levels":1})");
    CHECK(sent.back().commandId == redo);
    REQUIRE(QTest::qWaitFor([&] { return fixture.text("responseMeta").contains("CId_Redo"); }, waitMs));
}

TEST_CASE("Commands newer than the host are flagged") {
    Fixture fixture;
    fixture.session.script(getVersion, {completed(R"({"version":2023,"version_minor":1})")});
    fixture.connectSession();
    REQUIRE(fixture.window.selectCommand(makeWidget));
    fixture.child<QPushButton>("sendButton")->click();
    CHECK(fixture.window.statusBar()->currentMessage().contains("requires Pro Tools 2023.3"));
}

TEST_CASE("UI helpers") {
    CHECK(categoryLabel("session_read") == "Session read");
    CHECK(categoryLabel("") == "");
    CHECK(categoryLabel("io_setup") == "IO setup");
    CHECK(prettyJson(R"({"a":1})") == "{\n    \"a\": 1\n}");
    CHECK(prettyJson("not json") == "not json");

    const auto* command = test::fixtureCatalog().findById(makeWidget);
    REQUIRE(command != nullptr);
    const QString markdown = commandMarkdown(*command);
    CHECK(markdown.startsWith("Makes a new widget."));
    CHECK(markdown.contains("`MakeWidgetRequestBody`"));
    CHECK(markdown.contains("Since 2023.3"));
}

#if defined(PTSLGUI_SDK_PROTO) && defined(PTSLGUI_SDK_CATALOG)
TEST_CASE("Render the main window with the real catalog", "[.screenshot]") {
    const QByteArray path = qgetenv("PTSLGUI_SCREENSHOT");
    REQUIRE_FALSE(path.isEmpty());
    const auto schema = ProtoSchema::fromProtoText(test::readFile(PTSLGUI_SDK_PROTO));
    const auto catalog = CommandCatalog::fromJson(test::readFile(PTSLGUI_SDK_CATALOG));
    REQUIRE(schema.has_value());
    REQUIRE(catalog.has_value());

    FakePtslSession session;
    session.setAutoDeliver(true);
    const auto* version = catalog->findByName("GetPTSLVersion");
    const auto* trackList = catalog->findByName("GetTrackList");
    REQUIRE(version != nullptr);
    REQUIRE(trackList != nullptr);
    session.script(version->id, {completed(R"({"version":2026,"version_minor":4})")});
    session.script(trackList->id,
                   {completed(R"({"track_list":[{"name":"Audio 1","type":"TT_Audio","id":"1"},)"
                              R"({"name":"Audio 2","type":"TT_Audio","id":"2"}],"pagination_response":{"total":2}})")});

    test::TemporarySettings settings;
    MainWindow window(*catalog, *schema, session, *settings.settings);
    window.setConfirmHandler([](const CommandInfo&, bool&) { return true; });
    window.show();
    window.findChild<QAction*>("connectAction")->trigger();
    REQUIRE(QTest::qWaitFor([&] { return window.hostVersion().has_value(); }, waitMs));
    const QByteArray commandName = qgetenv("PTSLGUI_SCREENSHOT_COMMAND");
    const auto* shown = commandName.isEmpty() ? trackList : catalog->findByName(commandName.toStdString());
    REQUIRE(shown != nullptr);
    window.resize(1500, 1000);
    REQUIRE(window.selectCommand(shown->id));
    window.findChild<QPushButton*>("loadExample")->click();
    window.findChild<QPushButton*>("sendButton")->click();
    QTest::qWait(300);
    REQUIRE(window.grab().save(QString::fromLocal8Bit(path)));
}
#endif
