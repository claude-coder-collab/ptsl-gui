#include "preferences_dialog.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace ptslgui::ui {

PreferencesDialog::PreferencesDialog(AppSettings& settings, QWidget* parent)
    : QDialog(parent)
    , settings_(settings)
    , confirm_(new QCheckBox(tr("Ask before sending commands that modify the session"), this))
    , remember_(new QCheckBox(tr("Remember the last request sent for each command"), this)) {
    setWindowTitle(tr("Preferences"));
    setObjectName("preferencesDialog");

    confirm_->setObjectName("confirmMutatingCheck");
    confirm_->setChecked(settings_.confirmMutating());
    remember_->setObjectName("rememberRequestsCheck");
    remember_->setChecked(settings_.rememberRequests());

    auto* note = new QLabel(tr("Commands in the session file, session write, editing and export categories "
                               "modify the session. When this is off, no confirmation is shown for any command."),
                            this);
    note->setWordWrap(true);
    note->setEnabled(false);

    auto* clear = new QPushButton(tr("Forget saved requests"), this);
    clear->setObjectName("clearRequestsButton");
    connect(clear, &QPushButton::clicked, this, [this, clear] {
        settings_.clearLastRequests();
        clear->setEnabled(false);
    });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(this, &QDialog::accepted, this, &PreferencesDialog::apply);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(confirm_);
    layout->addWidget(note);
    layout->addSpacing(8);
    layout->addWidget(remember_);
    layout->addWidget(clear, 0, Qt::AlignLeft);
    layout->addStretch(1);
    layout->addWidget(buttons);
}

void PreferencesDialog::apply() {
    settings_.setConfirmMutating(confirm_->isChecked());
    settings_.setRememberRequests(remember_->isChecked());
}

} // namespace ptslgui::ui
