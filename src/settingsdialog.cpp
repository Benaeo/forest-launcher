#include "settingsdialog.h"
#include "gameoptionswidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
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
    m_prefixRoot = new QLineEdit(m_original.value("prefix_root").toString(), this);
    m_prefixRoot->setObjectName("prefixRoot");
    m_prefixRoot->setToolTip("Storage for automatically allocated per-game prefixes. A fixed new-game prefix can be set in New game defaults.");
    auto *prefixBrowse = new QPushButton("Browse…", this);
    auto *prefixRow = new QHBoxLayout;
    prefixRow->addWidget(m_prefixRoot, 1);
    prefixRow->addWidget(prefixBrowse);
    form->addRow("Automatic prefix storage", prefixRow);
    m_umu = new QLineEdit(m_original.value("umu_program").toString(), this);
    m_umu->setObjectName("umuProgram");
    m_umu->setPlaceholderText("Automatic: find installed umu-run");
    auto *umuBrowse = new QPushButton("Browse…", this);
    auto *umuRow = new QHBoxLayout;
    umuRow->addWidget(m_umu, 1);
    umuRow->addWidget(umuBrowse);
    form->addRow("UMU executable", umuRow);
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
    connect(prefixBrowse, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getExistingDirectory(this, "Choose automatic prefix storage", m_prefixRoot->text());
        if (!path.isEmpty()) m_prefixRoot->setText(path);
    });
    connect(umuBrowse, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, "Choose umu-run executable", m_umu->text());
        if (!path.isEmpty()) m_umu->setText(path);
    });
}

QJsonObject SettingsDialog::settingsData() const {
    auto settings = m_original;
    settings.insert("prefix_root", m_prefixRoot->text());
    settings.insert("default_proton", m_defaults->protonSelection());
    settings.insert("umu_program", m_umu->text());
    settings.insert("close_after_launch", m_closeAfter->isChecked());
    auto options = m_defaults->optionsData();
    options.insert("proton", "default");
    settings.insert("new_game_defaults", options);
    return settings;
}
