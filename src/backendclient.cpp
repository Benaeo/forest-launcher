#include "backendclient.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#include <memory>
#include <utility>

namespace {
struct ArtworkSession {
    QTemporaryDir directory{QString("/tmp/forest-launcher-artwork-%1-XXXXXX").arg(getuid())};
    int lock = -1;
    ArtworkSession() {
        if (!directory.isValid()) return;
        const auto filename = QFile::encodeName(directory.filePath(".owner.lock"));
        lock = ::open(filename.constData(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (lock < 0 || ::flock(lock, LOCK_EX | LOCK_NB) != 0) {
            if (lock >= 0) ::close(lock);
            lock = -1;
        }
    }
    ~ArtworkSession() {
        // Remove while the lock is still held, before another session's cleanup.
        directory.remove();
        if (lock >= 0) ::close(lock);
    }
};
}

QString BackendClient::artworkSessionDirectory() {
    static ArtworkSession session;
    return session.lock >= 0 ? session.directory.path() : QString();
}

BackendClient::BackendClient(QString directory, QString dataRoot, QObject *parent)
    : QObject(parent), m_directory(std::move(directory)), m_dataRoot(std::move(dataRoot)) {}

BackendClient::~BackendClient() {
    for (auto *process : findChildren<QProcess *>()) {
        process->disconnect(this);
        for (auto *timer : process->findChildren<QTimer *>()) timer->stop();
        if (process->state() != QProcess::NotRunning) {
            process->terminate();
            if (!process->waitForFinished(3000)) {
                process->kill();
                process->waitForFinished(1000);
            }
        }
    }
}

void BackendClient::request(const QString &action, const QJsonObject &params, Success success, Failure failure, Success progress) {
    auto *process = new QProcess(this);
    auto completed = std::make_shared<bool>(false);
    auto stderrBuffer = std::make_shared<QByteArray>();
    auto diagnosticBuffer = std::make_shared<QByteArray>();
    const auto payload = QJsonDocument(QJsonObject{
        {"protocol", 1}, {"action", action}, {"params", params}
    }).toJson(QJsonDocument::Compact);
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("PYTHONPATH", m_directory);
    environment.insert("PYTHONUTF8", "1");
    environment.insert("PYTHONDONTWRITEBYTECODE", "1");
    environment.insert("FOREST_ARTWORK_SESSION", artworkSessionDirectory());
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
    connect(process, &QProcess::readyReadStandardError, this, [process, completed, stderrBuffer, diagnosticBuffer, progress] {
        *stderrBuffer += process->readAllStandardError();
        int newline;
        while ((newline = stderrBuffer->indexOf('\n')) >= 0) {
            const auto line = stderrBuffer->left(newline);
            stderrBuffer->remove(0, newline + 1);
            if (line.startsWith("FOREST_PROGRESS ")) {
                const auto event = QJsonDocument::fromJson(line.mid(16));
                if (!*completed && progress && event.isObject()) progress(event.object());
            } else {
                *diagnosticBuffer += line + '\n';
            }
        }
    });
    connect(process, &QProcess::finished, this,
            [process, completed, success, failure, stderrBuffer, diagnosticBuffer](int exitCode, QProcess::ExitStatus exitStatus) {
        if (*completed) return;
        *completed = true;
        const auto output = process->readAllStandardOutput().trimmed();
        const auto diagnostics = QString::fromUtf8(*diagnosticBuffer + *stderrBuffer + process->readAllStandardError()).trimmed();
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
        failure("The backend request timed out. Check your connection or Steam and try again.");
        process->deleteLater();
    });
    // Account switching has one 105-second backend budget. Leave headroom
    // for planning/IPC; user confirmation runs between requests, not on this timer.
    deadline->start((action == "download_proton" || action == "download_latest_proton") ? 3600000
                    : action == "launch_game" ? 180000 : action == "prepare_umu" ? 120000 : 90000);
    const auto python = QStandardPaths::findExecutable("python3");
    process->start(python.isEmpty() ? "python3" : python, arguments);
}
