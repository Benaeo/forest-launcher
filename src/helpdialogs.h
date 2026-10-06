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
#include <QTextBrowser>
#include <QRegularExpression>
#include "newsdialog.h"
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

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
        fileLink->setAlignment(Qt::AlignCenter);
        fileLink->setTextInteractionFlags(Qt::TextBrowserInteraction);
        fileLink->setOpenExternalLinks(false);
        licenseLayout->addWidget(fileLink);
        m_license = new QTextBrowser(licensePage);
        m_license->setObjectName("licenseText");
        m_license->setOpenLinks(false);
        m_license->setFrameShape(QFrame::NoFrame);
        m_license->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_license->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_license->document()->setDocumentMargin(16);
        m_license->document()->setDefaultStyleSheet("p { text-align: left; line-height: 125%; }");
        licenseLayout->addWidget(m_license);
        connect(fileLink, &QLabel::linkActivated, this, [this](const QString &) { readLicense(); });
        readLicense();
        tabs->addTab(licensePage, "License");
        layout->addWidget(tabs, 1);
        auto *footer = new QHBoxLayout;
        auto *support = new QPushButton("Support the Project", this);
        support->setObjectName("supportProjectButton");
        support->setIcon(QIcon::fromTheme("food", style()->standardIcon(QStyle::SP_DialogApplyButton)));
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
        if (!file.open(QIODevice::ReadOnly)) {
            m_license->setPlainText("The local LICENSE file could not be opened.");
            return;
        }
        // Reflow only the display; never edit or replace the official LICENSE.
        const auto original = QString::fromUtf8(file.readAll());
        QString html;
        for (const auto &paragraph : original.split(QRegularExpression("\\n[ \\t]*\\n"))) {
            QString text = paragraph.trimmed();
            if (text.isEmpty()) continue;
            text.replace(QRegularExpression("[ \\t]*\\n[ \\t]*"), " ");
            if (text.startsWith("GNU GENERAL PUBLIC LICENSE")) {
                html += "<h3 align=\"center\">GNU GENERAL PUBLIC LICENSE</h3>"
                        "<p align=\"center\">Version 3, 29 June 2007</p>";
            } else if (text == "Preamble" || text == "TERMS AND CONDITIONS"
                       || text == "END OF TERMS AND CONDITIONS"
                       || text == "How to Apply These Terms to Your New Programs") {
                html += "<h3 align=\"center\">" + text.toHtmlEscaped() + "</h3>";
            } else {
                html += "<p>" + text.toHtmlEscaped() + "</p>";
            }
        }
        m_license->setHtml(html);
    }
    QString m_licensePath;
    QTextBrowser *m_license;
};
