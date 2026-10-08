#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <functional>

class BackendClient : public QObject {
public:
    using Success = std::function<void(const QJsonObject &)>;
    using Failure = std::function<void(const QString &)>;
    BackendClient(QString directory, QString dataRoot, QObject *parent = nullptr);
    ~BackendClient() override;
    static QString artworkSessionDirectory();
    void request(const QString &action, const QJsonObject &params, Success success, Failure failure,
                 Success progress = {});
private:
    QString m_directory;
    QString m_dataRoot;
};
