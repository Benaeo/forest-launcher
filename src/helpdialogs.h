#pragma once

#include <QApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

class NewsDialog final : public QDialog {
public:
    explicit NewsDialog(QWidget *parent = nullptr) : QDialog(parent) {
        setObjectName("newsDialog");
        setWindowTitle("News — Forest Launcher");
        resize(620, 440);
        auto *layout = new QVBoxLayout(this);
        auto *news = new QPlainTextEdit(this);
        news->setObjectName("newsText");
        news->setReadOnly(true);
        layout->addWidget(news);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }
};

class AboutDialog final : public QDialog {
public:
    explicit AboutDialog(const QString &backendDirectory, QWidget *parent = nullptr) : QDialog(parent) {
        setObjectName("aboutDialog");
        setWindowTitle("About Forest Launcher");
        resize(560, 580);
        auto *layout = new QVBoxLayout(this);
        auto *logo = new QLabel(this);
        logo->setObjectName("aboutLogo");
        const auto appIcon = QApplication::windowIcon().isNull()
            ? QIcon::fromTheme("applications-games", style()->standardIcon(QStyle::SP_ComputerIcon))
            : QApplication::windowIcon();
        logo->setPixmap(appIcon.pixmap(64, 64));
        logo->setAlignment(Qt::AlignCenter);
        layout->addWidget(logo);
        auto *name = new QLabel("Forest Launcher", this);
        name->setObjectName("aboutProjectName");
        name->setAlignment(Qt::AlignCenter);
        auto font = name->font();
        font.setPointSize(font.pointSize() + 5);
        name->setFont(font);
        layout->addWidget(name);
        auto *version = new QLabel(QCoreApplication::applicationVersion(), this);
        version->setObjectName("aboutVersion");
        version->setAlignment(Qt::AlignCenter);
        layout->addWidget(version);
        auto *tabs = new QTabWidget(this);
        tabs->setObjectName("aboutTabs");
        auto *credits = new QScrollArea(tabs);
        credits->setWidgetResizable(true);
        auto *creditContent = new QWidget(credits);
        auto *creditLayout = new QVBoxLayout(creditContent);
        auto *heading = new QLabel("Forest Launcher Developers", creditContent);
        heading->setAlignment(Qt::AlignCenter);
        auto headingFont = heading->font();
        headingFont.setBold(true);
        heading->setFont(headingFont);
        creditLayout->addWidget(heading);
        auto *developer = new QLabel("Benaeo &lt;<a href=\"https://github.com/Benaeo\">Github</a>&gt;", creditContent);
        developer->setObjectName("developerCredits");
        developer->setTextFormat(Qt::RichText);
        developer->setTextInteractionFlags(Qt::TextBrowserInteraction);
        developer->setOpenExternalLinks(true);
        developer->setAlignment(Qt::AlignCenter);
        creditLayout->addWidget(developer);
        creditLayout->addStretch();
        credits->setWidget(creditContent);
        tabs->addTab(credits, "Credits");
        auto *licensePage = new QWidget(tabs);
        auto *licenseLayout = new QVBoxLayout(licensePage);
        m_licensePath = QDir::cleanPath(QDir(backendDirectory).filePath("../LICENSE"));
        auto *fileLink = new QLabel("<a href=\"" + QUrl::fromLocalFile(m_licensePath).toString(QUrl::FullyEncoded).toHtmlEscaped()
                                   + "\">LICENSE</a>", licensePage);
        fileLink->setObjectName("licenseFileLink");
        fileLink->setTextInteractionFlags(Qt::TextBrowserInteraction);
        fileLink->setOpenExternalLinks(false);
        licenseLayout->addWidget(fileLink);
        m_license = new QPlainTextEdit(licensePage);
        m_license->setObjectName("licenseText");
        m_license->setReadOnly(true);
        licenseLayout->addWidget(m_license);
        connect(fileLink, &QLabel::linkActivated, this, [this](const QString &) { readLicense(); });
        readLicense();
        tabs->addTab(licensePage, "License");
        layout->addWidget(tabs, 1);
        auto *footer = new QHBoxLayout;
        auto *support = new QPushButton("Support the Project", this);
        support->setObjectName("supportProjectButton");
        footer->addWidget(support);
        footer->addStretch();
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
        footer->addWidget(buttons);
        connect(support, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl("https://www.paypal.com/")); });
        layout->addLayout(footer);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }
private:
    void readLicense() {
        QFile file(m_licensePath);
        m_license->setPlainText(file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString());
    }
    QString m_licensePath;
    QPlainTextEdit *m_license;
};
