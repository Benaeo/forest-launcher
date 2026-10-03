#pragma once

#include <QJsonObject>
#include <QMainWindow>

class QAction;
class BackendClient;
class QLabel;
class QListView;
class QPushButton;
class QSortFilterProxyModel;
class QStackedWidget;
class QStandardItemModel;
class QDragEnterEvent;
class QDropEvent;

class MainWindow : public QMainWindow {
public:
    MainWindow(QString backendDirectory, QString dataRoot, bool smokeTest, QWidget *parent = nullptr);
    void addExecutable(const QString &path);
protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
private:
    void refresh(const QString &selectedId = {});
    void populateLibrary(const QString &selectedId);
    QJsonObject selectedGame() const;
    void updateSelection();
    void editGame(const QJsonObject &game);
    void removeSelected();
    void launchSelected();
    void previewSelected();
    void showSettings(const QJsonObject &pendingSettings = {});
    void showError(const QString &message);
    void setBusy(bool busy);
    void runSmokeTest();
    BackendClient *m_backend;
    QString m_dataRoot;
    bool m_smokeTest;
    bool m_smokeStarted = false;
    bool m_busy = true;
    QJsonObject m_bootstrap;
    QStandardItemModel *m_model;
    QSortFilterProxyModel *m_proxy;
    QListView *m_library;
    QStackedWidget *m_details;
    QLabel *m_title;
    QLabel *m_type;
    QLabel *m_path;
    QLabel *m_prefix;
    QLabel *m_proton;
    QLabel *m_tags;
    QLabel *m_lastLaunched;
    QPushButton *m_play;
    QPushButton *m_edit;
    QPushButton *m_log;
    QAction *m_addAction;
    QAction *m_editAction;
    QAction *m_removeAction;
    QAction *m_playAction;
    QAction *m_previewAction;
    QAction *m_settingsAction;
};
