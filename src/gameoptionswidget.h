#pragma once

#include <QJsonObject>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QVBoxLayout;
class QProgressBar;
class QLabel;
class BackendClient;

class GameOptionsWidget : public QWidget {
public:
    GameOptionsWidget(const QJsonObject &options, const QJsonObject &bootstrap, QWidget *parent = nullptr,
                      bool defaultsEditor = false);
    QJsonObject optionsData() const;
    QComboBox *kindSelector() const { return m_kind; }
    QString kind() const;
    QString protonSelection() const;
    void setKind(const QString &kind);
    void addGeneralOption(QWidget *option);
private:
    void updateKind();
    void updateLatestButton();
    void refreshProtonChoices(const QString &selection);
    void finishLatestDownload();
    QComboBox *m_kind;
    QLineEdit *m_prefix;
    QPushButton *m_prefixBrowse;
    QComboBox *m_proton;
    QPushButton *m_downloadLatest;
    QPushButton *m_managerButton;
    QPushButton *m_losslessButton;
    QJsonObject m_losslessOptions;
    bool m_lsfgInstalled = false;
    QWidget *m_latestProgressRow;
    QProgressBar *m_latestProgress;
    QLabel *m_latestStatus;
    BackendClient *m_latestBackend = nullptr;
    bool m_latestDownloading = false;
    QString m_runnerRoot;
    QStringList m_latestIds;
    QLineEdit *m_arguments;
    QLineEdit *m_tags;
    QCheckBox *m_onlineFix;
    QCheckBox *m_mangohud;
    QCheckBox *m_preferSdl;
    QCheckBox *m_noSleep;
    QCheckBox *m_desktopShortcut;
    QCheckBox *m_appMenuShortcut;
    QPlainTextEdit *m_environment;
    QVBoxLayout *m_generalOptions = nullptr;
};
