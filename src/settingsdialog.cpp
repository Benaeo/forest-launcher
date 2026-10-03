#include "settingsdialog.h"
#include "gameoptionswidget.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(const QJsonObject &bootstrap, QWidget *parent)
    : QDialog(parent), m_original(bootstrap.value("settings").toObject()) {
    setWindowTitle("Settings — General");
    setMinimumWidth(960);
    auto *layout = new QVBoxLayout(this);
    auto *heading = new QLabel("General", this);
    heading->setObjectName("settingsGeneralHeading");
    auto font = heading->font();
    font.setBold(true);
    heading->setFont(font);
    layout->addWidget(heading);
    m_defaults = new GameOptionsWidget(m_original.value("new_game_defaults").toObject(), bootstrap, this, true);
    m_closeAfter = new QCheckBox("Close Forest after launch", this);
    m_closeAfter->setObjectName("closeAfterLaunchCheck");
    m_closeAfter->setToolTip("Applies to all games. Close the launcher after a successful launch; the game continues independently.");
    m_closeAfter->setChecked(m_original.value("close_after_launch").toBool());
    m_defaults->addGeneralOption(m_closeAfter);
    layout->addWidget(m_defaults);
    auto *hint = new QLabel("Game options prefill newly added games; existing profiles are unchanged. Auto-close applies to all games.", this);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QJsonObject SettingsDialog::settingsData() const {
    auto settings = m_original;
    settings.insert("default_proton", m_defaults->protonSelection());
    settings.remove("umu_program");
    settings.insert("close_after_launch", m_closeAfter->isChecked());
    auto options = m_defaults->optionsData();
    options.insert("proton", "default");
    settings.insert("new_game_defaults", options);
    return settings;
}
