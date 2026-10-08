#include <QApplication>
#include <catch2/catch_session.hpp>

int main(int argc, char* argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    const QApplication application(argc, argv);
    return Catch::Session().run(argc, argv);
}
