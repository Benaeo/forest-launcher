#include "mainwindow.h"
#include "backendclient.h"
#include "gamedialog.h"

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
#include <QMenuBar>
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
    m_previewAction = new QAction("Preview launch…", this);
    m_settingsAction = new QAction(icon("configure", QStyle::SP_FileDialogDetailedView), "Settings…", this);
    auto *libraryMenu = menuBar()->addMenu("&Library");
    libraryMenu->addActions({m_addAction, m_editAction, m_removeAction});
    libraryMenu->addSeparator();
    libraryMenu->addActions({m_playAction, m_previewAction});
    libraryMenu->addSeparator();
    auto *quit = libraryMenu->addAction("Quit");
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);
    menuBar()->addMenu("&Tools")->addAction(m_settingsAction);
    auto *toolbar = addToolBar("Library");
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar->addAction(m_addAction);
    toolbar->addAction(m_editAction);
    toolbar->addSeparator();
    toolbar->addAction(m_settingsAction);

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

    connect(search, &QLineEdit::textChanged, m_proxy, &QSortFilterProxyModel::setFilterFixedString);
    connect(m_library->selectionModel(), &QItemSelectionModel::currentChanged, this, [this] { updateSelection(); });
    connect(m_library, &QListView::doubleClicked, this, [this] { launchSelected(); });
    connect(m_library, &QListView::customContextMenuRequested, this, [this](const QPoint &position) {
        const auto index = m_library->indexAt(position);
        if (index.isValid()) m_library->setCurrentIndex(index);
        QMenu menu(this);
        menu.addActions({m_playAction, m_editAction, m_previewAction});
        menu.addSeparator();
        menu.addAction(m_removeAction);
        menu.exec(m_library->viewport()->mapToGlobal(position));
    });
    connect(m_addAction, &QAction::triggered, this, [this] { editGame({}); });
    connect(m_editAction, &QAction::triggered, this, [this] { editGame(selectedGame()); });
    connect(m_removeAction, &QAction::triggered, this, &MainWindow::removeSelected);
    connect(m_playAction, &QAction::triggered, this, &MainWindow::launchSelected);
    connect(m_previewAction, &QAction::triggered, this, &MainWindow::previewSelected);
    connect(m_settingsAction, &QAction::triggered, this, &MainWindow::showSettings);
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
        populateLibrary(selection);
        setBusy(false);
        statusBar()->showMessage(QString("%1 games · Forest %2")
            .arg(m_model->rowCount()).arg(QCoreApplication::applicationVersion()));
        if (m_smokeTest && !m_smokeStarted) {
            m_smokeStarted = true;
            runSmokeTest();
        }
    }, [this](const QString &error) { setBusy(false); showError(error); });
}

void MainWindow::populateLibrary(const QString &selectedId) {
    m_model->clear();
    const auto icon = QIcon::fromTheme("applications-games", style()->standardIcon(QStyle::SP_ComputerIcon));
    QModelIndex selected;
    for (const auto &value : m_bootstrap.value("games").toArray()) {
        const auto game = value.toObject();
        auto *item = new QStandardItem(icon, game.value("title").toString());
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
    const bool available = !m_busy && !game.isEmpty();
    for (auto *action : {m_editAction, m_removeAction, m_playAction, m_previewAction}) action->setEnabled(available);
    for (auto *button : {m_play, m_edit, m_log}) button->setEnabled(available);
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
    if (dialog.exec() != QDialog::Accepted) return;
    const auto updated = dialog.gameData();
    setBusy(true);
    m_backend->request("save_game", {{"game", updated}}, [this](const QJsonObject &data) {
        refresh(data.value("game").toObject().value("id").toString());
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
    if (dialog.exec() != QDialog::Accepted) return;
    const auto game = dialog.gameData();
    setBusy(true);
    m_backend->request("save_game", {{"game", game}}, [this](const QJsonObject &data) {
        refresh(data.value("game").toObject().value("id").toString());
    }, [this, game](const QString &error) { setBusy(false); showError(error); editGame(game); });
}

void MainWindow::removeSelected() {
    const auto game = selectedGame();
    if (m_busy || game.isEmpty()) return;
    QMessageBox prompt(QMessageBox::Question, "Remove game",
        "Remove “" + game.value("title").toString() + "” from the library?\n\nGame files and prefixes will not be deleted.",
        QMessageBox::Yes | QMessageBox::No, this);
    prompt.setTextFormat(Qt::PlainText);
    prompt.setDefaultButton(QMessageBox::No);
    if (prompt.exec() != QMessageBox::Yes) return;
    setBusy(true);
    m_backend->request("delete_game", {{"id", game.value("id")}}, [this](const QJsonObject &) { refresh(); },
        [this](const QString &error) { setBusy(false); showError(error); });
}

void MainWindow::launchSelected() {
    const auto game = selectedGame();
    if (m_busy || game.isEmpty()) return;
    setBusy(true);
    statusBar()->showMessage("Starting " + game.value("title").toString() + "…");
    m_backend->request("launch_game", {{"id", game.value("id")}}, [this, game](const QJsonObject &) {
        if (m_bootstrap.value("settings").toObject().value("close_after_launch").toBool()) {
            close();
            return;
        }
        refresh(game.value("id").toString());
        statusBar()->showMessage("Launch started.", 5000);
    }, [this](const QString &error) { setBusy(false); statusBar()->showMessage("Launch failed."); showError(error); });
}

void MainWindow::previewSelected() {
    const auto game = selectedGame();
    if (m_busy || game.isEmpty()) return;
    setBusy(true);
    m_backend->request("preview_launch", {{"id", game.value("id")}}, [this](const QJsonObject &data) {
        setBusy(false);
        QDialog dialog(this);
        dialog.setWindowTitle("Launch preview — nothing executed");
        dialog.resize(740, 480);
        auto *layout = new QVBoxLayout(&dialog);
        auto *text = new QPlainTextEdit(&dialog);
        text->setReadOnly(true);
        text->setPlainText(QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Indented)));
        layout->addWidget(text);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(buttons);
        dialog.exec();
    }, [this](const QString &error) { setBusy(false); showError(error); });
}

void MainWindow::showSettings() {
    QDialog dialog(this);
    dialog.setWindowTitle("Settings");
    dialog.setMinimumWidth(600);
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    const auto settings = m_bootstrap.value("settings").toObject();
    auto *prefix = new QLineEdit(settings.value("prefix_root").toString(), &dialog);
    auto *prefixBrowse = new QPushButton("Browse…", &dialog);
    auto *prefixRow = new QHBoxLayout;
    prefixRow->addWidget(prefix, 1);
    prefixRow->addWidget(prefixBrowse);
    form->addRow("Default prefix folder", prefixRow);
    auto *proton = new QComboBox(&dialog);
    proton->addItem("Automatic (prefer installed GE-Proton)", "auto");
    for (const auto &value : m_bootstrap.value("protons").toArray()) {
        const auto build = value.toObject();
        proton->addItem(build.value("label").toString(), build.value("id").toString());
    }
    const auto selected = settings.value("default_proton").toString();
    if (proton->findData(selected) < 0) proton->addItem(selected, selected);
    proton->setCurrentIndex(proton->findData(selected));
    form->addRow("Default Proton", proton);
    auto *umu = new QLineEdit(settings.value("umu_program").toString(), &dialog);
    umu->setPlaceholderText("Automatic: find installed umu-run");
    auto *umuBrowse = new QPushButton("Browse…", &dialog);
    auto *umuRow = new QHBoxLayout;
    umuRow->addWidget(umu, 1);
    umuRow->addWidget(umuBrowse);
    form->addRow("UMU executable", umuRow);
    auto *closeAfter = new QCheckBox("Close Forest after launching a game", &dialog);
    closeAfter->setChecked(settings.value("close_after_launch").toBool());
    form->addRow(QString(), closeAfter);
    layout->addLayout(form);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(prefixBrowse, &QPushButton::clicked, &dialog, [&dialog, prefix] {
        const auto path = QFileDialog::getExistingDirectory(&dialog, "Choose prefix folder", prefix->text());
        if (!path.isEmpty()) prefix->setText(path);
    });
    connect(umuBrowse, &QPushButton::clicked, &dialog, [&dialog, umu] {
        const auto path = QFileDialog::getOpenFileName(&dialog, "Choose umu-run executable", umu->text());
        if (!path.isEmpty()) umu->setText(path);
    });
    if (dialog.exec() != QDialog::Accepted) return;
    const QJsonObject values{{"prefix_root", prefix->text()}, {"default_proton", proton->currentData().toString()},
        {"umu_program", umu->text()}, {"close_after_launch", closeAfter->isChecked()}};
    setBusy(true);
    m_backend->request("save_settings", {{"settings", values}}, [this](const QJsonObject &) { refresh(); },
        [this](const QString &error) { setBusy(false); showError(error); });
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
    const QString umu = m_dataRoot + "/mock-umu";
    for (const auto &path : {exe, umu}) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) { failure("Could not create fixtures."); return; }
        file.write("#!/bin/sh\nexit 99\n");
        file.close();
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }
    GameDialog dialog({}, m_bootstrap, this);
    dialog.setExecutablePath(exe);
    if (!dialog.findChild<QCheckBox *>("onlineFixCheck")) { failure("Online-fix control missing."); return; }
    const auto game = dialog.gameData();
    m_backend->request("save_settings", {{"settings", QJsonObject{{"umu_program", umu}}}},
        [this, game, failure](const QJsonObject &) {
        m_backend->request("save_game", {{"game", game}}, [this, failure](const QJsonObject &data) {
            const auto game = data.value("game").toObject();
            m_bootstrap.insert("games", QJsonArray{game});
            populateLibrary(game.value("id").toString());
            if (selectedGame().value("id") != game.value("id")) { failure("Library selection failed."); return; }
            m_backend->request("preview_launch", {{"id", game.value("id")}},
                [this, game, failure](const QJsonObject &plan) {
                if (plan.value("mode").toString() != "umu") { failure("Wrong launch mode."); return; }
                m_backend->request("delete_game", {{"id", game.value("id")}},
                    [this, failure](const QJsonObject &) {
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
