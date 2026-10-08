#include <ptslgui/session.hpp>
#include <ptslgui/ui/app_settings.hpp>

#include <QSettings>

namespace ptslgui::ui {
namespace {

const char* const confirmKey = "safety/confirmMutating";
const char* const rememberKey = "requests/remember";
const char* const addressKey = "connection/address";
const char* const launchKey = "connection/launchHost";
const char* const requestsGroup = "lastRequests";
const char* const lastCommandKey = "ui/lastCommand";
const char* const geometryKey = "window/geometry";
const char* const stateKey = "window/state";
const char* const splitterKey = "window/splitter";

} // namespace

bool AppSettings::confirmMutating() const {
    return settings_->value(confirmKey, true).toBool();
}

void AppSettings::setConfirmMutating(bool enabled) {
    settings_->setValue(confirmKey, enabled);
}

bool AppSettings::rememberRequests() const {
    return settings_->value(rememberKey, true).toBool();
}

void AppSettings::setRememberRequests(bool enabled) {
    settings_->setValue(rememberKey, enabled);
}

QString AppSettings::address() const {
    return settings_->value(addressKey, QString::fromStdString(ConnectionSettings{}.address)).toString();
}

void AppSettings::setAddress(const QString& address) {
    settings_->setValue(addressKey, address);
}

bool AppSettings::launchHost() const {
    return settings_->value(launchKey, false).toBool();
}

void AppSettings::setLaunchHost(bool launch) {
    settings_->setValue(launchKey, launch);
}

std::optional<std::string> AppSettings::lastRequest(const std::string& commandName) const {
    settings_->beginGroup(requestsGroup);
    const QVariant value = settings_->value(QString::fromStdString(commandName));
    settings_->endGroup();
    if (!value.isValid()) {
        return std::nullopt;
    }
    return value.toString().toStdString();
}

void AppSettings::setLastRequest(const std::string& commandName, const std::string& requestJson) {
    settings_->beginGroup(requestsGroup);
    settings_->setValue(QString::fromStdString(commandName), QString::fromStdString(requestJson));
    settings_->endGroup();
}

void AppSettings::clearLastRequests() {
    settings_->remove(requestsGroup);
}

QString AppSettings::lastCommand() const {
    return settings_->value(lastCommandKey).toString();
}

void AppSettings::setLastCommand(const QString& commandName) {
    settings_->setValue(lastCommandKey, commandName);
}

QByteArray AppSettings::windowGeometry() const {
    return settings_->value(geometryKey).toByteArray();
}

QByteArray AppSettings::windowState() const {
    return settings_->value(stateKey).toByteArray();
}

QByteArray AppSettings::splitterState() const {
    return settings_->value(splitterKey).toByteArray();
}

void AppSettings::setWindowLayout(const QByteArray& geometry, const QByteArray& state, const QByteArray& splitter) {
    settings_->setValue(geometryKey, geometry);
    settings_->setValue(stateKey, state);
    settings_->setValue(splitterKey, splitter);
}

void AppSettings::sync() {
    settings_->sync();
}

} // namespace ptslgui::ui
