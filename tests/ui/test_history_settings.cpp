#include "../core/test_support.hpp"
#include "history_panel.hpp"
#include "preferences_dialog.hpp"
#include "ui_support.hpp"

#include <ptslgui/fake_session.hpp>
#include <ptslgui/ui/app_settings.hpp>
#include <ptslgui/ui/main_window.hpp>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QTest>
#include <QTreeWidget>
#include <catch2/catch_test_macros.hpp>

using namespace ptslgui;
using namespace ptslgui::ui;

namespace {

constexpr int makeWidget = 0;
constexpr int ping = 2;
constexpr int deleteWidget = 3;
constexpr int waitMs = 5000;

struct Fixture {
    test::TemporarySettings settings;
    FakePtslSession session;
    std::unique_ptr<MainWindow> window =
        std::make_unique<MainWindow>(test::fixtureCatalog(), test::fixtureSchema(), session, *settings.settings);
    int confirmations = 0;
    bool answer = true;
    bool dontAskAgain = false;

    Fixture() {
        session.setAutoDeliver(true);
        window->setConfirmHandler([this](const CommandInfo&, bool& dontAsk) {
            ++confirmations;
            dontAsk = dontAskAgain;
            return answer;
        });
        window->findChild<QAction*>("connectAction")->trigger();
        REQUIRE(QTest::qWaitFor([this] { return session.state() == ConnectionState::Connected; }, waitMs));
        QTest::qWait(50);
    }

    template <typename T>
    T* child(const char* name) {
        auto* widget = window->findChild<T*>(QString::fromLatin1(name));
        REQUIRE(widget != nullptr);
        return widget;
    }

    std::size_t sentCount(int commandId) const {
        std::size_t count = 0;
        for (const auto& request : session.sent()) {
            count += request.commandId == commandId ? 1 : 0;
        }
        return count;
    }

    void send(int commandId, const QString& json = {}) {
        REQUIRE(window->selectCommand(commandId));
        if (!json.isNull()) {
            child<QPlainTextEdit>("requestEditor")->setPlainText(json);
        }
        child<QPushButton>("sendButton")->click();
        QTest::qWait(20);
    }
};

} // namespace

TEST_CASE("AppSettings defaults and round trip") {
    test::TemporarySettings temporary;
    AppSettings settings(*temporary.settings);
    CHECK(settings.confirmMutating());
    CHECK(settings.rememberRequests());
    CHECK(settings.address() == "localhost:31416");
    CHECK_FALSE(settings.launchHost());
    CHECK_FALSE(settings.lastRequest("CId_X").has_value());

    settings.setConfirmMutating(false);
    settings.setRememberRequests(false);
    settings.setAddress("host:1");
    settings.setLaunchHost(true);
    settings.setLastRequest("CId_X", R"({"a":1})");
    settings.setLastCommand("CId_X");
    settings.sync();

    QSettings reopened(temporary.path(), QSettings::IniFormat);
    const AppSettings again(reopened);
    CHECK_FALSE(again.confirmMutating());
    CHECK_FALSE(again.rememberRequests());
    CHECK(again.address() == "host:1");
    CHECK(again.launchHost());
    CHECK(again.lastRequest("CId_X") == R"({"a":1})");
    CHECK(again.lastCommand() == "CId_X");

    settings.clearLastRequests();
    CHECK_FALSE(settings.lastRequest("CId_X").has_value());
}

TEST_CASE("Commands that modify the session are confirmed") {
    Fixture fixture;
    fixture.answer = false;
    fixture.send(deleteWidget);
    CHECK(fixture.confirmations == 1);
    CHECK(fixture.sentCount(deleteWidget) == 0);
    CHECK(fixture.window->statusBar()->currentMessage().contains("was not sent"));

    fixture.answer = true;
    fixture.send(deleteWidget);
    CHECK(fixture.confirmations == 2);
    CHECK(fixture.sentCount(deleteWidget) == 1);

    fixture.send(makeWidget);
    CHECK(fixture.confirmations == 2);
    CHECK(fixture.sentCount(makeWidget) == 1);
}

TEST_CASE("Don't ask again turns confirmations off") {
    Fixture fixture;
    fixture.dontAskAgain = true;
    fixture.send(deleteWidget);
    CHECK(fixture.sentCount(deleteWidget) == 1);
    CHECK_FALSE(fixture.child<QAction>("confirmAction")->isChecked());
    CHECK_FALSE(AppSettings(*fixture.settings.settings).confirmMutating());

    fixture.send(deleteWidget);
    CHECK(fixture.confirmations == 1);
    CHECK(fixture.sentCount(deleteWidget) == 2);
}

TEST_CASE("The menu setting turns confirmations off and on") {
    Fixture fixture;
    auto* action = fixture.child<QAction>("confirmAction");
    CHECK(action->isChecked());
    action->setChecked(false);
    CHECK_FALSE(AppSettings(*fixture.settings.settings).confirmMutating());
    fixture.send(deleteWidget);
    CHECK(fixture.confirmations == 0);
    CHECK(fixture.sentCount(deleteWidget) == 1);

    action->setChecked(true);
    fixture.send(deleteWidget);
    CHECK(fixture.confirmations == 1);
}

TEST_CASE("Preferences dialog edits the settings") {
    test::TemporarySettings temporary;
    AppSettings settings(*temporary.settings);
    settings.setLastRequest("CId_X", "{}");
    PreferencesDialog dialog(settings);
    auto* confirm = dialog.findChild<QCheckBox*>("confirmMutatingCheck");
    auto* remember = dialog.findChild<QCheckBox*>("rememberRequestsCheck");
    REQUIRE(confirm != nullptr);
    REQUIRE(remember != nullptr);
    CHECK(confirm->isChecked());
    confirm->setChecked(false);
    remember->setChecked(false);
    dialog.findChild<QPushButton*>("clearRequestsButton")->click();
    CHECK_FALSE(settings.lastRequest("CId_X").has_value());
    CHECK(settings.confirmMutating());
    dialog.accept();
    CHECK_FALSE(settings.confirmMutating());
    CHECK_FALSE(settings.rememberRequests());

    PreferencesDialog cancelled(settings);
    cancelled.findChild<QCheckBox*>("confirmMutatingCheck")->setChecked(true);
    cancelled.reject();
    CHECK_FALSE(settings.confirmMutating());
}

TEST_CASE("History lists requests newest first and shows selected responses") {
    Fixture fixture;
    fixture.session.script(makeWidget, {Response{.status = ResponseStatus::Completed,
                                                 .progress = 100,
                                                 .taskId = "t",
                                                 .bodyJson = R"({"widget_id":"first"})",
                                                 .errorJson = {}}});
    fixture.send(makeWidget, R"({"name":"one"})");
    fixture.send(ping);
    auto* list = fixture.child<QTreeWidget>("historyList");
    REQUIRE(list->topLevelItemCount() == 3);
    CHECK(list->topLevelItem(0)->text(2) == "Ping");
    CHECK(list->topLevelItem(1)->text(2) == "MakeWidget");
    CHECK(list->topLevelItem(1)->text(3) == "Completed");
    CHECK(list->topLevelItem(2)->text(2) == "GetPTSLVersion");

    list->setCurrentItem(list->topLevelItem(1));
    CHECK(fixture.child<QPlainTextEdit>("responseRaw")->toPlainText().contains("first"));
    fixture.child<QPushButton>("copyResponse")->click();
    CHECK(QGuiApplication::clipboard()->text().contains("first"));

    fixture.child<QPushButton>("historyLoad")->click();
    CHECK(fixture.child<QLabel>("commandTitle")->text() == "Make Widget");
    CHECK(fixture.child<QPlainTextEdit>("requestEditor")->toPlainText().contains("\"one\""));

    fixture.child<QPushButton>("historyResend")->click();
    QTest::qWait(20);
    CHECK(fixture.sentCount(makeWidget) == 2);
    CHECK(fixture.session.sent().back().requestJson == R"({"name":"one"})");
    CHECK(list->topLevelItemCount() == 4);

    fixture.child<QPushButton>("historyClear")->click();
    CHECK(list->topLevelItemCount() == 0);
    CHECK(fixture.window->history().entries().empty());
}

TEST_CASE("Resending from history is confirmed too") {
    Fixture fixture;
    fixture.send(deleteWidget);
    REQUIRE(fixture.confirmations == 1);
    fixture.answer = false;
    fixture.child<QPushButton>("historyResend")->click();
    CHECK(fixture.confirmations == 2);
    CHECK(fixture.sentCount(deleteWidget) == 1);
}

TEST_CASE("History exports and imports") {
    Fixture fixture;
    fixture.send(makeWidget, R"({"name":"exported"})");
    const QString path = fixture.settings.directory.filePath("history.json");
    REQUIRE(fixture.window->exportHistory(path).has_value());

    Fixture other;
    REQUIRE(other.window->importHistory(path).has_value());
    const auto& entries = other.window->history().entries();
    REQUIRE(entries.size() == 2);
    CHECK(entries.back().requestJson == R"({"name":"exported"})");
    CHECK(other.child<QTreeWidget>("historyList")->topLevelItemCount() == 2);

    QFile bad(fixture.settings.directory.filePath("bad.json"));
    REQUIRE(bad.open(QIODevice::WriteOnly));
    bad.write("nope");
    bad.close();
    CHECK_FALSE(other.window->importHistory(bad.fileName()).has_value());
    CHECK(other.window->history().entries().size() == 2);
    CHECK_FALSE(other.window->importHistory(fixture.settings.directory.filePath("missing.json")).has_value());
    CHECK_FALSE(fixture.window->exportHistory(fixture.settings.directory.filePath("no/such/dir/h.json")).has_value());
}

TEST_CASE("The last request for each command is remembered") {
    Fixture fixture;
    fixture.send(makeWidget, R"({"name":"remember me"})");
    REQUIRE(fixture.window->selectCommand(ping));
    REQUIRE(fixture.window->selectCommand(makeWidget));
    CHECK(fixture.child<QPlainTextEdit>("requestEditor")->toPlainText() == R"({"name":"remember me"})");

    AppSettings(*fixture.settings.settings).setRememberRequests(false);
    REQUIRE(fixture.window->selectCommand(ping));
    REQUIRE(fixture.window->selectCommand(makeWidget));
    CHECK(fixture.child<QPlainTextEdit>("requestEditor")->toPlainText() == "{}");
}

TEST_CASE("Invalid remembered requests are ignored") {
    test::TemporarySettings temporary;
    AppSettings(*temporary.settings).setLastRequest("CId_MakeWidget", R"({"bogus": 1})");
    FakePtslSession session;
    MainWindow window(test::fixtureCatalog(), test::fixtureSchema(), session, *temporary.settings);
    REQUIRE(window.selectCommand(makeWidget));
    CHECK(window.findChild<QPlainTextEdit*>("requestEditor")->toPlainText() == "{}");
}

TEST_CASE("Connection fields and last command persist between windows") {
    test::TemporarySettings temporary;
    FakePtslSession session;
    {
        MainWindow window(test::fixtureCatalog(), test::fixtureSchema(), session, *temporary.settings);
        window.findChild<QLineEdit*>("addressEdit")->setText("example:5");
        window.findChild<QCheckBox*>("launchCheck")->setChecked(true);
        REQUIRE(window.selectCommand(ping));
        window.close();
    }
    const MainWindow restored(test::fixtureCatalog(), test::fixtureSchema(), session, *temporary.settings);
    CHECK(restored.findChild<QLineEdit*>("addressEdit")->text() == "example:5");
    CHECK(restored.findChild<QCheckBox*>("launchCheck")->isChecked());
    CHECK(restored.findChild<QLabel*>("commandTitle")->text() == "Ping");
}

TEST_CASE("History panel helpers") {
    CHECK(outcomeLabel(Outcome::Pending) == "Pending");
    CHECK(outcomeLabel(Outcome::Cancelled) == "Cancelled");
    HistoryPanel panel;
    History history;
    const auto first = history.add(1, "CId_A", "", std::chrono::system_clock::now());
    history.add(2, "CId_B", "", std::chrono::system_clock::now());
    panel.setEntries(history);
    CHECK(panel.count() == 2);
    CHECK_FALSE(panel.selectedSequence().has_value());
    panel.select(first);
    CHECK(panel.selectedSequence() == first);
    history.apply(
        first,
        Response{.status = ResponseStatus::InProgress, .progress = 40, .taskId = {}, .bodyJson = {}, .errorJson = {}},
        std::chrono::system_clock::now());
    panel.updateEntry(*history.find(first));
    CHECK(panel.count() == 2);
    CHECK(panel.findChild<QTreeWidget*>("historyList")->topLevelItem(1)->text(3) == "In progress 40%");
}
