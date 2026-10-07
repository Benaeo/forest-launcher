#include "gameoptionswidget.h"
#include "protonmanager.h"
#include "backendclient.h"
#include "elidingcombobox.h"
#include "losslessdialog.h"

#include <QActionGroup>
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
#include <QMenu>
#include <QToolButton>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QTimer>

namespace {
bool isZeroSteamAccount(const QString &identity) {
    return !identity.isEmpty() && identity.count(QLatin1Char('0')) == identity.size();
}

// Account selection is multi-select: activating a checkable row must not
// dismiss the popup. Keep normal QMenu handling for dismissal and navigation.
class SteamAccountsMenu final : public QMenu {
public:
    using QMenu::QMenu;
protected:
    void mouseReleaseEvent(QMouseEvent *event) override {
        auto *action = actionAt(event->position().toPoint());
        if (event->button() == Qt::LeftButton && canToggle(action)) {
            action->trigger();
            event->accept();
            return;
        }
        QMenu::mouseReleaseEvent(event);
    }
    void keyPressEvent(QKeyEvent *event) override {
        auto *action = activeAction();
        if ((event->key() == Qt::Key_Space || event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
            && canToggle(action)) {
            if (!event->isAutoRepeat()) action->trigger();
            event->accept();
            return;
        }
        QMenu::keyPressEvent(event);
    }
private:
    static bool canToggle(const QAction *action) {
        return action && action->isEnabled() && action->isCheckable() && !action->isSeparator();
    }
};
}

GameOptionsWidget::GameOptionsWidget(const QJsonObject &options, const QJsonObject &bootstrap, QWidget *parent,
                                     bool defaultsEditor)
    : QWidget(parent), m_defaultsEditor(defaultsEditor) {
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
    m_backendDirectory = frontend.value("backend").toString();
    m_dataRoot = dataRoot;
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
    m_arguments->setPlaceholderText("-fullscreen -dx11 -novid");
    QStringList tags;
    for (const auto &value : options.value("tags").toArray())
        if (value.toString() != "online-fix") tags << value.toString();
    m_tags = new QLineEdit(tags.join(", "), this);
    m_tags->setObjectName("gameTags");
    m_tags->setPlaceholderText("fps,horror,free-to-play,survival");
    m_onlineFix = new QCheckBox("online-fix - Steam", this);
    m_onlineFix->setObjectName("onlineFixCheck");
    m_onlineFix->setToolTip("Use native Steam and Proton with the [Main] FakeAppId from OnlineFix.ini or SteamFix.ini beside the game executable. A valid INI is required. No game files are changed.");
    m_onlineFix->setChecked(options.contains("online_fix_requested")
        ? options.value("online_fix_requested").toBool() : options.value("tags").toArray().contains("online-fix"));
    m_onlineFixDebounce = new QTimer(this);
    m_onlineFixDebounce->setSingleShot(true);
    m_onlineFixDebounce->setInterval(200);
    connect(m_onlineFixDebounce, &QTimer::timeout, this, &GameOptionsWidget::checkOnlineFix);
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
    m_steamShortcut = new QCheckBox("Steam", this);
    auto *steamShortcut = m_steamShortcut;
    steamShortcut->setObjectName("steamShortcutCheck");
    steamShortcut->setChecked(options.value("steam_shortcut").toBool());
    steamShortcut->setToolTip(defaultsEditor
        ? "Copy this Steam shortcut preference and selected accounts to new non-Steam-library games. Existing games are unchanged."
        : "Write a Forest shortcut and selected artwork for the checked accounts. Restart Steam to refresh its library; writing shortcuts never stops Steam.");
    m_steamAccountsButton = new QToolButton(this);
    m_steamAccountsButton->setObjectName("steamAccountsButton");
    m_steamAccountsButton->setText("Accounts");
    m_steamAccountsButton->setPopupMode(QToolButton::InstantPopup);
    auto *accountMenu = new SteamAccountsMenu(m_steamAccountsButton);
    m_steamAccountsButton->setMenu(accountMenu);
    const auto selectedAccounts = options.value("steam_accounts").toArray();
    for (const auto &value : bootstrap.value("steam_accounts").toArray()) {
        const auto account = value.toObject();
        if (isZeroSteamAccount(account.value("id").toString())) continue;
        auto *action = accountMenu->addAction(account.value("name").toString() + " (" + account.value("id").toString() + ")");
        action->setCheckable(true);
        action->setData(account.value("id").toString());
        action->setProperty("accountName", account.value("name").toString());
        action->setChecked(selectedAccounts.contains(account.value("id")));
    }
    m_hasSteamAccounts = !accountMenu->actions().isEmpty();
    for (const auto &identity : selectedAccounts) {
        if (isZeroSteamAccount(identity.toString())) continue;
        bool found = false;
        for (auto *action : accountMenu->actions()) found |= action->data().toString() == identity.toString();
        if (!found) {
            auto *action = accountMenu->addAction("Unavailable account (" + identity.toString() + ")");
            action->setCheckable(true); action->setChecked(true); action->setData(identity.toString());
            action->setProperty("accountName", "Unavailable account (" + identity.toString() + ")");
        }
    }
    const auto updateShortcutAccountText = [this, accountMenu] {
        int count = 0;
        QString name;
        for (auto *action : accountMenu->actions()) {
            if (!action->isChecked()) continue;
            ++count;
            name = action->property("accountName").toString();
            if (name.isEmpty()) name = action->data().toString();
        }
        m_steamAccountsButton->setText(count == 1 ? name
            : count > 1 ? QString("Accounts (%1)").arg(count) : QString("Accounts"));
    };
    for (auto *action : accountMenu->actions())
        connect(action, &QAction::toggled, this, [updateShortcutAccountText](bool) { updateShortcutAccountText(); });
    updateShortcutAccountText();
    steamShortcut->setEnabled(defaultsEditor || (m_hasSteamAccounts && kind() != "steam"));
    if (!m_hasSteamAccounts) m_steamAccountsButton->setToolTip("No local native Steam accounts were found. Sign in to Steam first.");
    connect(steamShortcut, &QCheckBox::toggled, this, [this](bool checked) { m_steamAccountsButton->setEnabled(checked); });
    m_steamAccountsButton->setEnabled(steamShortcut->isChecked());
    m_steamLaunchAccountButton = new QToolButton(this);
    m_steamLaunchAccountButton->setObjectName("steamLaunchAccountButton");
    m_steamLaunchAccountButton->setText("Account");
    m_steamLaunchAccountButton->setPopupMode(QToolButton::InstantPopup);
    auto *launchAccountMenu = new QMenu(m_steamLaunchAccountButton);
    m_steamLaunchAccountButton->setMenu(launchAccountMenu);
    auto *launchAccountGroup = new QActionGroup(launchAccountMenu);
    launchAccountGroup->setExclusive(true);
    auto *anyAccount = launchAccountMenu->addAction("Any account");
    anyAccount->setCheckable(true);
    anyAccount->setData(QString());
    anyAccount->setProperty("accountName", "Any account");
    launchAccountGroup->addAction(anyAccount);
    const auto switchable = bootstrap.value(bootstrap.contains("steam_switchable_accounts")
        ? "steam_switchable_accounts" : "steam_accounts").toArray();
    const auto savedAccount = options.value("steam_launch_account").toString();
    const auto wantedAccount = isZeroSteamAccount(savedAccount) ? QString() : savedAccount;
    QAction *checkedAccount = nullptr;
    for (const auto &value : switchable) {
        const auto account = value.toObject();
        const auto identity = account.value("id").toString();
        if (isZeroSteamAccount(identity)) continue;
        auto *action = launchAccountMenu->addAction(account.value("name").toString() + " (" + identity + ")");
        action->setCheckable(true);
        action->setData(identity);
        action->setProperty("accountName", account.value("name").toString());
        launchAccountGroup->addAction(action);
        if (!identity.isEmpty() && identity == wantedAccount) checkedAccount = action;
    }
    if (!wantedAccount.isEmpty() && !checkedAccount) {
        checkedAccount = launchAccountMenu->addAction("Unavailable account (" + wantedAccount + ")");
        checkedAccount->setCheckable(true);
        checkedAccount->setData(wantedAccount);
        checkedAccount->setProperty("accountName", "Unavailable account (" + wantedAccount + ")");
        checkedAccount->setToolTip("This game still requires this account. Sign in to it through Steam, or explicitly choose another account.");
        launchAccountGroup->addAction(checkedAccount);
    }
    const auto updateLaunchAccountText = [this, launchAccountMenu] {
        for (auto *action : launchAccountMenu->actions()) {
            if (!action->isChecked()) continue;
            auto name = action->property("accountName").toString();
            if (name.isEmpty()) name = action->data().toString();
            m_steamLaunchAccountButton->setText(name);
            return;
        }
        m_steamLaunchAccountButton->setText("Any account");
    };
    for (auto *action : launchAccountMenu->actions())
        connect(action, &QAction::toggled, this, [updateLaunchAccountText](bool) { updateLaunchAccountText(); });
    (checkedAccount ? checkedAccount : anyAccount)->setChecked(true);
    updateLaunchAccountText();
    auto accountTooltip = QString("Online-fix games only. Requires “Remember password” for switching. Forest asks before restarting an open Steam client. Any account uses whichever account is signed in.");
    const auto accountError = bootstrap.value("steam_account_error").toString();
    if (!accountError.isEmpty()) accountTooltip += "\n" + accountError;
    m_steamLaunchAccountButton->setToolTip(accountTooltip);
    QGroupBox *generalOptions = nullptr;
    QHBoxLayout *tools = nullptr;
    if (defaultsEditor) {
        generalOptions = new QGroupBox("Options", this);
        m_generalOptions = new QVBoxLayout(generalOptions);
        m_generalOptions->addWidget(m_mangohud);
        m_generalOptions->addWidget(m_preferSdl);
        m_generalOptions->addWidget(m_noSleep);
        auto *onlineFixDefaultsRow = new QHBoxLayout;
        onlineFixDefaultsRow->addWidget(m_onlineFix);
        onlineFixDefaultsRow->addWidget(m_steamLaunchAccountButton);
        onlineFixDefaultsRow->addStretch();
        m_generalOptions->addLayout(onlineFixDefaultsRow);
        m_generalOptions->addWidget(new QLabel("New game shortcuts", this));
        m_generalOptions->addWidget(m_desktopShortcut);
        m_generalOptions->addWidget(m_appMenuShortcut);
        auto *steamDefaultsRow = new QHBoxLayout;
        steamDefaultsRow->addWidget(steamShortcut);
        steamDefaultsRow->addWidget(m_steamAccountsButton);
        steamDefaultsRow->addStretch();
        m_generalOptions->addLayout(steamDefaultsRow);
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
        auto *onlineFixRow = new QHBoxLayout;
        onlineFixRow->addWidget(m_onlineFix);
        onlineFixRow->addWidget(m_steamLaunchAccountButton);
        onlineFixRow->addStretch();
        m_launchTail->addLayout(onlineFixRow, 0, 1);
        m_launchTail->addWidget(new QLabel("Tools", tail), 1, 0);
        m_launchTail->addLayout(tools, 1, 1);
        auto *shortcuts = new QHBoxLayout;
        shortcuts->addWidget(m_desktopShortcut);
        shortcuts->addWidget(m_appMenuShortcut);
        shortcuts->addWidget(steamShortcut);
        shortcuts->addWidget(m_steamAccountsButton);
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
    m_environment->setPlaceholderText("PROTON_ENABLE_WAYLAND=1\nDXVK_HUD=fps,memory,version,api\nPROTON_FSR4_UPGRADE=1\nPROTON_VKD3D_LOWLATENCY=1");
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
    connect(m_onlineFix, &QCheckBox::toggled, this, [this] {
        m_steamLaunchAccountButton->setEnabled(m_onlineFix->isEnabled() && m_onlineFix->isChecked());
    });
    connect(m_prefixBrowse, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getExistingDirectory(this, "Choose Wine prefix", m_prefix->text());
        if (!path.isEmpty()) m_prefix->setText(path);
    });
    updateKind();
    if (!defaultsEditor) setExecutablePath(options.value("path").toString());
}

void GameOptionsWidget::setExecutablePath(const QString &path) {
    if (m_defaultsEditor) return;
    ++m_onlineFixRevision;
    m_executablePath = path.trimmed();
    m_onlineFixSupported = false;
    m_onlineFixReason = m_executablePath.isEmpty() || m_backendDirectory.isEmpty()
        ? "Choose a Windows executable with a valid [Main] FakeAppId in OnlineFix.ini or SteamFix.ini beside it."
        : "Checking Steam online-fix support beside the selected executable…";
    m_onlineFixDebounce->stop();
    if (m_onlineFixBackend) {
        m_onlineFixBackend->deleteLater();
        m_onlineFixBackend = nullptr;
    }
    updateKind();
    m_onlineFixDebounce->start();
}

void GameOptionsWidget::checkOnlineFix() {
    if (m_defaultsEditor) return;
    if (kind() != "windows" || m_executablePath.isEmpty() || m_backendDirectory.isEmpty()) {
        m_onlineFixReason = "Choose a Windows executable with a valid Steam OnlineFix.ini or SteamFix.ini beside it.";
        updateKind();
        return;
    }
    const auto revision = m_onlineFixRevision;
    auto *client = new BackendClient(m_backendDirectory, m_dataRoot, this);
    m_onlineFixBackend = client;
    client->request("detect_online_fix", {{"path", m_executablePath}}, [this, revision, client](const QJsonObject &data) {
        client->deleteLater();
        if (m_onlineFixBackend == client) m_onlineFixBackend = nullptr;
        if (revision != m_onlineFixRevision) return;
        m_onlineFixSupported = data.value("supported").toBool();
        m_onlineFixReason = data.value("reason").toString();
        updateKind();
    }, [this, revision, client](const QString &error) {
        client->deleteLater();
        if (m_onlineFixBackend == client) m_onlineFixBackend = nullptr;
        if (revision != m_onlineFixRevision) return;
        m_onlineFixSupported = false;
        m_onlineFixReason = error;
        updateKind();
    });
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

void GameOptionsWidget::addGeneralOption(QWidget *option, bool first) {
    if (m_generalOptions) m_generalOptions->insertWidget(first ? 0 : m_generalOptions->count() - 1, option);
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
    m_onlineFix->setEnabled(m_defaultsEditor || (windows && m_onlineFixSupported));
    m_onlineFix->setToolTip(m_defaultsEditor
        ? "Prefer Steam online-fix for new games only when a valid Steam configuration is detected beside their executable."
        : m_onlineFix->isEnabled()
            ? "Steam online-fix support detected in OnlineFix.ini or SteamFix.ini. Uses the configured FakeAppId without changing game files."
            : m_onlineFixReason + "\nThe saved checkmark is preserved; unsupported games do not use Steam online-fix mode.");
    m_environment->setEnabled(kind() != "steam");
    m_mangohud->setEnabled(kind() != "steam");
    m_preferSdl->setEnabled(windows);
    m_noSleep->setEnabled(kind() != "steam");
    m_steamShortcut->setEnabled(m_defaultsEditor || (m_hasSteamAccounts && kind() != "steam"));
    if (!m_defaultsEditor && kind() == "steam") m_steamShortcut->setChecked(false);
    m_steamAccountsButton->setEnabled(m_steamShortcut->isChecked());
    m_steamLaunchAccountButton->setEnabled(m_onlineFix->isEnabled() && m_onlineFix->isChecked());
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
    updateLatestButton();
}

QJsonObject GameOptionsWidget::optionsData() const {
    QJsonArray tags;
    for (const auto &tag : m_tags->text().split(",", Qt::SkipEmptyParts)) {
        const auto value = tag.trimmed().toLower();
        if (!value.isEmpty() && value != "online-fix" && !tags.contains(value)) tags.append(value);
    }
    if (kind() == "windows" && m_onlineFix->isChecked() && (m_defaultsEditor || m_onlineFixSupported))
        tags.append("online-fix");
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
    {
        // Defaults are snapshots, not live overrides of existing profiles.
        result.insert("online_fix_requested", m_onlineFix->isChecked());
        result.insert("steam_shortcut", m_steamShortcut->isChecked() && (m_defaultsEditor || kind() != "steam"));
        QJsonArray accounts;
        for (auto *action : m_steamAccountsButton->menu()->actions())
            if (action->isChecked()) accounts.append(action->data().toString());
        result.insert("steam_accounts", accounts);
        QString launchAccount;
        // Preserve the account alongside the preference even when detection
        // disables the checkbox. It is used only by an effective Steam launch.
        if (m_onlineFix->isChecked())
            for (auto *action : m_steamLaunchAccountButton->menu()->actions())
                if (action->isChecked()) launchAccount = action->data().toString();
        result.insert("steam_launch_account", launchAccount);
    }
    if (!m_losslessOptions.isEmpty()) {
        auto lossless = m_losslessOptions;
        if (kind() == "steam") lossless.insert("multiplier", 1);
        result.insert("lossless_scaling", lossless);
    }
    return result;
}
