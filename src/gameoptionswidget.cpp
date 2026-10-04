#include "gameoptionswidget.h"
#include "protonmanager.h"
#include "backendclient.h"
#include "elidingcombobox.h"
#include "losslessdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QSignalBlocker>
#include <QSet>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QGridLayout>

GameOptionsWidget::GameOptionsWidget(const QJsonObject &options, const QJsonObject &bootstrap, QWidget *parent,
                                     bool defaultsEditor)
    : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *tabs = defaultsEditor ? nullptr : new QTabWidget(this);
    QWidget *launch = defaultsEditor ? static_cast<QWidget *>(new QGroupBox("New game defaults", this)) : new QWidget(tabs);
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
    const auto frontend = bootstrap.value("frontend").toObject();
    const auto dataRoot = frontend.value("data_root").toString();
    m_runnerRoot = dataRoot.isEmpty() ? QDir::homePath() + "/.local/share/Steam/compatibilitytools.d"
                                    : dataRoot + "/compatibilitytools.d";
    m_proton = new ElidingComboBox(this);
    m_proton->setObjectName("gameProton");
    const QStringList latestNames{"Proton-CachyOS Latest", "Proton-GE Latest"};
    for (const auto &name : latestNames) {
        QJsonObject build;
        for (const auto &value : bootstrap.value("protons").toArray()) {
            if (value.toObject().value("label").toString() == name) { build = value.toObject(); break; }
        }
        m_latestIds.append(build.value("id").toString(m_runnerRoot + "/" + name));
    }
    auto proton = options.value("proton").toString("default");
    if (proton.isEmpty() || proton == "default")
        proton = bootstrap.value("settings").toObject().value("default_proton").toString(latestNames.first());
    refreshProtonChoices(proton);
    m_arguments = new QLineEdit(options.value("arguments").toString(), this);
    m_arguments->setObjectName("gameArguments");
    m_arguments->setPlaceholderText("Optional arguments, e.g. -fullscreen");
    QStringList tags;
    for (const auto &value : options.value("tags").toArray())
        if (value.toString() != "online-fix") tags << value.toString();
    m_tags = new QLineEdit(tags.join(", "), this);
    m_tags->setObjectName("gameTags");
    m_tags->setPlaceholderText("Optional comma-separated tags");
    m_onlineFix = new QCheckBox("online-fix — Steam", this);
    m_onlineFix->setObjectName("onlineFixCheck");
    m_onlineFix->setToolTip("Use native Steam and Proton with the [Main] FakeAppId from OnlineFix.ini or SteamFix.ini beside the game executable. A valid INI is required. No game files are changed.");
    m_onlineFix->setChecked(options.value("tags").toArray().contains("online-fix"));
    m_mangohud = new QCheckBox("MangoHud", this);
    m_mangohud->setObjectName("mangohudCheck");
    m_mangohud->setChecked(options.value("mangohud").toBool());
    m_mangohud->setToolTip("Set MANGOHUD=1 for the game (overrides the environment field). Requires MangoHud installed; API support depends on the game.");
    m_preferSdl = new QCheckBox("SDL", this);
    m_preferSdl->setObjectName("preferSdlCheck");
    m_preferSdl->setChecked(options.value("prefer_sdl").toBool());
    m_preferSdl->setToolTip("Set PROTON_PREFER_SDL=1 for Windows games (overrides the environment field). Support depends on the selected Proton build.");
    m_noSleep = new QCheckBox("No sleep", this);
    m_noSleep->setObjectName("noSleepCheck");
    m_noSleep->setChecked(options.value("no_sleep").toBool());
    m_noSleep->setToolTip("Prevent system sleep while the game command runs using systemd-inhibit. Does not change screen-lock settings. Steam library launches cannot be tracked this way.");
    m_desktopShortcut = new QCheckBox("Desktop", this);
    m_desktopShortcut->setObjectName("desktopShortcutCheck");
    m_desktopShortcut->setChecked(options.value("desktop_shortcut").toBool());
    m_desktopShortcut->setToolTip("Create a Forest shortcut in your Desktop folder when this game is saved.");
    m_appMenuShortcut = new QCheckBox("App Menu", this);
    m_appMenuShortcut->setObjectName("appMenuShortcutCheck");
    m_appMenuShortcut->setChecked(options.value("app_menu_shortcut").toBool());
    m_appMenuShortcut->setToolTip("Create a Forest shortcut in your application menu when this game is saved.");
    auto *steamShortcut = new QCheckBox("Steam (later)", this);
    steamShortcut->setObjectName("steamShortcutCheck");
    steamShortcut->setEnabled(false);
    steamShortcut->setToolTip("Steam shortcut integration is deferred until icon and banner support is added.");
    QGroupBox *generalOptions = nullptr;
    QHBoxLayout *tools = nullptr;
    if (defaultsEditor) {
        generalOptions = new QGroupBox("Options", this);
        m_generalOptions = new QVBoxLayout(generalOptions);
        m_generalOptions->addWidget(m_mangohud);
        m_generalOptions->addWidget(m_preferSdl);
        m_generalOptions->addWidget(m_noSleep);
        m_generalOptions->addWidget(m_onlineFix);
        m_generalOptions->addWidget(new QLabel("New game shortcuts", this));
        m_generalOptions->addWidget(m_desktopShortcut);
        m_generalOptions->addWidget(m_appMenuShortcut);
        m_generalOptions->addWidget(steamShortcut);
        m_generalOptions->addStretch();
    } else {
        tools = new QHBoxLayout;
        tools->addWidget(m_mangohud);
        tools->addWidget(m_preferSdl);
        tools->addWidget(m_noSleep);
        tools->addStretch();
    }
    form->addRow("Game type", m_kind);
    form->addRow("Wine prefix", prefixRow);
    auto *protonRow = new QHBoxLayout;
    protonRow->addWidget(m_proton, 1);
    m_downloadLatest = new QPushButton("Download Latest", this);
    m_downloadLatest->setObjectName("downloadLatestProtonButton");
    m_downloadLatest->setFixedWidth(m_downloadLatest->sizeHint().width());
    protonRow->addWidget(m_downloadLatest);
    form->addRow(defaultsEditor ? "Default Proton" : "Proton build", protonRow);
    m_latestProgressRow = new QWidget(this);
    m_latestProgressRow->setObjectName("latestDownloadProgressRow");
    auto *progressLayout = new QHBoxLayout(m_latestProgressRow);
    progressLayout->setContentsMargins(0, 0, 0, 0);
    m_latestProgress = new QProgressBar(m_latestProgressRow);
    m_latestProgress->setObjectName("latestDownloadProgress");
    auto *cancelDownload = new QPushButton("Cancel", m_latestProgressRow);
    cancelDownload->setObjectName("cancelLatestDownload");
    progressLayout->addWidget(m_latestProgress, 1);
    progressLayout->addWidget(cancelDownload);
    m_latestProgressRow->hide();
    form->addRow(QString(), m_latestProgressRow);
    m_latestStatus = new QLabel(this);
    m_latestStatus->setObjectName("latestDownloadStatus");
    m_latestStatus->setTextFormat(Qt::PlainText);
    m_latestStatus->setWordWrap(true);
    m_latestStatus->hide();
    form->addRow(QString(), m_latestStatus);
    m_managerButton = new QPushButton("Proton Manager", this);
    m_managerButton->setObjectName("protonManagerButton");
    form->addRow(QString(), m_managerButton);
    m_losslessOptions = options.value("lossless_scaling").toObject();
    m_lsfgInstalled = bootstrap.value("capabilities").toObject().value("lsfg_vk").toBool();
    m_losslessButton = new QPushButton("Lossless Scaling", this);
    m_losslessButton->setObjectName("losslessScalingButton");
    m_losslessButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    form->addRow("lsfg-vk", m_losslessButton);
    connect(m_losslessButton, &QPushButton::clicked, this, [this, frontend] {
        LosslessDialog dialog(m_losslessOptions, frontend, this);
        if (dialog.exec() == QDialog::Accepted) m_losslessOptions = dialog.optionsData();
    });
    connect(m_downloadLatest, &QPushButton::clicked, this, [this, frontend] {
        if (m_latestDownloading) return;
        const auto family = m_proton->currentIndex() == 0 ? "cachyos" : "ge";
        m_latestDownloading = true;
        m_proton->setEnabled(false);
        m_kind->setEnabled(false);
        m_managerButton->setEnabled(false);
        if (auto *buttons = window()->findChild<QDialogButtonBox *>()) buttons->setEnabled(false);
        m_latestProgress->setRange(0, 0);
        m_latestProgressRow->show();
        m_latestStatus->setText("Preparing download…");
        m_latestStatus->show();
        updateLatestButton();
        m_latestBackend = new BackendClient(frontend.value("backend").toString(), frontend.value("data_root").toString(), this);
        m_latestBackend->request("download_latest_proton", {{"family", family}},
            [this](const QJsonObject &) {
                m_latestStatus->hide();
                finishLatestDownload();
            }, [this](const QString &error) {
                m_latestStatus->setText(error);
                finishLatestDownload();
            }, [this](const QJsonObject &event) {
                const auto phase = event.value("phase").toString();
                const double done = event.value("bytes").toDouble(), total = event.value("total").toDouble();
                m_latestProgress->setRange(0, total > 0 ? 100 : 0);
                if (total > 0) m_latestProgress->setValue(qBound(0, int(done * 100 / total), 100));
                m_latestProgress->setFormat(phase == "extract" ? "Extracting: %p%" : "Downloading: %p%");
                m_latestStatus->setText(phase == "download"
                    ? QString("%1 / %2 MiB — %3 MiB/s").arg(done / 1048576.0, 0, 'f', 1)
                        .arg(total / 1048576.0, 0, 'f', 1).arg(event.value("speed").toDouble() / 1048576.0, 0, 'f', 1)
                    : phase == "verify" ? "Verifying checksum…" : "Extracting and installing…");
            });
    });
    connect(cancelDownload, &QPushButton::clicked, this, [this] {
        if (!m_latestDownloading) return;
        delete m_latestBackend;
        m_latestBackend = nullptr;
        m_latestStatus->setText("Download cancelled.");
        finishLatestDownload();
    });
    connect(m_proton, &QComboBox::currentIndexChanged, this, [this] { updateLatestButton(); });
    connect(m_proton, &QComboBox::activated, this, [this] { updateLatestButton(); });
    connect(m_managerButton, &QPushButton::clicked, this, [this, frontend] {
        const auto previous = protonSelection();
        ProtonManager manager(frontend.value("backend").toString(), frontend.value("data_root").toString(), this);
        const bool selected = manager.exec() == QDialog::Accepted && !manager.selectedVersion().isEmpty();
        refreshProtonChoices(selected ? manager.selectedVersion() : previous);
        updateLatestButton();
    });
    form->addRow("Arguments", m_arguments);
    form->addRow("Tags", m_tags);
    if (!defaultsEditor) {
        auto *tail = new QWidget(this);
        m_launchTail = new QGridLayout(tail);
        m_launchTail->setContentsMargins(0, 0, 0, 0);
        m_launchTail->setColumnStretch(1, 1);
        m_launchTail->addWidget(m_onlineFix, 0, 1);
        m_launchTail->addWidget(new QLabel("Tools", tail), 1, 0);
        m_launchTail->addLayout(tools, 1, 1);
        auto *shortcuts = new QHBoxLayout;
        shortcuts->addWidget(m_desktopShortcut);
        shortcuts->addWidget(m_appMenuShortcut);
        shortcuts->addWidget(steamShortcut);
        shortcuts->addStretch();
        m_launchTail->addWidget(new QLabel("Shortcuts", tail), 2, 0);
        m_launchTail->addLayout(shortcuts, 2, 1);
        form->addRow(tail);
        tabs->addTab(launch, "Launch");
    }
    QWidget *advanced = defaultsEditor ? static_cast<QWidget *>(new QGroupBox("Environment variables", this)) : new QWidget(tabs);
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
    if (defaultsEditor) {
        auto *columns = new QHBoxLayout;
        columns->addWidget(launch, 3);
        columns->addWidget(generalOptions, 2);
        columns->addWidget(advanced, 3);
        layout->addLayout(columns);
    } else {
        tabs->addTab(advanced, "Environment");
        layout->addWidget(tabs);
    }
    connect(m_kind, &QComboBox::currentIndexChanged, this, [this] { updateKind(); });
    connect(m_prefixBrowse, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getExistingDirectory(this, "Choose Wine prefix", m_prefix->text());
        if (!path.isEmpty()) m_prefix->setText(path);
    });
    updateKind();
}

QString GameOptionsWidget::kind() const { return m_kind->currentData().toString(); }
QString GameOptionsWidget::protonSelection() const {
    return m_proton->currentData().toString();
}

void GameOptionsWidget::refreshProtonChoices(const QString &selection) {
    const QSignalBlocker blocker(m_proton);
    m_proton->clear();
    const QStringList latestNames{"Proton-CachyOS Latest", "Proton-GE Latest"};
    for (int index = 0; index < latestNames.size(); ++index) {
        m_proton->addItem(latestNames[index], m_latestIds[index]);
        m_proton->setItemData(index, m_runnerRoot + "/" + latestNames[index], Qt::ToolTipRole);
    }
    QSet<QString> seen;
    for (const auto &name : latestNames) {
        const QFileInfo runner(m_runnerRoot + "/" + name);
        if (QFileInfo::exists(runner.filePath() + "/proton")) seen.insert(runner.canonicalFilePath());
    }
    const auto entries = QDir(m_runnerRoot).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const auto &runner : entries) {
        if (latestNames.contains(runner.fileName()) || !QFileInfo::exists(runner.filePath() + "/proton")
            || seen.contains(runner.canonicalFilePath())) continue;
        seen.insert(runner.canonicalFilePath());
        m_proton->addItem(runner.fileName(), runner.absoluteFilePath());
        m_proton->setItemData(m_proton->count() - 1, runner.absoluteFilePath(), Qt::ToolTipRole);
    }
    auto index = m_proton->findData(selection);
    if (index < 0) index = m_proton->findText(selection);
    if (index < 0 && !selection.isEmpty()) {
        auto label = QFileInfo(selection).fileName();
        if (selection == "auto") label = "Automatic (legacy)";
        if (label.isEmpty()) label = selection;
        if (!QFileInfo::exists(selection + "/proton")) label += " (unavailable)";
        m_proton->addItem(label, selection);
        index = m_proton->count() - 1;
        m_proton->setItemData(index, selection, Qt::ToolTipRole);
    }
    m_proton->setCurrentIndex(index >= 0 ? index : 0);
}

void GameOptionsWidget::setKind(const QString &kind) {
    const auto index = m_kind->findData(kind);
    if (index >= 0) m_kind->setCurrentIndex(index);
}

void GameOptionsWidget::addGeneralOption(QWidget *option) {
    if (m_generalOptions) m_generalOptions->insertWidget(m_generalOptions->count() - 1, option);
}

void GameOptionsWidget::addIconControl(QWidget *control) {
    if (m_launchTail) m_launchTail->addWidget(control, 0, 2, 3, 1, Qt::AlignRight | Qt::AlignBottom);
}

void GameOptionsWidget::finishLatestDownload() {
    if (m_latestBackend) m_latestBackend->deleteLater();
    m_latestBackend = nullptr;
    m_latestDownloading = false;
    m_latestProgressRow->hide();
    m_kind->setEnabled(true);
    m_managerButton->setEnabled(true);
    refreshProtonChoices(protonSelection());
    if (auto *buttons = window()->findChild<QDialogButtonBox *>()) buttons->setEnabled(true);
    updateKind();
}

void GameOptionsWidget::updateLatestButton() {
    const auto name = m_proton->currentText();
    m_proton->setToolTip(name + "\n" + protonSelection());
    const bool latest = m_proton->currentIndex() >= 0 && m_proton->currentIndex() < 2;
    const auto path = m_runnerRoot + "/" + name;
    const bool installed = latest && QFileInfo::exists(path + "/proton");
    m_downloadLatest->setText(installed ? "Installed" : "Download Latest");
    m_downloadLatest->setEnabled(kind() == "windows" && latest && !installed && !m_latestDownloading);
    m_downloadLatest->setToolTip(latest ? (installed ? "Already installed: " : "Download the newest release to: ") + path
                                     : "Choose a Latest runner to download it.");
    if (latest) m_proton->setItemData(m_proton->currentIndex(), path, Qt::ToolTipRole);
}

void GameOptionsWidget::updateKind() {
    const bool windows = kind() == "windows";
    m_prefix->setEnabled(windows);
    m_prefixBrowse->setEnabled(windows);
    m_proton->setEnabled(windows);
    m_onlineFix->setEnabled(windows);
    m_environment->setEnabled(kind() != "steam");
    m_mangohud->setEnabled(kind() != "steam");
    m_preferSdl->setEnabled(windows);
    m_noSleep->setEnabled(kind() != "steam");
    m_losslessButton->setEnabled(m_lsfgInstalled && kind() != "steam");
    m_losslessButton->setToolTip(!m_lsfgInstalled
        ? "Install the missing package lsfg-vk to use this feature."
        : kind() == "steam" ? "Lossless Scaling cannot be configured for Steam library launch requests. Configure it in Steam instead."
                           : "Configure Lossless Scaling for a Vulkan-rendered game. Multiplier 1 = off; 2 or more = on.");
    if (!windows) m_preferSdl->setChecked(false);
    if (kind() == "steam") {
        m_mangohud->setChecked(false);
        m_noSleep->setChecked(false);
    }
    if (!windows) m_onlineFix->setChecked(false);
    updateLatestButton();
}

QJsonObject GameOptionsWidget::optionsData() const {
    QJsonArray tags;
    for (const auto &tag : m_tags->text().split(",", Qt::SkipEmptyParts)) {
        const auto value = tag.trimmed().toLower();
        if (!value.isEmpty() && value != "online-fix" && !tags.contains(value)) tags.append(value);
    }
    if (m_onlineFix->isEnabled() && m_onlineFix->isChecked()) tags.append("online-fix");
    QJsonObject result{
        {"kind", kind()},
        {"prefix", kind() == "windows" ? m_prefix->text().trimmed() : QString()},
        {"proton", kind() == "windows" ? protonSelection() : "default"},
        {"arguments", m_arguments->text()}, {"tags", tags},
        {"mangohud", m_mangohud->isEnabled() && m_mangohud->isChecked()},
        {"prefer_sdl", m_preferSdl->isEnabled() && m_preferSdl->isChecked()},
        {"no_sleep", m_noSleep->isEnabled() && m_noSleep->isChecked()},
        {"desktop_shortcut", m_desktopShortcut->isChecked()},
        {"app_menu_shortcut", m_appMenuShortcut->isChecked()},
        {"environment", m_environment->isEnabled() ? m_environment->toPlainText() : QString()},
    };
    if (!m_losslessOptions.isEmpty()) {
        auto lossless = m_losslessOptions;
        if (kind() == "steam") lossless.insert("multiplier", 1);
        result.insert("lossless_scaling", lossless);
    }
    return result;
}
