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
class QGridLayout;
class QToolButton;
class BackendClient;
class QTimer;

class GameOptionsWidget : public QWidget {
public:
    GameOptionsWidget(const QJsonObject &options, const QJsonObject &bootstrap, QWidget *parent = nullptr,
                      bool defaultsEditor = false);
    QJsonObject optionsData() const;
    QComboBox *kindSelector() const { return m_kind; }
    QString kind() const;
    QString protonSelection() const;
    void setKind(const QString &kind);
    void setExecutablePath(const QString &path);
    void addGeneralOption(QWidget *option);
    void addIconControl(QWidget *control);
    QCheckBox *steamShortcutCheck() const { return m_steamShortcut; }
private:
    void updateKind();
    void checkOnlineFix();
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
    bool m_onlineFixSupported = false;
    QString m_onlineFixReason;
    QString m_executablePath;
    QString m_backendDirectory;
    QString m_dataRoot;
    QTimer *m_onlineFixDebounce;
    BackendClient *m_onlineFixBackend = nullptr;
    quint64 m_onlineFixRevision = 0;
    QCheckBox *m_mangohud;
    QCheckBox *m_preferSdl;
    QCheckBox *m_noSleep;
    QCheckBox *m_desktopShortcut;
    QCheckBox *m_appMenuShortcut;
    QCheckBox *m_steamShortcut;
    QToolButton *m_steamAccountsButton;
    QToolButton *m_steamLaunchAccountButton;
    QGridLayout *m_launchTail = nullptr;
    bool m_defaultsEditor = false;
    bool m_hasSteamAccounts = false;
    QPlainTextEdit *m_environment;
    QVBoxLayout *m_generalOptions = nullptr;
};
