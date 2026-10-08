#pragma once

#include <QByteArray>
#include <QString>

#include <optional>
#include <string>

class QSettings;

namespace ptslgui::ui {

/// Typed access to the persisted user settings.
class AppSettings {
public:
    explicit AppSettings(QSettings& settings) : settings_(&settings) {}

    /// Ask before sending commands that modify the session (default true). When false, nothing is confirmed.
    [[nodiscard]] bool confirmMutating() const;
    void setConfirmMutating(bool enabled);

    /// Restore the last request sent for each command when it is selected again (default true).
    [[nodiscard]] bool rememberRequests() const;
    void setRememberRequests(bool enabled);

    [[nodiscard]] QString address() const;
    void setAddress(const QString& address);
    [[nodiscard]] bool launchHost() const;
    void setLaunchHost(bool launch);

    [[nodiscard]] std::optional<std::string> lastRequest(const std::string& commandName) const;
    void setLastRequest(const std::string& commandName, const std::string& requestJson);
    void clearLastRequests();

    [[nodiscard]] QString lastCommand() const;
    void setLastCommand(const QString& commandName);

    [[nodiscard]] QByteArray windowGeometry() const;
    [[nodiscard]] QByteArray windowState() const;
    [[nodiscard]] QByteArray splitterState() const;
    void setWindowLayout(const QByteArray& geometry, const QByteArray& state, const QByteArray& splitter);

    void sync();

private:
    QSettings* settings_;
};

} // namespace ptslgui::ui
