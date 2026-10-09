#include "settingsdialog.h"
#include "gameoptionswidget.h"
#include "dialogbuttons.h"
#include "appearance.h"
#include "backendclient.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QEvent>
#include <QMessageBox>
#include <QPointer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <deque>
#include <functional>

namespace {
struct ThemeChange {
    QString backend;
    QString dataRoot;
    QString style;
    std::function<void(const QString &)> finished;
};
std::deque<ThemeChange> themeChanges;

void saveNextTheme() {
    const auto change = themeChanges.front();
    // App-owned requests survive Settings closing. Serialize them so rapid
    // selections, including reopening Settings, cannot save out of order.
    auto *client = new BackendClient(change.backend, change.dataRoot, qApp);
    const auto finish = [client, change](const QString &error) {
        client->deleteLater();
        themeChanges.pop_front();
        if (!themeChanges.empty()) saveNextTheme();
        change.finished(error);
    };
    client->request("save_settings", {{"settings", QJsonObject{{"widget_style", change.style}}}},
        [finish](const QJsonObject &data) {
            // The backend atomically saves and fsyncs first. Even if switching
            // the native style crashes, the chosen theme is already on disk.
            ++Appearance::revision();
            Appearance::apply(data.value("settings").toObject().value("widget_style").toString("default"));
            finish({});
        }, finish);
}
}

SettingsDialog::SettingsDialog(const QJsonObject &bootstrap, QWidget *parent)
    : QDialog(parent), m_original(bootstrap.value("settings").toObject()) {
    setWindowTitle("Settings — General");
    setMinimumWidth(960);
    auto *layout = new QVBoxLayout(this);
    auto *appearance = new QGroupBox("Appearance", this);
    auto *appearanceLayout = new QVBoxLayout(appearance);
    auto *styleRow = new QHBoxLayout;
    auto *styleLabel = new QLabel("Theme", appearance);
    styleRow->addWidget(styleLabel);
    m_widgetStyle = new QComboBox(appearance);
    m_widgetStyle->setObjectName("widgetStyle");
    styleLabel->setBuddy(m_widgetStyle);
    for (const auto &name : {QString("breeze"), QString("fusion"), QString("windows")}) {
        if (!Appearance::installedStyle(name).isEmpty())
            m_widgetStyle->addItem(Appearance::displayName(name), name);
    }
    // The active style includes changes saved independently since bootstrap.
    const auto selected = Appearance::resolvedStyle(QApplication::style()->objectName()).toLower();
    m_widgetStyle->setCurrentIndex(m_widgetStyle->findData(selected));
    m_widgetStyle->setToolTip("Native Qt widget rendering. Colors and icons continue to follow your desktop.");
    styleRow->addWidget(m_widgetStyle, 1);
    appearanceLayout->addLayout(styleRow);
    auto *styleNote = new QLabel("Theme changes apply and save immediately, independently of the other settings.", appearance);
    styleNote->setWordWrap(true);
    appearanceLayout->addWidget(styleNote);
    layout->addWidget(appearance);
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
    const auto frontend = bootstrap.value("frontend").toObject();
    connect(m_widgetStyle, &QComboBox::activated, this, [this, frontend](int index) {
        if (index < 0 || m_styleSaving) return;
        const auto style = m_widgetStyle->currentData().toString();
        m_styleSaving = true;
        m_widgetStyle->setEnabled(false);
        const QPointer<SettingsDialog> dialog(this);
        const bool idle = themeChanges.empty();
        themeChanges.push_back({frontend.value("backend").toString(), frontend.value("data_root").toString(), style,
            [dialog](const QString &error) {
                if (dialog) {
                    dialog->m_styleSaving = false;
                    dialog->m_widgetStyle->setEnabled(true);
                    dialog->m_widgetStyle->setCurrentIndex(dialog->m_widgetStyle->findData(
                        Appearance::resolvedStyle(QApplication::style()->objectName()).toLower()));
                }
                if (!error.isEmpty())
                    QMessageBox::warning(dialog.data(), "Theme could not be saved", error);
            }});
        if (idle) saveNextTheme();
    });
}

void SettingsDialog::changeEvent(QEvent *event) {
    QDialog::changeEvent(event);
    if (event->type() == QEvent::StyleChange && m_widgetStyle && !m_styleSaving)
        m_widgetStyle->setCurrentIndex(m_widgetStyle->findData(
            Appearance::resolvedStyle(QApplication::style()->objectName()).toLower()));
}

QJsonObject SettingsDialog::settingsData() const {
    auto settings = m_original;
    settings.insert("default_proton", m_defaults->protonSelection());
    settings.remove("umu_program");
    settings.insert("close_after_launch", m_closeAfter->isChecked());
    settings.insert("steamgriddb_api_key", m_apiKey->text().trimmed());
    settings.insert("default_icon_source", m_iconSource->currentData().toString());
    // Never overwrite an independently saved theme with this settings draft.
    settings.remove("widget_style");
    auto options = m_defaults->optionsData();
    options.insert("proton", "default");
    settings.insert("new_game_defaults", options);
    const auto prefixes = m_defaults->prefixSettings();
    for (auto it = prefixes.begin(); it != prefixes.end(); ++it) settings.insert(it.key(), it.value());
    return settings;
}
