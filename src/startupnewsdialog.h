#pragma once

#include <QDialog>
#include <QJsonArray>
#include <QString>
#include <QPointer>
#include <QVector>

class BackendClient;
class QTextBrowser;

class UpdateAnnouncementDialog final : public QDialog {
public:
    explicit UpdateAnnouncementDialog(const QString &version, QWidget *parent = nullptr);
};

class ReleaseNotesDialog final : public QDialog {
public:
    ReleaseNotesDialog(const QString &installedVersion, const QJsonArray &releases, QWidget *parent = nullptr,
                       const QString &backendDirectory = {}, const QString &dataRoot = {});
private:
    void loadNextImages();
    struct ImageRequest {
        QPointer<QTextBrowser> browser;
        QJsonArray urls;
    };
    BackendClient *m_backend = nullptr;
    QVector<ImageRequest> m_imageRequests;
};
