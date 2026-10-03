#include "gamedialog.h"
#include "gameoptionswidget.h"
#include "dialogbuttons.h"

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

GameDialog::GameDialog(const QJsonObject &game, const QJsonObject &bootstrap, QWidget *parent)
    : QDialog(parent), m_original(game) {
    const bool creating = game.value("id").toString().isEmpty();
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
    layout->addWidget(m_options);
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
    return game;
}

void GameDialog::validateAndAccept() {
    QString error;
    if (m_title->text().trimmed().isEmpty()) error = "Enter a title.";
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
