#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QTemporaryDir>
#include <QTimer>
#include <memory>

#ifndef FOREST_SOURCE_BACKEND_DIR
#define FOREST_SOURCE_BACKEND_DIR ""
#endif

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("forest-launcher");
    QCoreApplication::setApplicationVersion(FOREST_APPLICATION_VERSION);
    QCoreApplication::setOrganizationName("Forest");
    QApplication::setApplicationDisplayName("Forest Launcher");
    app.setDesktopFileName("io.github.Benaeo.forest-launcher");
    app.setWindowIcon(QIcon::fromTheme("applications-games"));
    QCommandLineParser parser;
    parser.setApplicationDescription("A native Qt game launcher with a headless Python backend.");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"backend-dir", "Backend module directory.", "directory"});
    parser.addOption({"data-root", "Isolate all mutable data under a directory.", "directory"});
    parser.addOption({"smoke-test", "Exercise the GUI/backend protocol without launching games."});
    parser.addPositionalArgument("executable", "Open the add-game dialog for this executable.", "[executable]");
    parser.process(app);
    const bool smoke = parser.isSet("smoke-test");
    std::unique_ptr<QTemporaryDir> temporary;
    QString dataRoot = parser.value("data-root");
    if (!dataRoot.isEmpty()) dataRoot = QFileInfo(dataRoot).absoluteFilePath();
    if (smoke) {
        temporary = std::make_unique<QTemporaryDir>();
        if (!temporary->isValid()) { qCritical("Cannot create smoke test directory."); return 1; }
        dataRoot = temporary->path();
    }
    QString backend = parser.value("backend-dir");
    if (backend.isEmpty()) backend = qEnvironmentVariable("FOREST_BACKEND_DIR");
    if (backend.isEmpty()) {
        backend = QDir(QCoreApplication::applicationDirPath()).filePath("../share/forest-launcher/backend");
        if (!QFileInfo::exists(backend + "/forest_backend/__main__.py")) backend = FOREST_SOURCE_BACKEND_DIR;
    }
    backend = QDir(backend).absolutePath();
    if (!QFileInfo::exists(backend + "/forest_backend/__main__.py")) {
        qCritical("Forest backend files were not found. Reinstall Forest or specify --backend-dir.");
        return 1;
    }
    MainWindow window(backend, dataRoot, smoke);
    window.show();
    const auto arguments = parser.positionalArguments();
    if (!arguments.isEmpty() && !smoke) {
        const QString path = QFileInfo(arguments.first()).absoluteFilePath();
        QTimer::singleShot(0, &window, [&window, path] { window.addExecutable(path); });
    }
    return app.exec();
}
