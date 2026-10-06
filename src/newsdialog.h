#pragma once

#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>

class BackendClient;
class QLabel;
class QListWidget;
class QPushButton;
class QTextBrowser;

class NewsDialog final : public QDialog {
public:
    NewsDialog(const QString &backendDirectory, const QString &dataRoot, QWidget *parent = nullptr);
private:
    void load(bool append = false);
    void showRelease(int row);
    void render(const QJsonObject &release);
    void sizeReleaseList();
    BackendClient *m_backend;
    QLabel *m_title;
    QLabel *m_status;
    QTextBrowser *m_news;
    QListWidget *m_list;
    QPushButton *m_retry;
    QJsonArray m_releases;
    int m_page = 1;
    quint64 m_generation = 0;
    bool m_loading = false;
    bool m_loadingImages = false;
    bool m_more = false;
};
