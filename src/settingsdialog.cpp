#include "settingsdialog.h"
#include "gameoptionswidget.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QTabWidget>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(const QJsonObject &bootstrap, QWidget *parent)
    : QDialog(parent), m_original(bootstrap.value("settings").toObject()) {
    setWindowTitle("Settings");
    setMinimumWidth(620);
    auto *layout = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    tabs->setObjectName("settingsTabs");
    auto *general = new QWidget(tabs);
    auto *generalLayout = new QVBoxLayout(general);
    auto *form = new QFormLayout;
    const auto umu = bootstrap.value("umu").toObject();
    auto *umuStatus = new QLabel(this);
    umuStatus->setObjectName("umuStatus");
    umuStatus->setTextFormat(Qt::PlainText);
    umuStatus->setWordWrap(true);
    const auto version = umu.value("version").toString();
    umuStatus->setText(version.isEmpty() ? "Managed automatically"
        : "UMU " + version + " — managed automatically");
    QString details = umu.value("path").toString();
    if (!umu.value("last_error").toString().isEmpty()) details += "\n" + umu.value("last_error").toString();
    umuStatus->setToolTip(details);
    form->addRow("UMU launcher", umuStatus);
    m_closeAfter = new QCheckBox("Close Forest after launching a game", this);
    m_closeAfter->setChecked(m_original.value("close_after_launch").toBool());
    form->addRow(QString(), m_closeAfter);
    generalLayout->addLayout(form);
    generalLayout->addStretch();
    tabs->addTab(general, "General");
    auto *defaults = new QWidget(tabs);
    auto *defaultsLayout = new QVBoxLayout(defaults);
    auto *hint = new QLabel("These options prefill newly added games. Existing game profiles are not rewritten.\nReview or override them in the Add game dialog.", defaults);
    hint->setWordWrap(true);
    defaultsLayout->addWidget(hint);
    m_defaults = new GameOptionsWidget(m_original.value("new_game_defaults").toObject(), bootstrap, defaults, true);
    defaultsLayout->addWidget(m_defaults);
    tabs->addTab(defaults, "New game defaults");
    layout->addWidget(tabs);
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
