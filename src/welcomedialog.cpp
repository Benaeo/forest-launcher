#include "welcomedialog.h"
#include "settingsdialog.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

WelcomeDialog::WelcomeDialog(const QJsonObject &bootstrap, QWidget *parent)
    : QDialog(parent), m_draft(bootstrap.value("settings").toObject()) {
    setWindowTitle("Welcome to Forest Launcher");
    setObjectName("firstLaunchWelcome");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(14);
    auto *heading = new QLabel("First-time launch", this);
    heading->setObjectName("welcomeHeading");
    heading->setAlignment(Qt::AlignCenter);
    auto font = heading->font();
    font.setBold(true);
    font.setPointSize(font.pointSize() + 3);
    heading->setFont(font);
    layout->addWidget(heading);

    auto *artwork = new QGroupBox("SteamGridDB", this);
    auto *artworkLayout = new QVBoxLayout(artwork);
    auto *description = new QLabel("Add an API key to get artwork for your games.", artwork);
    description->setWordWrap(true);
    artworkLayout->addWidget(description);
    auto *keyRow = new QHBoxLayout;
    keyRow->addWidget(new QLabel("API key", artwork));
    m_key = new QLineEdit(m_draft.value("steamgriddb_api_key").toString(), artwork);
    m_key->setObjectName("welcomeSteamGridDbApiKey");
    m_key->setEchoMode(QLineEdit::Password);
    m_key->setPlaceholderText("Paste your SteamGridDB API key");
    m_key->setToolTip("Saved in private application settings, not encrypted. Sent only to the SteamGridDB API.");
    keyRow->addWidget(m_key, 1);
    artworkLayout->addLayout(keyRow);
    auto *keyActions = new QHBoxLayout;
    auto *showKey = new QCheckBox("Show key", artwork);
    showKey->setObjectName("welcomeShowKey");
    auto *getKey = new QPushButton("Get your API key", artwork);
    getKey->setObjectName("welcomeGetApiKey");
    keyActions->addWidget(showKey);
    keyActions->addStretch();
    keyActions->addWidget(getKey);
    artworkLayout->addLayout(keyActions);
    connect(showKey, &QCheckBox::toggled, this, [this](bool visible) {
        m_key->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password);
    });
    connect(getKey, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl("https://www.steamgriddb.com/profile/preferences/api"));
    });
    layout->addWidget(artwork);

    auto *global = new QGroupBox("Global settings", this);
    auto *globalLayout = new QVBoxLayout(global);
    auto *globalDescription = new QLabel("Review the defaults used for new games.", global);
    globalDescription->setWordWrap(true);
    globalLayout->addWidget(globalDescription);
    auto *reviewStatus = new QLabel(global);
    reviewStatus->setWordWrap(true);
    reviewStatus->hide();
    globalLayout->addWidget(reviewStatus);
    auto *review = new QPushButton("Review settings", global);
    review->setObjectName("welcomeReviewSettings");
    globalLayout->addWidget(review);
    layout->addWidget(global);
    connect(review, &QPushButton::clicked, this, [this, bootstrap, reviewStatus] {
        auto options = bootstrap;
        options.insert("settings", settingsData());
        SettingsDialog settings(options, this);
        if (settings.exec() == QDialog::Accepted) {
            m_draft = settings.settingsData();
            m_key->setText(m_draft.value("steamgriddb_api_key").toString());
            reviewStatus->setText("Settings reviewed · Select Done to apply");
            reviewStatus->show();
        }
    });
    auto *footer = new QHBoxLayout;
    auto *skip = new QPushButton("Skip for now", this);
    skip->setObjectName("welcomeSkip");
    auto *done = new QPushButton("Done", this);
    done->setObjectName("welcomeDone");
    footer->addWidget(skip, 1);
    footer->addWidget(done, 1);
    layout->addLayout(footer);
    connect(skip, &QPushButton::clicked, this, &QDialog::reject);
    connect(done, &QPushButton::clicked, this, &QDialog::accept);
    resize(480, sizeHint().height());
}

QJsonObject WelcomeDialog::settingsData() const {
    auto settings = m_draft;
    settings.insert("steamgriddb_api_key", m_key->text().trimmed());
    return settings;
}
