#include "mainwindow.h"
#include "backendclient.h"
#include "launchconfirmation.h"
#include "gamedialog.h"
#include "settingsdialog.h"
#include "removegamedialog.h"
#include "helpdialogs.h"
#include "artworkui.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <utility>

namespace {
constexpr int GameRole = Qt::UserRole + 1;
constexpr int SearchRole = Qt::UserRole + 2;
QLabel *valueLabel(QWidget *parent) {
    auto *label = new QLabel(parent);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}
QStringList tagNames(const QJsonObject &game) {
    QStringList tags;
    for (const auto &tag : game.value("tags").toArray()) tags << tag.toString();
    return tags;
}
}

MainWindow::MainWindow(QString backendDirectory, QString dataRoot, bool smokeTest, QWidget *parent)
    : QMainWindow(parent), m_dataRoot(std::move(dataRoot)), m_smokeTest(smokeTest) {
    setWindowTitle("Forest Launcher");
    resize(970, 620);
    setAcceptDrops(true);
    m_shortcutContext = {{"launcher", QCoreApplication::applicationFilePath()}, {"backend", backendDirectory}};
    m_backend = new BackendClient(std::move(backendDirectory), m_dataRoot, this);
    const auto icon = [this](const QString &name, QStyle::StandardPixmap fallback) {
        return QIcon::fromTheme(name, style()->standardIcon(fallback));
    };
    m_addAction = new QAction(icon("list-add", QStyle::SP_FileIcon), "Add game…", this);
    m_addAction->setShortcut(QKeySequence::New);
    m_editAction = new QAction(icon("document-edit", QStyle::SP_FileDialogDetailedView), "Edit game…", this);
    m_editAction->setShortcut(QKeySequence("Ctrl+E"));
    m_removeAction = new QAction(icon("list-remove", QStyle::SP_TrashIcon), "Remove from library…", this);
    m_removeAction->setShortcut(QKeySequence::Delete);
    m_playAction = new QAction(icon("media-playback-start", QStyle::SP_MediaPlay), "Play", this);
    m_playAction->setShortcut(QKeySequence("Ctrl+Return"));
    m_contextEditAction = new QAction("Edit", this);
    m_contextEditAction->setObjectName("contextEditAction");
    m_playAction->setObjectName("playAction");
    m_removeAction->setText("Remove from library");
    m_removeAction->setObjectName("removeGameAction");
    m_runFileAction = new QAction("Run file in the prefix", this);
    m_runFileAction->setObjectName("runFileAction");
    m_settingsAction = new QAction(icon("configure", QStyle::SP_FileDialogDetailedView), "Settings…", this);
    // No menu bar: keep shortcuts registered on the window even for actions
    // that now appear only in the context menu (Play/Stop and Remove).
    auto *quit = new QAction("Quit", this);
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);
    addActions({m_addAction, m_editAction, m_removeAction, m_playAction, m_runFileAction, m_settingsAction, quit});
    auto *toolbar = addToolBar("Library");
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar->addAction(m_addAction);
    toolbar->addAction(m_editAction);
    toolbar->addSeparator();
    toolbar->addAction(m_settingsAction);
    auto *help = new QToolButton(toolbar);
    help->setObjectName("helpButton");
    help->setText("Help");
    help->setIcon(icon("help-contents", QStyle::SP_DialogHelpButton));
    help->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    help->setPopupMode(QToolButton::InstantPopup);
    auto *helpMenu = new QMenu(help);
    helpMenu->setObjectName("helpMenu");
    auto *report = helpMenu->addAction("Report a bug or suggest a feature");
    report->setObjectName("reportIssueAction");
    connect(report, &QAction::triggered, this, [] {
        QDesktopServices::openUrl(QUrl("https://github.com/Benaeo/forest-launcher/issues"));
    });
    auto *news = helpMenu->addAction("News");
    news->setObjectName("newsAction");
    connect(news, &QAction::triggered, this, [this] { NewsDialog dialog(this); dialog.exec(); });
    auto *about = helpMenu->addAction("About");
    about->setObjectName("aboutAction");
    connect(about, &QAction::triggered, this, [this] {
        AboutDialog dialog(m_shortcutContext.value("backend").toString(), this);
        dialog.exec();
    });
    help->setMenu(helpMenu);
    toolbar->addWidget(help);

    auto *splitter = new QSplitter(this);
    auto *sidebar = new QWidget(splitter);
    auto *sidebarLayout = new QVBoxLayout(sidebar);
    auto *search = new QLineEdit(sidebar);
    search->setObjectName("librarySearch");
    search->setPlaceholderText("Search games or tags");
    search->setClearButtonEnabled(true);
    sidebarLayout->addWidget(search);
    m_model = new QStandardItemModel(this);
    m_proxy = new QSortFilterProxyModel(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setFilterRole(SearchRole);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_library = new QListView(sidebar);
    m_library->setObjectName("gameLibrary");
    m_library->setModel(m_proxy);
    m_library->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_library->setIconSize(QSize(32, 32));
    m_library->setSpacing(4);
    m_library->setContextMenuPolicy(Qt::CustomContextMenu);
    sidebarLayout->addWidget(m_library, 1);
    m_details = new QStackedWidget(splitter);
    auto *empty = new QWidget(m_details);
    auto *emptyLayout = new QVBoxLayout(empty);
    emptyLayout->addStretch();
    auto *welcome = new QLabel("Your games, without the clutter.", empty);
    welcome->setAlignment(Qt::AlignCenter);
    auto welcomeFont = welcome->font();
    welcomeFont.setPointSize(welcomeFont.pointSize() + 3);
    welcome->setFont(welcomeFont);
    emptyLayout->addWidget(welcome);
    auto *explanation = new QLabel("Add a Windows, native Linux, or Steam game.\nYou can also drop an executable into this window.", empty);
    explanation->setAlignment(Qt::AlignCenter);
    explanation->setWordWrap(true);
    emptyLayout->addWidget(explanation);
    auto *addButton = new QPushButton(icon("list-add", QStyle::SP_FileIcon), "Add your first game", empty);
    emptyLayout->addWidget(addButton, 0, Qt::AlignHCenter);
    connect(addButton, &QPushButton::clicked, this, [this] { if (!m_busy) editGame({}); });
    emptyLayout->addStretch();
    m_details->addWidget(empty);
    auto *detail = new QWidget(m_details);
    auto *detailLayout = new QVBoxLayout(detail);
    detailLayout->setContentsMargins(24, 24, 24, 24);
    m_title = valueLabel(detail);
    auto titleFont = m_title->font();
    titleFont.setPointSize(titleFont.pointSize() + 7);
    titleFont.setBold(true);
    m_title->setFont(titleFont);
    detailLayout->addWidget(m_title);
    auto *information = new QFormLayout;
    m_type = valueLabel(detail);
    m_path = valueLabel(detail);
    m_prefix = valueLabel(detail);
    m_proton = valueLabel(detail);
    m_tags = valueLabel(detail);
    m_lastLaunched = valueLabel(detail);
    information->addRow("Type", m_type);
    information->addRow("Executable / App ID", m_path);
    information->addRow("Prefix", m_prefix);
    information->addRow("Proton", m_proton);
    information->addRow("Tags", m_tags);
    information->addRow("Last launched", m_lastLaunched);
    detailLayout->addLayout(information);
    detailLayout->addStretch();
    auto *buttons = new QHBoxLayout;
    m_play = new QPushButton(icon("media-playback-start", QStyle::SP_MediaPlay), "Play", detail);
    m_play->setObjectName("playButton");
    m_play->setMinimumHeight(40);
    m_edit = new QPushButton("Edit…", detail);
    m_log = new QPushButton("Open launch log", detail);
    buttons->addWidget(m_play);
    buttons->addWidget(m_edit);
    buttons->addStretch();
    buttons->addWidget(m_log);
    detailLayout->addLayout(buttons);
    m_details->addWidget(detail);
    splitter->addWidget(sidebar);
    splitter->addWidget(m_details);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({330, 640});
    setCentralWidget(splitter);
    statusBar()->showMessage("Loading library…");
    m_umuStatus = new QLabel(this);
    m_umuStatus->setObjectName("umuStatusBar");
    m_umuStatus->setTextFormat(Qt::PlainText);
    statusBar()->addPermanentWidget(m_umuStatus);

    connect(search, &QLineEdit::textChanged, m_proxy, &QSortFilterProxyModel::setFilterFixedString);
    connect(m_library->selectionModel(), &QItemSelectionModel::currentChanged, this, [this] { updateSelection(); });
    connect(m_library, &QListView::doubleClicked, this, [this] { launchSelected(); });
    connect(m_library, &QListView::customContextMenuRequested, this, [this](const QPoint &position) {
        const auto index = m_library->indexAt(position);
        if (!index.isValid()) return;
        m_library->setCurrentIndex(index);
        QMenu menu(this);
        menu.setObjectName("gameContextMenu");
        menu.addActions({m_playAction, m_contextEditAction, m_runFileAction});
        menu.addSeparator();
        menu.addAction(m_removeAction);
        menu.exec(m_library->viewport()->mapToGlobal(position));
    });
    connect(m_addAction, &QAction::triggered, this, [this] { editGame({}); });
    connect(m_editAction, &QAction::triggered, this, [this] { editGame(selectedGame()); });
    connect(m_removeAction, &QAction::triggered, this, &MainWindow::removeSelected);
    connect(m_playAction, &QAction::triggered, this, &MainWindow::launchSelected);
    connect(m_contextEditAction, &QAction::triggered, this, [this] { editGame(selectedGame()); });
    connect(m_runFileAction, &QAction::triggered, this, &MainWindow::runFileSelected);
    auto *monitor = new QTimer(this);
    monitor->setInterval(2000);
    connect(monitor, &QTimer::timeout, this, &MainWindow::pollRunning);
    if (!m_smokeTest) monitor->start();
    connect(m_settingsAction, &QAction::triggered, this, [this] { showSettings(); });
    connect(m_play, &QPushButton::clicked, this, &MainWindow::launchSelected);
    connect(m_edit, &QPushButton::clicked, this, [this] { editGame(selectedGame()); });
    connect(m_log, &QPushButton::clicked, this, [this] {
        const auto path = m_bootstrap.value("paths").toObject().value("state").toString()
            + "/logs/" + selectedGame().value("id").toString() + "/launch.log";
        if (!QFile::exists(path)) showError("This game has no launch log yet.");
        else QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    setBusy(true);
    refresh();
}

QJsonObject MainWindow::selectedGame() const {
    return m_library->currentIndex().data(GameRole).value<QJsonObject>();
}

void MainWindow::setBusy(bool busy) {
    if (busy) ++m_runningGeneration;
    m_busy = busy;
    m_addAction->setEnabled(!busy);
    m_settingsAction->setEnabled(!busy);
    updateSelection();
}

void MainWindow::refresh(const QString &selectedId) {
    const auto selection = selectedId.isEmpty() ? selectedGame().value("id").toString() : selectedId;
    setBusy(true);
    m_backend->request("bootstrap", {}, [this, selection](const QJsonObject &data) {
        m_bootstrap = data;
        m_bootstrap.insert("frontend", QJsonObject{{"backend", m_shortcutContext.value("backend")}, {"data_root", m_dataRoot}});
        populateLibrary(selection);
        setBusy(false);
        statusBar()->showMessage(QString("%1 games · Forest %2")
            .arg(m_model->rowCount()).arg(QCoreApplication::applicationVersion()));
        if (m_smokeTest && !m_smokeStarted) {
            m_smokeStarted = true;
            runSmokeTest();
        } else if (!m_smokeTest && !m_umuStarted) {
            prepareUmu();
        }
        updateUmuStatus();
    }, [this](const QString &error) { setBusy(false); showError(error); });
}

void MainWindow::populateLibrary(const QString &selectedId) {
    m_model->clear();
    const auto icon = QIcon::fromTheme("applications-games", style()->standardIcon(QStyle::SP_ComputerIcon));
    QModelIndex selected;
    for (const auto &value : m_bootstrap.value("games").toArray()) {
        const auto game = value.toObject();
        const auto iconPath = game.value("artwork").toObject().value("icon").toString();
        const auto image = iconPath.isEmpty() ? QImage() : readArtworkImage(iconPath);
        auto *item = new QStandardItem(image.isNull() ? icon : QIcon(QPixmap::fromImage(image)), game.value("title").toString());
        item->setData(QVariant::fromValue(game), GameRole);
        item->setData(game.value("title").toString() + " " + tagNames(game).join(" "), SearchRole);
        item->setToolTip(game.value("path").toString());
        m_model->appendRow(item);
        if (game.value("id").toString() == selectedId) selected = m_proxy->mapFromSource(item->index());
    }
    if (!selected.isValid() && m_proxy->rowCount() > 0) selected = m_proxy->index(0, 0);
    m_library->setCurrentIndex(selected);
    updateSelection();
}

void MainWindow::updateSelection() {
    const auto game = selectedGame();
    const bool requiresUmu = game.value("kind").toString() == "windows" && !tagNames(game).contains("online-fix");
    const bool waitingForUmu = requiresUmu && m_umuUpdating && !m_bootstrap.value("umu").toObject().value("available").toBool();
    const bool available = !m_busy && !game.isEmpty();
    for (auto *action : {m_editAction, m_contextEditAction, m_removeAction, m_playAction}) action->setEnabled(available);
    const bool running = m_bootstrap.value("running").toArray().contains(game.value("id"));
    const bool stoppable = running && game.value("kind").toString() != "steam";
    const auto playText = stoppable ? "Stop" : "Play";
    const auto playIcon = QIcon::fromTheme(stoppable ? "media-playback-stop" : "media-playback-start",
        style()->standardIcon(stoppable ? QStyle::SP_MediaStop : QStyle::SP_MediaPlay));
    const auto playTooltip = stoppable ? "Stop this game immediately. Unsaved progress will be lost."
                                      : "Start this game.";
    m_play->setText(playText);
    m_play->setIcon(playIcon);
    m_play->setToolTip(playTooltip);
    m_playAction->setText(playText);
    m_playAction->setIcon(playIcon);
    m_playAction->setToolTip(playTooltip);
    m_runFileAction->setEnabled(available && game.value("kind").toString() == "windows" && !waitingForUmu);
    for (auto *button : {m_play, m_edit, m_log}) button->setEnabled(available);
    m_play->setEnabled(available && (stoppable || (!running && !waitingForUmu)));
    // Button, context menu, Library menu and keyboard all share Play/Stop.
    m_playAction->setEnabled(m_play->isEnabled());
    m_details->setCurrentIndex(game.isEmpty() ? 0 : 1);
    if (game.isEmpty()) return;
    m_title->setText(game.value("title").toString());
    const auto kind = game.value("kind").toString();
    m_type->setText(kind == "windows" ? "Windows · Proton / UMU" : kind == "steam" ? "Steam library" : "Native Linux");
    m_path->setText(game.value("path").toString());
    auto prefix = game.value("prefix").toString();
    if (prefix.isEmpty()) prefix = m_bootstrap.value("settings").toObject().value("prefix_root").toString()
        + "/" + game.value("id").toString();
    m_prefix->setText(kind == "windows" ? prefix : "—");
    auto proton = game.value("proton").toString();
    if (proton == "default") proton = m_bootstrap.value("settings").toObject().value("default_proton").toString();
    for (const auto &value : m_bootstrap.value("protons").toArray()) {
        const auto build = value.toObject();
        if (build.value("id").toString() == proton) { proton = build.value("label").toString(); break; }
    }
    m_proton->setText(kind != "windows" ? "—" : proton == "auto" ? "Automatic" : proton);
    m_tags->setText(tagNames(game).isEmpty() ? "—" : tagNames(game).join(", "));
    const auto last = QDateTime::fromString(game.value("last_launched").toString(), Qt::ISODate);
    m_lastLaunched->setText(last.isValid() ? last.toLocalTime().toString("yyyy-MM-dd HH:mm") : "Never");
}

void MainWindow::editGame(const QJsonObject &game) {
    if (m_busy) return;
    GameDialog dialog(game, m_bootstrap, this);
    if (dialog.exec() != QDialog::Accepted) { refresh(); return; }
    const auto updated = dialog.gameData();
    setBusy(true);
    m_backend->request("save_game", {{"game", updated}, {"shortcut_context", m_shortcutContext}}, [this](const QJsonObject &data) {
        refresh(data.value("game").toObject().value("id").toString());
        if (!data.value("warning").toString().isEmpty()) showError(data.value("warning").toString());
        else if (!data.value("notice").toString().isEmpty()) QMessageBox::information(this, "Steam shortcuts", data.value("notice").toString());
    }, [this, updated](const QString &error) {
        setBusy(false);
        showError(error);
        editGame(updated);
    });
}

void MainWindow::addExecutable(const QString &path) {
    if (m_busy) {
        QTimer::singleShot(100, this, [this, path] { addExecutable(path); });
        return;
    }
    GameDialog dialog({}, m_bootstrap, this);
    dialog.setExecutablePath(path);
    if (dialog.exec() != QDialog::Accepted) { refresh(); return; }
    const auto game = dialog.gameData();
    setBusy(true);
    m_backend->request("save_game", {{"game", game}, {"shortcut_context", m_shortcutContext}}, [this](const QJsonObject &data) {
        refresh(data.value("game").toObject().value("id").toString());
        if (!data.value("warning").toString().isEmpty()) showError(data.value("warning").toString());
        else if (!data.value("notice").toString().isEmpty()) QMessageBox::information(this, "Steam shortcuts", data.value("notice").toString());
    }, [this, game](const QString &error) { setBusy(false); showError(error); editGame(game); });
}

void MainWindow::removeSelected() {
    const auto game = selectedGame();
    if (m_busy || game.isEmpty()) return;
    setBusy(true);
    m_backend->request("removal_info", {{"id", game.value("id")}}, [this, game](const QJsonObject &info) {
        RemoveGameDialog prompt(game, info, this);
        if (prompt.exec() != QDialog::Accepted) { setBusy(false); return; }
        m_backend->request("delete_game", {{"id", game.value("id")}, {"delete_prefix", prompt.deletePrefix()},
                                          {"expected_prefix", info}}, [this](const QJsonObject &) { refresh(); },
            [this](const QString &error) { setBusy(false); showError(error); });
    }, [this](const QString &error) { setBusy(false); showError(error); });
}

void MainWindow::launchSelected() {
    const auto game = selectedGame();
    if (m_busy || game.isEmpty()) return;
    if (m_bootstrap.value("running").toArray().contains(game.value("id"))) {
        stopSelected();
        return;
    }
    if (!m_playAction->isEnabled()) return;
    setBusy(true);
    statusBar()->showMessage("Starting " + game.value("title").toString() + "…");
    launchGameWithConfirmation(m_backend, this, game.value("id").toString(), [this, game](const QJsonObject &) {
        if (m_bootstrap.value("settings").toObject().value("close_after_launch").toBool()) {
            close();
            return;
        }
        refresh(game.value("id").toString());
        statusBar()->showMessage("Launch started.", 5000);
    }, [this](const QString &error) { setBusy(false); statusBar()->showMessage("Launch failed."); showError(error); },
       [this] { setBusy(false); statusBar()->showMessage("Launch cancelled. Steam was not changed.", 5000); });
}

void MainWindow::stopSelected() {
    const auto game = selectedGame();
    if (m_busy || game.isEmpty() || game.value("kind").toString() == "steam"
        || !m_bootstrap.value("running").toArray().contains(game.value("id"))) return;
    // Stop is immediate by user preference; block repeated activation while
    // the existing profile-owned, PID-reuse-safe force-stop request runs.
    setBusy(true);
    m_backend->request("stop_game", {{"id", game.value("id")}}, [this](const QJsonObject &) {
        refresh();
        statusBar()->showMessage("Force-stop request sent.", 5000);
    }, [this](const QString &error) { setBusy(false); showError(error); });
}

void MainWindow::runFileSelected() {
    const auto game = selectedGame();
    if (!m_runFileAction->isEnabled() || game.isEmpty()) return;
    const auto file = QFileDialog::getOpenFileName(this, "Select a file to run in the prefix",
        QFileInfo(game.value("path").toString()).absolutePath(),
        "Windows files (*.exe *.msi *.bat *.lnk *.reg *.EXE *.MSI *.BAT *.LNK *.REG);;All files (*)");
    if (file.isEmpty()) return;
    if (QFileInfo(file).suffix().compare("reg", Qt::CaseInsensitive) == 0) {
        QMessageBox prompt(QMessageBox::Warning, "Import registry file",
            "Import this registry file into the selected game’s prefix?\nRegistry changes can affect other games sharing the prefix.",
            QMessageBox::Yes | QMessageBox::No, this);
        prompt.setDefaultButton(QMessageBox::No);
        if (prompt.exec() != QMessageBox::Yes) return;
    }
    setBusy(true);
    m_backend->request("run_file", {{"id", game.value("id")}, {"file", file}}, [this](const QJsonObject &) {
        refresh();
        statusBar()->showMessage("Prefix file started.", 5000);
    }, [this](const QString &error) { setBusy(false); showError(error); });
}

void MainWindow::pollRunning() {
    if (m_busy || m_polling || m_bootstrap.isEmpty()) return;
    m_polling = true;
    const auto generation = m_runningGeneration;
    m_backend->request("running_games", {}, [this, generation](const QJsonObject &data) {
        m_polling = false;
        // A poll started before Play/Stop can arrive after its refresh and
        // otherwise replace newer launch state with an older snapshot.
        if (m_busy || generation != m_runningGeneration) return;
        m_bootstrap.insert("running", data.value("running"));
        updateSelection();
    }, [this](const QString &) { m_polling = false; });
}

void MainWindow::showSettings(const QJsonObject &pendingSettings) {
    auto bootstrap = m_bootstrap;
    if (!pendingSettings.isEmpty()) bootstrap.insert("settings", pendingSettings);
    SettingsDialog dialog(bootstrap, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const auto values = dialog.settingsData();
    setBusy(true);
    m_backend->request("save_settings", {{"settings", values}}, [this](const QJsonObject &) { refresh(); },
        [this, values](const QString &error) {
            setBusy(false);
            showError(error);
            showSettings(values);
        });
}

void MainWindow::prepareUmu() {
    m_umuStarted = true;
    m_umuUpdating = true;
    updateUmuStatus();
    m_backend->request("prepare_umu", {}, [this](const QJsonObject &data) {
        m_umuUpdating = false;
        m_bootstrap.insert("umu", data.value("umu"));
        updateUmuStatus();
        const auto status = data.value("umu").toObject();
        if (!status.value("available").toBool())
            statusBar()->showMessage("UMU setup needs an internet connection. Play will retry setup.");
        else if (!status.value("last_error").toString().isEmpty())
            statusBar()->showMessage("UMU update unavailable; the existing launcher is ready.", 8000);
    }, [this](const QString &error) {
        m_umuUpdating = false;
        auto status = m_bootstrap.value("umu").toObject();
        status.insert("last_error", error);
        m_bootstrap.insert("umu", status);
        updateUmuStatus();
        statusBar()->showMessage("Automatic UMU setup could not finish. Play will retry setup.");
    });
}

void MainWindow::updateUmuStatus() {
    const auto status = m_bootstrap.value("umu").toObject();
    m_umuStatus->setText(m_umuUpdating ? "Checking UMU…" : status.value("available").toBool() ? "UMU ready" : "UMU setup needed");
    m_umuStatus->setToolTip(status.value("last_error").toString().isEmpty()
        ? status.value("path").toString() : status.value("last_error").toString());
    updateSelection();
}

void MainWindow::showError(const QString &message) {
    if (m_smokeTest) {
        qCritical().noquote() << message;
        QApplication::exit(1);
        return;
    }
    QMessageBox box(QMessageBox::Warning, "Forest Launcher", message, QMessageBox::Ok, this);
    box.setTextFormat(Qt::PlainText);
    box.exec();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
    if (!m_busy && event->mimeData()->hasUrls()) {
        for (const auto &url : event->mimeData()->urls())
            if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isFile()) {
                event->acceptProposedAction();
                return;
            }
    }
}

void MainWindow::dropEvent(QDropEvent *event) {
    if (m_busy) return;
    for (const auto &url : event->mimeData()->urls()) {
        if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isFile()) {
            event->acceptProposedAction();
            addExecutable(url.toLocalFile());
            return;
        }
    }
}

void MainWindow::runSmokeTest() {
    const auto failure = [this](const QString &error) { showError("Smoke test: " + error); };
    const QString exe = m_dataRoot + "/Forest smoke.exe";
    const QString umu = m_dataRoot + "/bin/umu-run";
    QDir().mkpath(m_dataRoot + "/bin");
    const auto home = m_dataRoot + "/home";
    const auto runners = home + "/.local/share/Steam/compatibilitytools.d";
    const auto proton = runners + "/Proton-CachyOS Latest";
    QDir().mkpath(proton);
    qputenv("HOME", home.toUtf8());
    m_bootstrap.insert("protons", QJsonArray{
        QJsonObject{{"id", proton}, {"label", "Proton-CachyOS Latest"}, {"installed", true}},
        QJsonObject{{"id", runners + "/Proton-GE Latest"}, {"label", "Proton-GE Latest"}, {"installed", false}},
    });
    const QString inhibitor = m_dataRoot + "/bin/systemd-inhibit";
    for (const auto &path : {exe, umu, proton + "/proton", inhibitor}) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) { failure("Could not create fixtures."); return; }
        file.write("#!/bin/sh\nexit 99\n");
        file.close();
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }
    qputenv("PATH", (m_dataRoot + "/bin:").toUtf8() + qgetenv("PATH"));
    const QJsonObject defaults{{"arguments", "--forest-smoke"}, {"tags", QJsonArray{"smoke-test"}},
        {"environment", QJsonObject{{"FOREST_SMOKE", "1"}}}, {"prefix", m_dataRoot + "/shared-prefix"},
        {"mangohud", true}, {"prefer_sdl", true}, {"no_sleep", true},
        {"desktop_shortcut", true}, {"app_menu_shortcut", true}};
    m_backend->request("save_settings", {{"settings", QJsonObject{{"default_proton", "Proton-CachyOS Latest"}, {"new_game_defaults", defaults}}}},
        [this, exe, failure](const QJsonObject &data) {
        m_bootstrap.insert("settings", data.value("settings"));
        GameDialog dialog({}, m_bootstrap, this);
        dialog.setExecutablePath(exe);
        if (!dialog.findChild<QCheckBox *>("onlineFixCheck")) { failure("Online-fix control missing."); return; }
        const auto game = dialog.gameData();
        if (game.value("arguments").toString() != "--forest-smoke"
            || !game.value("tags").toArray().contains("smoke-test")) {
            failure("Saved defaults did not prefill Add game."); return;
        }
        m_backend->request("save_game", {{"game", game}, {"shortcut_context", m_shortcutContext}}, [this, failure](const QJsonObject &data) {
            if (!data.value("warning").toString().isEmpty() || data.value("shortcuts").toArray().size() != 2) {
                failure("Shortcut creation failed."); return;
            }
            const auto game = data.value("game").toObject();
            m_bootstrap.insert("games", QJsonArray{game});
            populateLibrary(game.value("id").toString());
            if (selectedGame().value("id") != game.value("id")) { failure("Library selection failed."); return; }
            m_backend->request("preview_launch", {{"id", game.value("id")}},
                [this, game, failure](const QJsonObject &plan) {
                if (plan.value("mode").toString() != "umu"
                    || !plan.value("command").toArray().contains("--forest-smoke")
                    || plan.value("environment").toObject().value("FOREST_SMOKE").toString() != "1"
                    || plan.value("prefix").toString() != m_dataRoot + "/shared-prefix"
                    || plan.value("environment").toObject().value("MANGOHUD").toString() != "1"
                    || plan.value("environment").toObject().value("PROTON_PREFER_SDL").toString() != "1"
                    || plan.value("command").toArray().first().toString() != m_dataRoot + "/bin/systemd-inhibit") {
                    failure("Defaults did not reach the launch plan."); return;
                }
                m_backend->request("delete_game", {{"id", game.value("id")}},
                    [this, failure, game](const QJsonObject &) {
                    const auto filename = "/io.github.Benaeo.forest-launcher.game-" + game.value("id").toString() + ".desktop";
                    if (QFileInfo::exists(m_dataRoot + "/desktop" + filename) || QFileInfo::exists(m_dataRoot + "/applications" + filename)) {
                        failure("Shortcut cleanup failed."); return;
                    }
                    m_backend->request("list_games", {}, [failure](const QJsonObject &data) {
                        if (!data.value("games").toArray().isEmpty()) { failure("Delete failed."); return; }
                        qInfo("Forest GUI/backend smoke test passed (no game executed).");
                        QApplication::exit(0);
                    }, failure);
                }, failure);
            }, failure);
        }, failure);
    }, failure);
}
