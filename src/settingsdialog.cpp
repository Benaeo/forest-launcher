#include "settingsdialog.h"
#include "gameoptionswidget.h"
#include "dialogbuttons.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

SettingsDialog::SettingsDialog(const QJsonObject &bootstrap, QWidget *parent)
    : QDialog(parent), m_original(bootstrap.value("settings").toObject()) {
    setWindowTitle("Settings — General");
    setMinimumWidth(960);
    auto *layout = new QVBoxLayout(this);
    m_defaults = new GameOptionsWidget(m_original.value("new_game_defaults").toObject(), bootstrap, this, true);
    m_closeAfter = new QCheckBox("Close Forest after launch", this);
    m_closeAfter->setObjectName("closeAfterLaunchCheck");
    m_closeAfter->setToolTip("Applies to all games. Close the launcher after a successful launch; the game continues independently.");
    m_closeAfter->setChecked(m_original.value("close_after_launch").toBool());
    m_defaults->addGeneralOption(m_closeAfter);
    layout->addWidget(m_defaults);
    auto *keyRow = new QHBoxLayout;
    keyRow->addWidget(new QLabel("SteamGridDB API key", this));
    m_apiKey = new QLineEdit(m_original.value("steamgriddb_api_key").toString(), this);
    m_apiKey->setObjectName("steamGridDbApiKey");
    m_apiKey->setEchoMode(QLineEdit::Password);
    m_apiKey->setToolTip("Saved in private application settings, not encrypted. Sent only to the SteamGridDB API.");
    keyRow->addWidget(m_apiKey, 1);
    auto *showKey = new QCheckBox("Show", this);
    keyRow->addWidget(showKey);
    auto *clearKey = new QPushButton("Clear", this);
    keyRow->addWidget(clearKey);
    connect(showKey, &QCheckBox::toggled, this, [this](bool visible) { m_apiKey->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password); });
    connect(clearKey, &QPushButton::clicked, m_apiKey, &QLineEdit::clear);
    layout->addLayout(keyRow);
    auto *buttons = new WideDialogButtons(this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QJsonObject SettingsDialog::settingsData() const {
    auto settings = m_original;
    settings.insert("default_proton", m_defaults->protonSelection());
    settings.remove("umu_program");
    settings.insert("close_after_launch", m_closeAfter->isChecked());
    settings.insert("steamgriddb_api_key", m_apiKey->text().trimmed());
    auto options = m_defaults->optionsData();
    options.insert("proton", "default");
    settings.insert("new_game_defaults", options);
    return settings;
}
