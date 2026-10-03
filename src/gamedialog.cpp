#include "gamedialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QTabWidget>
#include <QVBoxLayout>

GameDialog::GameDialog(const QJsonObject &game, const QJsonObject &bootstrap, QWidget *parent)
    : QDialog(parent), m_original(game) {
    setWindowTitle(game.value("id").toString().isEmpty() ? "Add game" : "Edit game");
    setMinimumWidth(570);
    auto *layout = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    auto *general = new QWidget(tabs);
    auto *form = new QFormLayout(general);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_title = new QLineEdit(game.value("title").toString(), this);
    m_title->setObjectName("gameTitle");
    m_kind = new QComboBox(this);
    m_kind->addItem("Windows game (Proton / UMU)", "windows");
    m_kind->addItem("Native Linux game", "native");
    m_kind->addItem("Steam library game", "steam");
    m_kind->setCurrentIndex(qMax(0, m_kind->findData(game.value("kind").toString("windows"))));
    m_pathLabel = new QLabel(this);
    m_path = new QLineEdit(game.value("path").toString(), this);
    m_path->setObjectName("gameExecutable");
    m_browse = new QPushButton("Browse…", this);
    auto *pathRow = new QHBoxLayout;
    pathRow->addWidget(m_path, 1);
    pathRow->addWidget(m_browse);
    m_prefixLabel = new QLabel("Wine prefix", this);
    m_prefix = new QLineEdit(game.value("prefix").toString(), this);
    m_prefix->setPlaceholderText("Automatic: a dedicated prefix for this game");
    m_prefixBrowse = new QPushButton("Browse…", this);
    auto *prefixRow = new QHBoxLayout;
    prefixRow->addWidget(m_prefix, 1);
    prefixRow->addWidget(m_prefixBrowse);
    m_protonLabel = new QLabel("Proton build", this);
    m_proton = new QComboBox(this);
    m_proton->addItem("Use application default", "default");
    m_proton->addItem("Automatic (prefer installed GE-Proton)", "auto");
    for (const auto &value : bootstrap.value("protons").toArray()) {
        const auto build = value.toObject();
        m_proton->addItem(build.value("label").toString(), build.value("id").toString());
    }
    const auto proton = game.value("proton").toString("default");
    if (m_proton->findData(proton) < 0) m_proton->addItem(proton, proton);
    m_proton->setCurrentIndex(m_proton->findData(proton));
    m_arguments = new QLineEdit(game.value("arguments").toString(), this);
    m_arguments->setPlaceholderText("Optional arguments, e.g. -fullscreen");
    QStringList tags;
    for (const auto &value : game.value("tags").toArray())
        if (value.toString() != "online-fix") tags << value.toString();
    m_tags = new QLineEdit(tags.join(", "), this);
    m_tags->setPlaceholderText("Optional comma-separated tags");
    m_onlineFix = new QCheckBox("online-fix — Steam / Spacewar", this);
    m_onlineFix->setObjectName("onlineFixCheck");
    m_onlineFix->setToolTip("Use native Steam and Proton with App ID 480 and existing OnlineFix DLLs. No game files are changed.");
    m_onlineFix->setChecked(game.value("tags").toArray().contains("online-fix"));
    form->addRow("Title", m_title);
    form->addRow("Game type", m_kind);
    form->addRow(m_pathLabel, pathRow);
    form->addRow(m_prefixLabel, prefixRow);
    form->addRow(m_protonLabel, m_proton);
    form->addRow("Arguments", m_arguments);
    form->addRow("Tags", m_tags);
    form->addRow(QString(), m_onlineFix);
    tabs->addTab(general, "Game");

    auto *advanced = new QWidget(tabs);
    auto *advancedLayout = new QVBoxLayout(advanced);
    auto *hint = new QLabel("Optional environment variables, one KEY=value per line.\nUse the prefix and Proton fields for those settings.", advanced);
    hint->setWordWrap(true);
    advancedLayout->addWidget(hint);
    m_environment = new QPlainTextEdit(advanced);
    m_environment->setPlaceholderText("DXVK_HUD=fps\nPROTON_ENABLE_WAYLAND=1");
    QStringList environment;
    const auto variables = game.value("environment").toObject();
    for (auto it = variables.begin(); it != variables.end(); ++it)
        environment << it.key() + "=" + it.value().toString();
    m_environment->setPlainText(game.value("environment").isString()
        ? game.value("environment").toString() : environment.join("\n"));
    advancedLayout->addWidget(m_environment);
    tabs->addTab(advanced, "Environment");
    layout->addWidget(tabs);
    m_error = new QLabel(this);
    m_error->setTextFormat(Qt::PlainText);
    m_error->setWordWrap(true);
    m_error->setVisible(false);
    layout->addWidget(m_error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &GameDialog::validateAndAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    connect(m_kind, &QComboBox::currentIndexChanged, this, [this] { updateKind(); });
    connect(m_browse, &QPushButton::clicked, this, [this] {
        const auto filter = m_kind->currentData() == "windows"
            ? "Windows executables (*.exe *.EXE *.bat *.msi *.lnk);;All files (*)" : "All files (*)";
        const auto path = QFileDialog::getOpenFileName(this, "Choose game executable", m_path->text(), filter);
        if (!path.isEmpty()) setExecutablePath(path);
    });
    connect(m_prefixBrowse, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getExistingDirectory(this, "Choose Wine prefix", m_prefix->text());
        if (!path.isEmpty()) m_prefix->setText(path);
    });
    updateKind();
}

void GameDialog::setExecutablePath(const QString &path) {
    m_path->setText(path);
    const auto suffix = QFileInfo(path).suffix().toLower();
    m_kind->setCurrentIndex((suffix == "exe" || suffix == "msi" || suffix == "bat" || suffix == "lnk") ? 0 : 1);
    if (m_title->text().trimmed().isEmpty()) m_title->setText(QFileInfo(path).completeBaseName());
}

void GameDialog::updateKind() {
    const bool steam = m_kind->currentData() == "steam";
    const bool windows = m_kind->currentData() == "windows";
    m_pathLabel->setText(steam ? "Steam App ID" : "Executable");
    m_path->setPlaceholderText(steam ? "e.g. 480" : "Path to the game executable");
    m_browse->setEnabled(!steam);
    m_prefix->setEnabled(windows);
    m_prefixBrowse->setEnabled(windows);
    m_prefixLabel->setEnabled(windows);
    m_proton->setEnabled(windows);
    m_protonLabel->setEnabled(windows);
    m_onlineFix->setEnabled(windows);
    m_environment->setEnabled(!steam);
    if (!windows) m_onlineFix->setChecked(false);
}

QJsonObject GameDialog::gameData() const {
    auto game = m_original;
    game.insert("title", m_title->text().trimmed());
    game.insert("kind", m_kind->currentData().toString());
    game.insert("path", m_path->text().trimmed());
    game.insert("prefix", m_prefix->text().trimmed());
    game.insert("proton", m_proton->currentData().toString());
    game.insert("arguments", m_arguments->text());
    game.insert("environment", m_environment->isEnabled() ? m_environment->toPlainText() : QString());
    QJsonArray tags;
    for (const auto &tag : m_tags->text().split(",", Qt::SkipEmptyParts)) {
        const auto value = tag.trimmed().toLower();
        if (!value.isEmpty() && value != "online-fix" && !tags.contains(value)) tags.append(value);
    }
    if (m_onlineFix->isEnabled() && m_onlineFix->isChecked()) tags.append("online-fix");
    game.insert("tags", tags);
    return game;
}

void GameDialog::validateAndAccept() {
    QString error;
    if (m_title->text().trimmed().isEmpty()) error = "Enter a title.";
    else if (m_kind->currentData() == "steam") {
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
