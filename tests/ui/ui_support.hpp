#pragma once

#include <QSettings>
#include <QTemporaryDir>

#include <memory>

namespace ptslgui::test {

/// QSettings backed by an INI file in a temporary directory, so tests never touch user settings.
struct TemporarySettings {
    QTemporaryDir directory;
    std::unique_ptr<QSettings> settings =
        std::make_unique<QSettings>(directory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);

    [[nodiscard]] QString path() const { return directory.filePath(QStringLiteral("settings.ini")); }
};

} // namespace ptslgui::test
