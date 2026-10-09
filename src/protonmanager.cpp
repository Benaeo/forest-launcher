#include "protonmanager.h"
#include "backendclient.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

ProtonManager::ProtonManager(QString backendDirectory, QString dataRoot, QWidget *parent)
    : QDialog(parent), m_directory(std::move(backendDirectory)), m_dataRoot(std::move(dataRoot)) {
    setWindowTitle("Proton Manager");
    resize(660, 580);
    auto *layout = new QVBoxLayout(this);
    m_backend = new BackendClient(m_directory, m_dataRoot, this);
    m_family = new QComboBox(this);
    m_family->setObjectName("protonFamily");
    m_family->addItem("Proton-CachyOS", "cachyos");
    m_family->addItem("Proton-GE", "ge");
    layout->addWidget(m_family);
    auto *hint = new QLabel("Downloads install a separate version. Latest folders and current selections stay unchanged.\nUse version selects an installed build for the dialog that opened this manager.", this);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    m_table = new QTableWidget(0, 3, this);
    m_table->setObjectName("protonVersions");
    m_table->setHorizontalHeaderLabels({"Version", "Download", "Selection"});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_table->setColumnWidth(1, 108);
    m_table->setColumnWidth(2, 120);
    m_table->verticalHeader()->setDefaultSectionSize(36);
    m_table->verticalHeader()->hide();
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_table, 1);
    m_progress = new QProgressBar(this);
    m_progress->setObjectName("protonDownloadProgress");
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->hide();
    layout->addWidget(m_progress);
    m_status = new QLabel(this);
    m_status->setObjectName("protonManagerStatus");
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    auto *buttons = new QHBoxLayout;
    m_more = new QPushButton("Load more", this);
    m_cancel = new QPushButton("Cancel download", this);
    m_cancel->setObjectName("cancelProtonDownload");
    m_cancel->setEnabled(false);
    m_cancel->hide();
    m_close = new QPushButton("Close", this);
    buttons->addWidget(m_more);
    buttons->addStretch();
    buttons->addWidget(m_cancel);
    buttons->addWidget(m_close);
    layout->addLayout(buttons);
    connect(m_family, &QComboBox::currentIndexChanged, this, [this] { m_page = 1; load(); });
    connect(m_more, &QPushButton::clicked, this, [this] {
        if (m_versions.isEmpty()) { m_page = 1; load(); }
        else { ++m_page; load(true); }
    });
    connect(m_cancel, &QPushButton::clicked, this, [this] { cancelDownload(); });
    connect(m_close, &QPushButton::clicked, this, &ProtonManager::reject);
    QTimer::singleShot(0, this, [this] { load(); });
}

void ProtonManager::setBusy(bool busy) {
    m_busy = busy;
    m_family->setEnabled(!busy);
    m_table->setEnabled(!busy);
    m_more->setEnabled(!busy && m_hasMore);
    m_cancel->setEnabled(m_downloading);
    m_cancel->setVisible(m_downloading);
    m_progress->setVisible(m_downloading);
}

void ProtonManager::load(bool append) {
    if (m_busy) return;
    setBusy(true);
    m_status->setText("Loading upstream releases…");
    m_backend->request("proton_releases", {{"family", m_family->currentData().toString()}, {"page", m_page}},
        [this, append](const QJsonObject &data) {
            if (!append) m_versions = {};
            for (const auto &value : data.value("versions").toArray()) m_versions.append(value);
            m_hasMore = data.value("more").toBool();
            render();
            setBusy(false);
            m_status->setText("Install directory: " + data.value("directory").toString());
        }, [this, append](const QString &error) {
            if (append) --m_page;
            else { m_versions = {}; render(); }
            m_hasMore = true;
            setBusy(false);
            m_more->setText("Retry / load more");
            m_status->setText(error);
        });
}

void ProtonManager::render() {
    m_table->setRowCount(0);
    for (int row = 0; row < m_versions.size(); ++row) {
        const auto version = m_versions[row].toObject();
        const bool installed = version.value("installed").toBool();
        m_table->insertRow(row);
        auto *name = new QTableWidgetItem(version.value("label").toString());
        name->setToolTip(version.value("path").toString());
        m_table->setItem(row, 0, name);
        auto *downloadButton = new QPushButton(installed ? "Installed" : "Download", m_table);
        downloadButton->setEnabled(!installed);
        downloadButton->setToolTip(QString::number(version.value("size").toDouble() / 1048576.0, 'f', 1) + " MiB");
        m_table->setCellWidget(row, 1, downloadButton);
        connect(downloadButton, &QPushButton::clicked, this, [this, row] { download(row); });
        auto *use = new QPushButton("Use version", m_table);
        use->setEnabled(installed);
        m_table->setCellWidget(row, 2, use);
        connect(use, &QPushButton::clicked, this, [this, row] {
            const auto path = m_versions[row].toObject().value("path").toString();
            if (!QFileInfo::exists(path + "/proton")) { m_status->setText("This runner is no longer installed."); return; }
            m_selected = path;
            accept();
        });
    }
}

void ProtonManager::download(int row) {
    if (m_busy) return;
    const auto version = m_versions[row].toObject();
    m_downloading = true;
    setBusy(true);
    m_progress->setRange(0, 0);
    m_status->setText("Preparing download…");
    m_backend->request("download_proton", {{"family", m_family->currentData().toString()}, {"tag", version.value("tag")}},
        [this, row](const QJsonObject &data) {
            m_downloading = false;
            auto version = m_versions[row].toObject();
            version.insert("installed", true);
            version.insert("path", data.value("path"));
            m_versions[row] = version;
            render();
            setBusy(false);
            m_progress->setRange(0, 100);
            m_progress->setValue(100);
            m_progress->setFormat("Installed: %p%");
            m_status->setText("Installed: " + data.value("path").toString() + "\nLatest folders and your selection were not changed.");
        }, [this](const QString &error) {
            m_downloading = false;
            setBusy(false);
            m_progress->setRange(0, 100);
            m_progress->setValue(0);
            m_status->setText(error);
        }, [this](const QJsonObject &event) {
            const auto phase = event.value("phase").toString();
            const double done = event.value("bytes").toDouble(), total = event.value("total").toDouble();
            if (total > 0) {
                m_progress->setRange(0, 100);
                m_progress->setValue(qBound(0, int(done * 100 / total), 100));
            } else m_progress->setRange(0, 0);
            if (phase == "download") {
                m_progress->setFormat("Downloading: %p%");
                m_status->setText(QString("%1 / %2 MiB — %3 MiB/s")
                    .arg(done / 1048576.0, 0, 'f', 1).arg(total / 1048576.0, 0, 'f', 1)
                    .arg(event.value("speed").toDouble() / 1048576.0, 0, 'f', 1));
            } else {
                m_progress->setFormat(phase == "extract" ? "Extracting: %p%" : "%p%");
                m_status->setText(phase == "verify" ? "Verifying checksum…"
                    : phase == "extract" && event.contains("unpacked_bytes")
                        ? QString("Extracting: %1 entries — %2 MiB unpacked — %3 MiB/s")
                            .arg(event.value("files").toInt())
                            .arg(event.value("unpacked_bytes").toDouble() / 1048576.0, 0, 'f', 1)
                            .arg(event.value("speed").toDouble() / 1048576.0, 0, 'f', 1)
                    : phase == "extract" ? "Extracting and installing…" : "Download complete.");
            }
        });
}

void ProtonManager::cancelDownload() {
    if (!m_downloading) return;
    delete m_backend;
    m_backend = new BackendClient(m_directory, m_dataRoot, this);
    m_downloading = false;
    setBusy(false);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_status->setText("Download cancelled. Latest folders and existing runners are unchanged. Temporary files are cleaned automatically.");
}

void ProtonManager::reject() {
    if (m_downloading) {
        if (QMessageBox::question(this, "Cancel download?", "Cancel the current Proton download and close the manager?") != QMessageBox::Yes) return;
        cancelDownload();
    }
    QDialog::reject();
}

void ProtonManager::closeEvent(QCloseEvent *event) {
    if (m_downloading) {
        event->ignore();
        reject();
    } else QDialog::closeEvent(event);
}
