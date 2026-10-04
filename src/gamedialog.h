#pragma once

#include <QDialog>
#include <QJsonObject>
#include <QSet>

class GameOptionsWidget;
class QLabel;
class QLineEdit;
class QPushButton;
class QShowEvent;
class QTimer;
class BackendClient;

class GameDialog : public QDialog {
public:
    GameDialog(const QJsonObject &game, const QJsonObject &bootstrap, QWidget *parent = nullptr);
    QJsonObject gameData() const;
    void setExecutablePath(const QString &path);
protected:
    void showEvent(QShowEvent *event) override;
private:
    void updateKind();
    void extractInitialIcon();
    void extractIcon();
    void extractionFailed(const QString &error);
    void chooseIconSource();
    bool chooseArtwork(bool startSearch = false, bool steamRequested = false);
    void updateIcon();
    void setIconBusy(bool busy);
    void validateAndAccept();
    QJsonObject m_original;
    QJsonObject m_bootstrap;
    QJsonObject m_artwork;
    QSet<QString> m_extractionAttempted;
    bool m_creating = false;
    bool m_iconBusy = false;
    int m_iconRevision = 0;
    QPushButton *m_icon;
    QTimer *m_iconDebounce;
    BackendClient *m_iconBackend = nullptr;
    QLineEdit *m_title;
    QLabel *m_pathLabel;
    QLineEdit *m_path;
    QPushButton *m_browse;
    GameOptionsWidget *m_options;
    QLabel *m_error;
};
