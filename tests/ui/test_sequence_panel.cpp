#include "../core/test_support.hpp"
#include "sequence_panel.hpp"
#include "ui_support.hpp"

#include <ptslgui/fake_session.hpp>
#include <ptslgui/ui/app_settings.hpp>
#include <ptslgui/ui/main_window.hpp>

#include <QAction>
#include <QDockWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTest>
#include <QTreeWidget>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <memory>

using namespace ptslgui;
using namespace ptslgui::ui;

namespace {

constexpr int makeWidget = 0;
constexpr int listWidgets = 1;
constexpr int ping = 2;
constexpr int deleteWidget = 3;
constexpr int waitMs = 5000;

Response completed(std::string body) {
    return Response{.status = ResponseStatus::Completed,
                    .progress = 100,
                    .taskId = {},
                    .bodyJson = std::move(body),
                    .errorJson = {}};
}

struct Fixture {
    test::TemporarySettings settings;
    FakePtslSession session;
    std::unique_ptr<MainWindow> window =
        std::make_unique<MainWindow>(test::fixtureCatalog(), test::fixtureSchema(), session, *settings.settings);
    QStringList confirmedNames;
    int confirmations = 0;
    bool answer = true;
    bool dontAskAgain = false;

    Fixture() {
        session.setAutoDeliver(true);
        window->setSequenceConfirmHandler([this](const QStringList& names, bool& dontAsk) {
            ++confirmations;
            confirmedNames = names;
            dontAsk = dontAskAgain;
            return answer;
        });
    }

    void connect() {
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

    SequencePanel& panel() { return *child<SequencePanel>("sequencePanel"); }

    void addStep(int commandId, const QString& json = {}) {
        REQUIRE(window->selectCommand(commandId));
        if (!json.isNull()) {
            child<QPlainTextEdit>("requestEditor")->setPlainText(json);
        }
        child<QPushButton>("sequenceAdd")->click();
    }

    QString status(int row) { return child<QTreeWidget>("sequenceSteps")->topLevelItem(row)->text(4); }

    void runAndWait() const {
        QSignalSpy finished(window.get(), &MainWindow::sequenceFinished);
        REQUIRE(window->runSequence());
        REQUIRE(finished.wait(waitMs));
    }
};

} // namespace

TEST_CASE("Sequence dock is tabbed with history and listed in the View menu") {
    Fixture fixture;
    auto* dock = fixture.child<QDockWidget>("sequenceDock");
    CHECK(fixture.window->tabifiedDockWidgets(dock).contains(fixture.child<QDockWidget>("historyDock")));
    CHECK_FALSE(fixture.child<QPushButton>("sequenceRun")->isEnabled());
}

TEST_CASE("Steps are added from the editor with unique labels and can be edited") {
    Fixture fixture;
    fixture.addStep(listWidgets, R"({"limit": 2})");
    fixture.addStep(listWidgets, R"({"limit": 3})");
    fixture.addStep(ping);
    const auto& steps = fixture.panel().sequence().steps;
    REQUIRE(steps.size() == 3);
    CHECK(steps[0].label == "ListWidgets");
    CHECK(steps[1].label == "ListWidgets_2");
    CHECK(steps[1].requestTemplate == R"({"limit": 3})");
    CHECK(steps[2].commandName == "CId_Ping");
    CHECK(steps[2].requestTemplate.empty());

    const auto* tree = fixture.child<QTreeWidget>("sequenceSteps");
    REQUIRE(tree->topLevelItemCount() == 3);
    CHECK(tree->topLevelItem(0)->text(2) == "List Widgets");

    tree->topLevelItem(0)->setCheckState(0, Qt::Unchecked);
    tree->topLevelItem(1)->setCheckState(3, Qt::Checked);
    CHECK_FALSE(fixture.panel().sequence().steps[0].enabled);
    CHECK(fixture.panel().sequence().steps[1].continueOnError);

    tree->topLevelItem(2)->setText(1, "pinger");
    CHECK(fixture.panel().sequence().steps[2].label == "pinger");
    tree->topLevelItem(2)->setText(1, "ListWidgets");
    CHECK(fixture.panel().sequence().steps[2].label == "pinger");
    CHECK(tree->topLevelItem(2)->text(1) == "pinger");
    tree->topLevelItem(2)->setText(1, "bad label");
    CHECK(fixture.panel().sequence().steps[2].label == "pinger");

    fixture.panel().selectStep(2);
    fixture.child<QPushButton>("sequenceUp")->click();
    CHECK(fixture.panel().sequence().steps[1].label == "pinger");
    CHECK(fixture.panel().selectedStep() == 1);
    fixture.child<QPushButton>("sequenceDown")->click();
    CHECK(fixture.panel().sequence().steps[2].label == "pinger");

    REQUIRE(fixture.window->selectCommand(makeWidget));
    fixture.child<QPlainTextEdit>("requestEditor")->setPlainText(R"({"name": "x"})");
    fixture.child<QPushButton>("sequenceUpdate")->click();
    CHECK(fixture.panel().sequence().steps[2].commandName == "CId_MakeWidget");
    CHECK(fixture.panel().sequence().steps[2].requestTemplate == R"({"name": "x"})");

    fixture.child<QPushButton>("sequenceRemove")->click();
    CHECK(fixture.panel().sequence().steps.size() == 2);
}

TEST_CASE("Variables are edited in the table") {
    Fixture fixture;
    fixture.child<QPushButton>("variableAdd")->click();
    auto* table = fixture.child<QTableWidget>("sequenceVariables");
    REQUIRE(table->rowCount() == 1);
    CHECK(fixture.panel().sequence().variables[0].first == "var1");
    table->item(0, 0)->setText("size");
    table->item(0, 1)->setText("7");
    CHECK(fixture.panel().sequence().variables == std::vector<std::pair<std::string, std::string>>{{"size", "7"}});
    table->setCurrentCell(0, 0);
    fixture.child<QPushButton>("variableRemove")->click();
    CHECK(fixture.panel().sequence().variables.empty());
}

TEST_CASE("Loading a step puts its template in the editor and placeholders are not errors") {
    Fixture fixture;
    fixture.addStep(makeWidget, R"({"name": "{{list.widgets[0].name}}"})");
    REQUIRE(fixture.window->selectCommand(ping));
    fixture.panel().selectStep(0);
    fixture.child<QPushButton>("sequenceLoad")->click();
    CHECK(fixture.child<QPlainTextEdit>("requestEditor")->toPlainText() == R"({"name": "{{list.widgets[0].name}}"})");
    CHECK(fixture.child<QLabel>("validationLabel")->text().contains("placeholders"));
}

TEST_CASE("Running a sequence substitutes responses and reports each step") {
    Fixture fixture;
    fixture.session.script(listWidgets, {completed(R"({"widgets":[{"name":"first"}]})")});
    fixture.addStep(listWidgets, R"({"limit": 1})");
    fixture.addStep(makeWidget, R"({"name": "copy of {{ListWidgets.widgets[0].name}}"})");
    CHECK_FALSE(fixture.window->runSequence());
    fixture.connect();
    CHECK(fixture.child<QPushButton>("sequenceRun")->isEnabled());

    fixture.runAndWait();
    CHECK_FALSE(fixture.window->sequenceRunning());
    CHECK(fixture.confirmations == 0);
    const auto sent = fixture.session.sent();
    REQUIRE(sent.size() >= 3);
    CHECK(sent.back().commandId == makeWidget);
    CHECK(nlohmann::json::parse(sent.back().requestJson)["name"] == "copy of first");
    CHECK(fixture.status(0) == "Completed");
    CHECK(fixture.status(1) == "Completed");
    CHECK(fixture.child<QTreeWidget>("historyList")->topLevelItemCount() == 3);
    CHECK(fixture.child<QLabel>("responseMeta")->text().contains("CId_MakeWidget"));
}

TEST_CASE("A failing step stops the sequence and shows why") {
    Fixture fixture;
    fixture.connect();
    fixture.addStep(makeWidget, R"({"name": "{{nothing.here}}"})");
    fixture.addStep(ping);
    fixture.runAndWait();
    CHECK(fixture.status(0).startsWith("Failed: {{nothing.here}}"));
    CHECK(fixture.status(1).isEmpty());
}

TEST_CASE("Sequences that modify the session are confirmed once") {
    Fixture fixture;
    fixture.connect();
    fixture.addStep(deleteWidget, R"({"names": ["a"]})");
    fixture.addStep(deleteWidget, R"({"names": ["b"]})");
    fixture.addStep(ping);

    fixture.answer = false;
    CHECK_FALSE(fixture.window->runSequence());
    CHECK(fixture.confirmations == 1);
    CHECK(fixture.confirmedNames == QStringList{"Delete Widget"});

    fixture.answer = true;
    fixture.dontAskAgain = true;
    fixture.runAndWait();
    CHECK(fixture.confirmations == 2);
    CHECK_FALSE(AppSettings(*fixture.settings.settings).confirmMutating());
    CHECK_FALSE(fixture.child<QAction>("confirmAction")->isChecked());
    fixture.runAndWait();
    CHECK(fixture.confirmations == 2);
}

TEST_CASE("Sequences are saved, opened and restored") {
    Fixture fixture;
    QTest::keyClicks(fixture.child<QLineEdit>("sequenceName"), "Setup");
    fixture.addStep(ping);
    const QString path = fixture.settings.directory.filePath("sequence.json");
    REQUIRE(fixture.window->saveSequence(path).has_value());

    fixture.child<QPushButton>("sequenceNew")->click();
    CHECK(fixture.panel().sequence().steps.empty());
    REQUIRE(fixture.window->openSequence(path).has_value());
    CHECK(fixture.panel().sequence().name == "Setup");
    CHECK(fixture.panel().sequence().steps.size() == 1);
    CHECK(fixture.child<QLineEdit>("sequenceName")->text() == "Setup");

    const QString bad = fixture.settings.directory.filePath("bad.json");
    {
        QFile file(bad);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write("{\"version\": 9}");
    }
    CHECK_FALSE(fixture.window->openSequence(bad).has_value());
    CHECK_FALSE(fixture.window->openSequence(fixture.settings.directory.filePath("missing.json")).has_value());
    CHECK_FALSE(fixture.window->saveSequence(fixture.settings.directory.filePath("no/such/dir/x.json")).has_value());
    CHECK(fixture.panel().sequence().steps.size() == 1);

    fixture.window.reset();
    const MainWindow restored(test::fixtureCatalog(), test::fixtureSchema(), fixture.session,
                              *fixture.settings.settings);
    const auto* panel = restored.findChild<SequencePanel*>("sequencePanel");
    REQUIRE(panel != nullptr);
    CHECK(panel->sequence().steps.size() == 1);
    CHECK(panel->sequence().name == "Setup");
}

TEST_CASE("Step status labels") {
    CHECK(stepStatusLabel(StepStatus::Waiting).isEmpty());
    CHECK(stepStatusLabel(StepStatus::Skipped) == "Skipped");
    CHECK(stepStatusLabel(StepStatus::Running) == "Running…");
    CHECK(stepStatusLabel(StepStatus::Cancelled) == "Cancelled");
}
