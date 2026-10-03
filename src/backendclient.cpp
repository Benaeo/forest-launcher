#include "backendclient.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTimer>
#include <memory>
#include <utility>

BackendClient::BackendClient(QString directory, QString dataRoot, QObject *parent)
    : QObject(parent), m_directory(std::move(directory)), m_dataRoot(std::move(dataRoot)) {}

BackendClient::~BackendClient() {
    for (auto *process : findChildren<QProcess *>()) {
        process->disconnect(this);
        for (auto *timer : process->findChildren<QTimer *>()) timer->stop();
        if (process->state() != QProcess::NotRunning) {
            process->kill();
            process->waitForFinished(1000);
        }
    }
}

void BackendClient::request(const QString &action, const QJsonObject &params, Success success, Failure failure) {
    auto *process = new QProcess(this);
    auto completed = std::make_shared<bool>(false);
    const auto payload = QJsonDocument(QJsonObject{
        {"protocol", 1}, {"action", action}, {"params", params}
    }).toJson(QJsonDocument::Compact);
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("PYTHONPATH", m_directory);
    environment.insert("PYTHONUTF8", "1");
    environment.insert("PYTHONDONTWRITEBYTECODE", "1");
    process->setProcessEnvironment(environment);
    process->setWorkingDirectory(m_directory);
    QStringList arguments{"-m", "forest_backend"};
    if (!m_dataRoot.isEmpty()) arguments << "--data-root" << m_dataRoot;

    connect(process, &QProcess::started, this, [process, payload] {
        process->write(payload);
        process->closeWriteChannel();
    });
    connect(process, &QProcess::errorOccurred, this, [process, completed, failure](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && !*completed) {
            *completed = true;
            failure("Could not start the Python backend: " + process->errorString());
            process->deleteLater();
        }
    });
    connect(process, &QProcess::finished, this,
            [process, completed, success, failure](int exitCode, QProcess::ExitStatus exitStatus) {
        if (*completed) return;
        *completed = true;
        const auto output = process->readAllStandardOutput().trimmed();
        const auto diagnostics = QString::fromUtf8(process->readAllStandardError()).trimmed();
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(output, &error);
        process->deleteLater();
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            failure(diagnostics.isEmpty() ? "The backend returned an invalid response." : diagnostics);
            return;
        }
        const auto response = document.object();
        if (response.value("protocol").toInt() != 1 || !response.value("ok").isBool()) {
            failure("The frontend and backend protocol versions do not match.");
        } else if (!response.value("ok").toBool()) {
            failure(response.value("error").toObject().value("message").toString("The backend request failed."));
        } else if (exitCode != 0 || exitStatus != QProcess::NormalExit || !response.value("data").isObject()) {
            failure("The backend exited unexpectedly.");
        } else {
            success(response.value("data").toObject());
        }
    });
    auto *deadline = new QTimer(process);
    deadline->setSingleShot(true);
    connect(deadline, &QTimer::timeout, this, [process, completed, failure] {
        if (*completed) return;
        *completed = true;
        process->kill();
        process->waitForFinished(1000);
        failure("The backend request timed out. Check Steam and try again.");
        process->deleteLater();
    });
    deadline->start(90000);
    const auto python = QStandardPaths::findExecutable("python3");
    process->start(python.isEmpty() ? "python3" : python, arguments);
}
