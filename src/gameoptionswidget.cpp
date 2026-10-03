#include "gameoptionswidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

GameOptionsWidget::GameOptionsWidget(const QJsonObject &options, const QJsonObject &bootstrap, QWidget *parent,
                                     bool defaultsEditor)
    : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *tabs = new QTabWidget(this);
    auto *launch = new QWidget(tabs);
    auto *form = new QFormLayout(launch);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_kind = new QComboBox(this);
    m_kind->setObjectName("gameKind");
    m_kind->addItem("Windows game (Proton / UMU)", "windows");
    m_kind->addItem("Native Linux game", "native");
    m_kind->addItem("Steam library game", "steam");
    m_kind->setCurrentIndex(qMax(0, m_kind->findData(options.value("kind").toString("windows"))));
    m_prefix = new QLineEdit(options.value("prefix").toString(), this);
    m_prefix->setObjectName("gamePrefix");
    const auto builtInPrefix = QDir::homePath() + "/Games/forest-launcher/default";
    auto sharedPrefix = bootstrap.value("settings").toObject().value("new_game_defaults").toObject().value("prefix").toString();
    if (defaultsEditor || sharedPrefix.isEmpty()) sharedPrefix = builtInPrefix;
    m_prefix->setPlaceholderText("Shared default: " + sharedPrefix);
    m_prefix->setToolTip(defaultsEditor
        ? "All new Windows games share this prefix unless overridden. Leave blank to restore " + builtInPrefix + "."
        : "Override the shared prefix for this game, or leave blank to use " + sharedPrefix + ".");
    m_prefixBrowse = new QPushButton("Browse…", this);
    auto *prefixRow = new QHBoxLayout;
    prefixRow->addWidget(m_prefix, 1);
    prefixRow->addWidget(m_prefixBrowse);
    m_proton = new QComboBox(this);
    m_proton->setObjectName("gameProton");
    const QStringList latestNames{"Proton-CachyOS Latest", "Proton-GE Latest"};
    for (const auto &name : latestNames) {
        QJsonObject build;
        for (const auto &value : bootstrap.value("protons").toArray()) {
            if (value.toObject().value("label").toString() == name) { build = value.toObject(); break; }
        }
        m_proton->addItem(name, build.value("id").toString(name));
        m_proton->setItemData(m_proton->count() - 1, build.value("installed").toBool()
            ? build.value("id").toString() : "Not installed. Expected at " + QDir::homePath()
                + "/.local/share/Steam/compatibilitytools.d/" + name, Qt::ToolTipRole);
    }
    auto proton = options.value("proton").toString("default");
    if (proton.isEmpty() || proton == "default")
        proton = bootstrap.value("settings").toObject().value("default_proton").toString(latestNames.first());
    auto index = m_proton->findData(proton);
    if (index < 0) index = m_proton->findText(proton);
    m_proton->setCurrentIndex(index);
    if (index < 0) m_preservedProton = proton;
    m_proton->setPlaceholderText("Existing selection (unchanged)");
    auto *legacyProton = new QLabel("Existing selection: " + proton + "\nPreserved until you choose a Latest runner.", this);
    legacyProton->setObjectName("legacyProtonSelection");
    legacyProton->setTextFormat(Qt::PlainText);
    legacyProton->setWordWrap(true);
    legacyProton->setVisible(index < 0);
    connect(m_proton, &QComboBox::currentIndexChanged, legacyProton, [legacyProton](int selected) {
        legacyProton->setVisible(selected < 0);
    });
    m_arguments = new QLineEdit(options.value("arguments").toString(), this);
    m_arguments->setObjectName("gameArguments");
    m_arguments->setPlaceholderText("Optional arguments, e.g. -fullscreen");
    QStringList tags;
    for (const auto &value : options.value("tags").toArray())
        if (value.toString() != "online-fix") tags << value.toString();
    m_tags = new QLineEdit(tags.join(", "), this);
    m_tags->setObjectName("gameTags");
    m_tags->setPlaceholderText("Optional comma-separated tags");
    m_onlineFix = new QCheckBox("online-fix — Steam / Spacewar", this);
    m_onlineFix->setObjectName("onlineFixCheck");
    m_onlineFix->setToolTip("Use native Steam and Proton with App ID 480 and existing OnlineFix DLLs. No game files are changed.");
    m_onlineFix->setChecked(options.value("tags").toArray().contains("online-fix"));
    form->addRow("Game type", m_kind);
    form->addRow("Wine prefix", prefixRow);
    form->addRow(defaultsEditor ? "Default Proton" : "Proton build", m_proton);
    form->addRow(QString(), legacyProton);
    form->addRow("Arguments", m_arguments);
    form->addRow("Tags", m_tags);
    form->addRow(QString(), m_onlineFix);
    tabs->addTab(launch, "Launch");
    auto *advanced = new QWidget(tabs);
    auto *advancedLayout = new QVBoxLayout(advanced);
    auto *hint = new QLabel("Optional environment variables, one KEY=value per line.\nUse the prefix and Proton fields for those settings.", advanced);
    hint->setWordWrap(true);
    advancedLayout->addWidget(hint);
    m_environment = new QPlainTextEdit(advanced);
    m_environment->setObjectName("gameEnvironment");
    m_environment->setPlaceholderText("DXVK_HUD=fps\nPROTON_ENABLE_WAYLAND=1");
    QStringList environment;
    const auto variables = options.value("environment").toObject();
    for (auto it = variables.begin(); it != variables.end(); ++it)
        environment << it.key() + "=" + it.value().toString();
    m_environment->setPlainText(options.value("environment").isString()
        ? options.value("environment").toString() : environment.join("\n"));
    advancedLayout->addWidget(m_environment);
    tabs->addTab(advanced, "Environment");
    layout->addWidget(tabs);
    connect(m_kind, &QComboBox::currentIndexChanged, this, [this] { updateKind(); });
    connect(m_prefixBrowse, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getExistingDirectory(this, "Choose Wine prefix", m_prefix->text());
        if (!path.isEmpty()) m_prefix->setText(path);
    });
    updateKind();
}

QString GameOptionsWidget::kind() const { return m_kind->currentData().toString(); }
QString GameOptionsWidget::protonSelection() const {
    return m_proton->currentIndex() < 0 ? m_preservedProton : m_proton->currentData().toString();
}

void GameOptionsWidget::setKind(const QString &kind) {
    const auto index = m_kind->findData(kind);
    if (index >= 0) m_kind->setCurrentIndex(index);
}

void GameOptionsWidget::updateKind() {
    const bool windows = kind() == "windows";
    m_prefix->setEnabled(windows);
    m_prefixBrowse->setEnabled(windows);
    m_proton->setEnabled(windows);
    m_onlineFix->setEnabled(windows);
    m_environment->setEnabled(kind() != "steam");
    if (!windows) m_onlineFix->setChecked(false);
}

QJsonObject GameOptionsWidget::optionsData() const {
    QJsonArray tags;
    for (const auto &tag : m_tags->text().split(",", Qt::SkipEmptyParts)) {
        const auto value = tag.trimmed().toLower();
        if (!value.isEmpty() && value != "online-fix" && !tags.contains(value)) tags.append(value);
    }
    if (m_onlineFix->isEnabled() && m_onlineFix->isChecked()) tags.append("online-fix");
    return {
        {"kind", kind()},
        {"prefix", kind() == "windows" ? m_prefix->text().trimmed() : QString()},
        {"proton", kind() == "windows" ? protonSelection() : "default"},
        {"arguments", m_arguments->text()}, {"tags", tags},
        {"environment", m_environment->isEnabled() ? m_environment->toPlainText() : QString()},
    };
}
