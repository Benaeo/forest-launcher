#include "gameoptionswidget.h"

#include <QCheckBox>
#include <QComboBox>
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
    m_prefix->setPlaceholderText("Automatic: a dedicated prefix for each new game");
    m_prefixBrowse = new QPushButton("Browse…", this);
    auto *prefixRow = new QHBoxLayout;
    prefixRow->addWidget(m_prefix, 1);
    prefixRow->addWidget(m_prefixBrowse);
    m_proton = new QComboBox(this);
    m_proton->setObjectName("gameProton");
    if (!defaultsEditor) m_proton->addItem("Current application default", "default");
    m_proton->addItem("Automatic (prefer installed GE-Proton)", "auto");
    for (const auto &value : bootstrap.value("protons").toArray()) {
        const auto build = value.toObject();
        m_proton->addItem(build.value("label").toString(), build.value("id").toString());
    }
    auto proton = options.value("proton").toString("default");
    if (defaultsEditor && (proton.isEmpty() || proton == "default"))
        proton = bootstrap.value("settings").toObject().value("default_proton").toString("auto");
    if (m_proton->findData(proton) < 0) m_proton->addItem(proton, proton);
    m_proton->setCurrentIndex(m_proton->findData(proton));
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
QString GameOptionsWidget::protonSelection() const { return m_proton->currentData().toString(); }

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
        {"proton", kind() == "windows" ? m_proton->currentData().toString() : "default"},
        {"arguments", m_arguments->text()}, {"tags", tags},
        {"environment", m_environment->isEnabled() ? m_environment->toPlainText() : QString()},
    };
}
