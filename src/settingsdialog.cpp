#include "settingsdialog.h"
#include "gameoptionswidget.h"
#include "dialogbuttons.h"

#include <QCheckBox>
#include <QComboBox>
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
    auto *iconOption = new QWidget(this);
    auto *iconRow = new QHBoxLayout(iconOption);
    iconRow->setContentsMargins(0, 0, 0, 0);
    iconRow->addWidget(new QLabel("Default icon source", this));
    m_iconSource = new QComboBox(this);
    m_iconSource->setObjectName("defaultIconSource");
    m_iconSource->addItem("Extracted icon", "extracted");
    m_iconSource->addItem("SteamGridDB", "steamgriddb");
    m_iconSource->setCurrentIndex(m_original.value("default_icon_source").toString("extracted") == "steamgriddb" ? 1 : 0);
    m_iconSource->setToolTip("Applies only to new games. SteamGridDB automatically uses the first available icon and requires an API key. Existing icons stay unchanged.");
    iconRow->addWidget(m_iconSource, 1);
    m_defaults->addGeneralOption(iconOption, true);
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
    settings.insert("default_icon_source", m_iconSource->currentData().toString());
    auto options = m_defaults->optionsData();
    options.insert("proton", "default");
    settings.insert("new_game_defaults", options);
    return settings;
}
