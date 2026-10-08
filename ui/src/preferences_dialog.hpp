#pragma once

#include <ptslgui/ui/app_settings.hpp>

#include <QDialog>

class QCheckBox;

namespace ptslgui::ui {

/// Edits the user settings; changes are written when the dialog is accepted.
class PreferencesDialog final : public QDialog {
    Q_OBJECT

public:
    PreferencesDialog(AppSettings& settings, QWidget* parent = nullptr);

    void apply();

private:
    AppSettings& settings_;
    QCheckBox* confirm_ = nullptr;
    QCheckBox* remember_ = nullptr;
};

} // namespace ptslgui::ui
