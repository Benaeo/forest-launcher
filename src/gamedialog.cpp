#include "gamedialog.h"
#include "gameoptionswidget.h"
#include "dialogbuttons.h"
#include "artworkdialog.h"
#if FOREST_ARTWORK_PREVIEW
#include "artworkpreview.h"
#endif

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>
#include <QTimer>
#include <QShowEvent>
#include <QStyle>

GameDialog::GameDialog(const QJsonObject &game, const QJsonObject &bootstrap, QWidget *parent)
    : QDialog(parent), m_original(game), m_bootstrap(bootstrap), m_artwork(game.value("artwork").toObject()) {
    const bool creating = game.value("id").toString().isEmpty();
    m_creating = creating;
    auto initial = creating
        ? bootstrap.value("settings").toObject().value("new_game_defaults").toObject() : QJsonObject();
    for (auto it = game.begin(); it != game.end(); ++it) initial.insert(it.key(), it.value());
    if (!creating && initial.value("kind").toString() == "windows" && initial.value("prefix").toString().isEmpty()) {
        // Display the effective legacy path instead of implying the new shared default.
        initial.insert("prefix", bootstrap.value("settings").toObject().value("prefix_root").toString()
            + "/" + game.value("id").toString());
    }
    setWindowTitle(creating ? "Add game" : "Edit game");
    setMinimumWidth(570);
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_title = new QLineEdit(initial.value("title").toString(), this);
    m_title->setObjectName("gameTitle");
    m_pathLabel = new QLabel(this);
    m_path = new QLineEdit(initial.value("path").toString(), this);
    m_path->setObjectName("gameExecutable");
    m_browse = new QPushButton("Browse…", this);
    auto *pathRow = new QHBoxLayout;
    pathRow->addWidget(m_path, 1);
    pathRow->addWidget(m_browse);
    form->addRow("Title", m_title);
    form->addRow(m_pathLabel, pathRow);
    layout->addLayout(form);
    m_options = new GameOptionsWidget(initial, bootstrap, this);
    m_icon = new QPushButton(this);
    m_icon->setObjectName("gameIconButton");
    m_icon->setFixedSize(96, 96);
    m_icon->setIconSize(QSize(80, 80));
    m_icon->setToolTip("Choose an extracted, local, or SteamGridDB icon and game artwork.");
    m_options->addIconControl(m_icon);
    layout->addWidget(m_options);
    updateIcon();
    m_iconDebounce = new QTimer(this);
    m_iconDebounce->setSingleShot(true);
    m_iconDebounce->setInterval(350);
    connect(m_iconDebounce, &QTimer::timeout, this, &GameDialog::extractInitialIcon);
    connect(m_path, &QLineEdit::textChanged, this, [this] {
        ++m_iconRevision;
        if (m_iconBackend) { delete m_iconBackend; m_iconBackend = nullptr; }
        setIconBusy(false);
        m_iconDebounce->start();
    });
    connect(m_icon, &QPushButton::clicked, this, &GameDialog::chooseIconSource);
    m_error = new QLabel(this);
    m_error->setTextFormat(Qt::PlainText);
    m_error->setWordWrap(true);
    m_error->setVisible(false);
    layout->addWidget(m_error);
    auto *buttons = new WideDialogButtons(this);
    connect(buttons, &QDialogButtonBox::accepted, this, &GameDialog::validateAndAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    connect(m_options->kindSelector(), &QComboBox::currentIndexChanged, this, [this] { updateKind(); });
    connect(m_browse, &QPushButton::clicked, this, [this] {
        const auto filter = m_options->kind() == "windows"
            ? "Windows executables (*.exe *.EXE *.bat *.msi *.lnk);;All files (*)" : "All files (*)";
        const auto path = QFileDialog::getOpenFileName(this, "Choose game executable", m_path->text(), filter);
        if (!path.isEmpty()) setExecutablePath(path);
    });
    updateKind();
}

void GameDialog::setExecutablePath(const QString &path) {
    m_path->setText(path);
    const auto suffix = QFileInfo(path).suffix().toLower();
    m_options->setKind((suffix == "exe" || suffix == "msi" || suffix == "bat" || suffix == "lnk") ? "windows" : "native");
    if (m_title->text().trimmed().isEmpty()) m_title->setText(QFileInfo(path).completeBaseName());
}

void GameDialog::updateKind() {
    const bool steam = m_options->kind() == "steam";
    m_pathLabel->setText(steam ? "Steam App ID" : "Executable");
    m_path->setPlaceholderText(steam ? "e.g. 480" : "Path to the game executable");
    m_browse->setEnabled(!steam);
}

QJsonObject GameDialog::gameData() const {
    auto game = m_original;
    const auto options = m_options->optionsData();
    for (auto it = options.begin(); it != options.end(); ++it) game.insert(it.key(), it.value());
    game.insert("title", m_title->text().trimmed());
    game.insert("path", m_path->text().trimmed());
    if (!m_artwork.isEmpty()) game.insert("artwork", m_artwork);
    return game;
}

void GameDialog::validateAndAccept() {
    // A fast Save must not bypass the initial asynchronous extraction.
    if (!m_iconBusy) extractInitialIcon();
    if (m_iconBusy) return;
    QString error;
    if (m_iconBusy) error = "Wait for icon extraction to finish, or choose an icon manually.";
    else if (m_title->text().trimmed().isEmpty()) error = "Enter a title.";
    else if (m_options->kind() == "steam") {
        if (!QRegularExpression("^[0-9]+$").match(m_path->text().trimmed()).hasMatch()
            || m_path->text().trimmed().toULongLong() == 0) error = "Enter a positive Steam App ID.";
    } else if (!QFileInfo(m_path->text().trimmed()).isFile()) error = "Choose an existing game executable.";
    if (!error.isEmpty()) {
        m_error->setText(error);
        m_error->setVisible(true);
        return;
    }
    accept();
}

void GameDialog::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    m_iconDebounce->start();
}

void GameDialog::updateIcon() {
    const auto path = m_artwork.value("icon").toString();
    const auto image = path.isEmpty() ? QImage() : readArtworkImage(path);
    m_icon->setIcon(image.isNull() ? QIcon::fromTheme("applications-games", style()->standardIcon(QStyle::SP_ComputerIcon))
                                 : QIcon(QPixmap::fromImage(image)));
}

void GameDialog::setIconBusy(bool busy) {
    m_iconBusy = busy;
    if (auto *buttons = findChild<QDialogButtonBox *>("dialogFooterButtons"))
        buttons->button(QDialogButtonBox::Save)->setEnabled(!busy);
    m_icon->setToolTip(busy ? "Extracting icon… Click to choose manually." : "Choose an extracted, local, or SteamGridDB icon and artwork.");
}

void GameDialog::extractInitialIcon() {
    const auto path = m_path->text().trimmed();
    const auto frontend = m_bootstrap.value("frontend").toObject();
    if (!isVisible() || !m_creating || !m_artwork.isEmpty() || m_options->kind() == "steam"
        || !QFileInfo(path).isFile() || frontend.value("backend").toString().isEmpty()
        || m_extractionAttempted.contains(path)) return;
    m_extractionAttempted.insert(path);
    extractIcon();
}

void GameDialog::extractIcon() {
    const auto path = m_path->text().trimmed();
    const auto frontend = m_bootstrap.value("frontend").toObject();
    if (!QFileInfo(path).isFile()) { extractionFailed("Choose an existing executable to extract its icon."); return; }
    const int revision = m_iconRevision;
    if (m_iconBackend) delete m_iconBackend;
    m_iconBackend = new BackendClient(frontend.value("backend").toString(), frontend.value("data_root").toString(), this);
    setIconBusy(true);
    m_iconBackend->request("extract_icon", {{"path", path}}, [this, revision](const QJsonObject &data) {
        if (revision != m_iconRevision) return;
        importArtworkImage(m_iconBackend, data.value("path").toString(), [this, revision](const QJsonObject &normalized) {
            if (revision != m_iconRevision) return;
            m_artwork.insert("extracted_icon", normalized.value("path"));
            m_artwork.insert("icon", normalized.value("path"));
            setIconBusy(false);
            updateIcon();
        }, [this, revision](const QString &error) { if (revision == m_iconRevision) extractionFailed(error); });
    }, [this, revision](const QString &error) { if (revision == m_iconRevision) extractionFailed(error); });
}

void GameDialog::extractionFailed(const QString &error) {
    setIconBusy(false);
    if (!isVisible()) return;
    ArtworkQuestion prompt("Icon extraction failed", "Icon extraction failed. Want to proceed with SteamGridDB?\n\n" + error, this);
    prompt.setObjectName("iconExtractionFailedDialog");
    if (prompt.exec() != QDialog::Accepted) return;
    const auto key = m_bootstrap.value("settings").toObject().value("steamgriddb_api_key").toString();
    if (!key.isEmpty()) { chooseArtwork(true); return; }
    ArtworkKeyDialog keyPrompt(this);
    if (keyPrompt.exec() != QDialog::Accepted) return;
    setIconBusy(true);
    m_iconBackend->request("save_settings", {{"settings", QJsonObject{{"steamgriddb_api_key", keyPrompt.key->text().trimmed()}}}},
        [this](const QJsonObject &data) {
            m_bootstrap.insert("settings", data.value("settings"));
            setIconBusy(false);
            if (isVisible()) chooseArtwork(true);
        }, [this](const QString &failure) { setIconBusy(false); m_error->setText(failure); m_error->show(); });
}

void GameDialog::chooseIconSource() {
    m_iconDebounce->stop();
    m_extractionAttempted.insert(m_path->text().trimmed());
    ++m_iconRevision;
    if (m_iconBackend) { m_iconBackend->deleteLater(); m_iconBackend = nullptr; }
    setIconBusy(false);
    IconSourceDialog prompt(this);
    const int choice = prompt.exec();
    if (choice == IconSourceDialog::Extracted) {
        extractIcon();
    } else if (choice == IconSourceDialog::File) {
        const auto path = QFileDialog::getOpenFileName(this, "Choose icon", {}, "Images (*.png *.jpg *.jpeg *.webp *.ico);;All files (*)");
        if (path.isEmpty()) return;
        const auto frontend = m_bootstrap.value("frontend").toObject();
        m_iconBackend = new BackendClient(frontend.value("backend").toString(), frontend.value("data_root").toString(), this);
        setIconBusy(true);
        importArtworkImage(m_iconBackend, path, [this](const QJsonObject &data) {
            m_artwork.insert("icon", data.value("path"));
            setIconBusy(false);
            updateIcon();
        }, [this](const QString &error) { setIconBusy(false); m_error->setText(error); m_error->show(); });
    } else if (choice == IconSourceDialog::SteamGridDB) {
        chooseArtwork(true);
    }
}

bool GameDialog::chooseArtwork(bool startSearch, bool steamRequested) {
    m_iconDebounce->stop();
    m_extractionAttempted.insert(m_path->text().trimmed());
    ++m_iconRevision;
    if (m_iconBackend) { m_iconBackend->deleteLater(); m_iconBackend = nullptr; }
    setIconBusy(false);
    ArtworkDialog dialog(m_bootstrap, m_title->text(), m_artwork, isVisible() ? this : parentWidget(), steamRequested);
#if FOREST_ARTWORK_PREVIEW
    if (steamRequested) {
        auto *preview = new ArtworkPreview(&dialog);
        preview->setArtwork(m_artwork);
        dialog.addPreview(preview);
        dialog.selectionChanged = [preview](const QJsonObject &artwork) { preview->setArtwork(artwork); };
    }
#endif
    if (startSearch) dialog.startSearch();
    const bool accepted = dialog.exec() == QDialog::Accepted;
    m_bootstrap.insert("settings", dialog.settingsData());
    if (accepted) {
        m_artwork = dialog.artworkData();
        updateIcon();
    }
    return accepted;
}
