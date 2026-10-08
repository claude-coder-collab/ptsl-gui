#include <ptslgui/catalog.hpp>
#include <ptslgui/fake_session.hpp>
#include <ptslgui/protocol.hpp>
#include <ptslgui/schema.hpp>
#include <ptslgui/sdk_session.hpp>
#include <ptslgui/ui/main_window.hpp>

#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QMessageBox>
#include <QSettings>

#include <cstdio>
#include <memory>
#include <print>

namespace {

std::expected<std::string, QString> readResource(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::unexpected(QObject::tr("Cannot read %1").arg(path));
    }
    return file.readAll().toStdString();
}

std::unique_ptr<ptslgui::IPtslSession> makeDemoSession(const ptslgui::CommandCatalog& catalog) {
    auto session = std::make_unique<ptslgui::FakePtslSession>();
    session->setAutoDeliver(true);
    if (const auto* version = catalog.findByName(ptslgui::protocol::getPtslVersion)) {
        session->script(version->id, {ptslgui::Response{.status = ptslgui::ResponseStatus::Completed,
                                                        .progress = 100,
                                                        .taskId = "demo",
                                                        .bodyJson = R"({"version":2026,"version_minor":4})",
                                                        .errorJson = {}}});
    }
    return session;
}

int fail(const QString& message) {
    QMessageBox::critical(nullptr, QObject::tr("PTSL GUI"), message);
    return 1;
}

int run(int argc, char** argv) {
    const QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("PTSL GUI"));
    QApplication::setOrganizationName(QStringLiteral("PTSL GUI"));

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption demo(QStringLiteral("demo"), QObject::tr("Use a simulated Pro Tools connection."));
    parser.addOption(demo);
    parser.process(application);

    const auto protoText = readResource(QStringLiteral(":/ptsl/PTSL.proto"));
    const auto catalogText = readResource(QStringLiteral(":/ptsl/catalog.json"));
    if (!protoText || !catalogText) {
        return fail(protoText ? catalogText.error() : protoText.error());
    }
    auto schema = ptslgui::ProtoSchema::fromProtoText(*protoText);
    if (!schema) {
        return fail(QString::fromStdString(schema.error()));
    }
    auto catalog = ptslgui::CommandCatalog::fromJson(*catalogText);
    if (!catalog) {
        return fail(QString::fromStdString(catalog.error()));
    }

    std::unique_ptr<ptslgui::IPtslSession> session;
    if (parser.isSet(demo)) {
        session = makeDemoSession(*catalog);
    } else {
        session = std::make_unique<ptslgui::SdkPtslSession>();
    }

    QSettings settings;
    ptslgui::ui::MainWindow window(*catalog, *schema, *session, settings);
    if (parser.isSet(demo)) {
        window.setWindowTitle(QObject::tr("PTSL GUI (demo)"));
    }
    window.show();
    return QApplication::exec();
}

} // namespace

// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::println(stderr, "PTSL GUI: {}", error.what());
        return 1;
    }
}
