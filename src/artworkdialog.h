#pragma once

#include "artworkui.h"
#include <QJsonArray>
#include <QListWidget>
#include <QQueue>
#include <QScrollBar>
#include <QSet>
#include <QTimer>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QEvent>
#include <QMap>
#include <QPushButton>

// Image-only browser. Matching titles are combined internally; the user never
// chooses a database game. Steam artwork clicks advance icon -> grid -> hero ->
// logo, with Back/Next for review and an explicit Apply to confirm the draft.
class ArtworkDialog : public QDialog {
public:
    ArtworkDialog(QJsonObject bootstrap, QString title, QJsonObject artwork,
                  QWidget *parent = nullptr, bool sequence = false, int gameId = 0)
        : QDialog(parent), m_bootstrap(std::move(bootstrap)), m_artwork(std::move(artwork)),
          m_identityTitle(title.trimmed()), m_gameId(gameId), m_sequence(sequence) {
        setObjectName("artworkDialog");
        resize(1040, 720);
        setMinimumSize(660, 470);
        m_layout = new QVBoxLayout(this);
        m_search = new QLineEdit(title, this);
        m_search->setObjectName("artworkSearch");
        m_search->setClearButtonEnabled(true);
        m_search->addAction(QIcon::fromTheme("edit-find"), QLineEdit::LeadingPosition);
        m_search->setPlaceholderText("Search…");
        m_search->setMaxLength(256);
        m_layout->addWidget(m_search);
        if (m_sequence) {
            auto *navigation = new QHBoxLayout;
            m_back = new QPushButton("Back", this);
            m_back->setObjectName("artworkBack");
            m_back->setAutoDefault(false);
            m_page = new QLabel(this);
            m_page->setAlignment(Qt::AlignCenter);
            m_next = new QPushButton("Next", this);
            m_next->setObjectName("artworkNext");
            m_next->setAutoDefault(false);
            navigation->addWidget(m_back);
            navigation->addWidget(m_page, 1);
            navigation->addWidget(m_next);
            m_layout->addLayout(navigation);
            connect(m_back, &QPushButton::clicked, this, [this] { goToStep(m_step - 1); });
            connect(m_next, &QPushButton::clicked, this, [this] { goToStep(m_step + 1); });
        }
        m_list = new QListWidget(this);
        m_list->setObjectName("artworkImages");
        m_list->setViewMode(QListView::IconMode);
        m_list->setResizeMode(QListView::Adjust);
        m_list->setMovement(QListView::Static);
        m_list->setIconSize(QSize(150, 150));
        m_list->setSpacing(6);
        m_list->setUniformItemSizes(true);
        m_layout->addWidget(m_list, 1);
        m_status = new QLabel(m_list->viewport());
        m_status->setObjectName("artworkStatus");
        m_status->setAlignment(Qt::AlignCenter);
        m_status->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_list->viewport()->installEventFilter(this);
        status("Press Enter to search.");
        m_error = new QLabel(this);
        m_error->setObjectName("artworkError");
        m_error->setTextFormat(Qt::PlainText);
        m_error->setWordWrap(true);
        m_error->hide();
        m_layout->addWidget(m_error);
        m_selectionStatus = new QLabel(this);
        m_selectionStatus->setTextFormat(Qt::PlainText);
        m_selectionStatus->setWordWrap(true);
        m_selectionStatus->hide();
        m_layout->addWidget(m_selectionStatus);
        m_retrySelections = new QPushButton("Retry selected image downloads", this);
        m_retrySelections->setAutoDefault(false);
        m_retrySelections->hide();
        m_layout->addWidget(m_retrySelections);
        if (m_sequence) {
            auto *footer = new QHBoxLayout;
            auto *close = new QPushButton("Close", this);
            close->setObjectName("artworkClose");
            close->setAutoDefault(false);
            m_apply = new QPushButton("Apply", this);
            m_apply->setObjectName("artworkApply");
            m_apply->setAutoDefault(false);
            footer->addWidget(close, 1);
            footer->addWidget(m_apply, 1);
            m_layout->addLayout(footer);
            connect(close, &QPushButton::clicked, this, &QDialog::reject);
            connect(m_apply, &QPushButton::clicked, this, [this] { if (canApply()) accept(); });
        }
        const auto frontend = m_bootstrap.value("frontend").toObject();
        m_selectionBackend = new BackendClient(frontend.value("backend").toString(), frontend.value("data_root").toString(), this);
        connect(m_retrySelections, &QPushButton::clicked, this, [this] {
            const auto failed = m_selectionFailures.keys();
            m_selectionFailures.clear();
            for (const auto &kind : failed) downloadSelection(kind, m_selectionUrls.value(kind));
            updateSelectionStatus();
        });
        connect(this, &QDialog::finished, this, [this] {
            m_selectionClosed = true;
            m_selectionBackend->deleteLater();
        });
        resetClient();
        connect(m_search, &QLineEdit::returnPressed, this, [this] { search(m_search->text().trimmed()); });
        connect(m_list->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
            if (value >= m_list->verticalScrollBar()->maximum() && !m_loading && !m_selecting) loadMore();
        });
        connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) { select(item); });
        updateTitle();
        updateSelectionStatus();
    }
    QJsonObject artworkData() const { return m_artwork; }
    QJsonObject settingsData() const { return m_bootstrap.value("settings").toObject(); }
    QString currentKind() const { return kinds().at(m_step); }
    bool isSequence() const { return m_sequence; }
    void addPreview(QWidget *widget) { m_layout->insertWidget(1, widget, 0, Qt::AlignHCenter); }
    std::function<void(const QJsonObject &)> selectionChanged;
    void startSearch() {
        m_search->setFocus();
        QTimer::singleShot(0, this, [this] { search(m_search->text().trimmed()); });
    }
protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (object == m_list->viewport() && event->type() == QEvent::Resize) m_status->setGeometry(m_list->viewport()->rect());
        return QDialog::eventFilter(object, event);
    }
private:
    static QStringList kinds() { return {"icon", "grid", "hero", "logo"}; }
    void updateTitle() {
        setWindowTitle(currentKind() == "icon" ? "Choose an icon" : currentKind() == "grid" ? "Choose a grid"
            : currentKind() == "hero" ? "Choose a hero / banner" : "Choose a logo");
        if (m_sequence) {
            const QStringList names{"Icon", "Grid", "Hero / banner", "Logo"};
            m_page->setText(QString("%1 of 4 — %2").arg(m_step + 1).arg(names.at(m_step)));
            m_back->setEnabled(m_step > 0);
            m_next->setEnabled(m_step < kinds().size() - 1);
        }
    }
    void goToStep(int step) {
        if (!m_sequence || m_selectionClosed || m_keyPrompt || step < 0 || step >= kinds().size() || step == m_step) return;
        m_step = step;
        updateTitle();
        search(m_query, true);
    }
    bool canApply() const {
        if (m_selectionClosed || !m_pendingSelections.isEmpty() || !m_selectionFailures.isEmpty()) return false;
        for (const auto &kind : kinds()) if (m_artwork.value(kind).toString().isEmpty()) return false;
        return true;
    }
    void status(const QString &message) { m_status->setText(message); m_status->setGeometry(m_list->viewport()->rect()); m_status->setVisible(!message.isEmpty()); }
    QString plural() const { return currentKind() == "hero" ? "banners" : currentKind() + "s"; }
    void error(const QString &message) { m_error->setText(message); m_error->setVisible(!message.isEmpty()); if (!message.isEmpty()) { m_failed = true; status({}); } }
    void resetClient() {
        if (m_backend) m_backend->deleteLater();
        const auto frontend = m_bootstrap.value("frontend").toObject();
        m_backend = new BackendClient(frontend.value("backend").toString(), frontend.value("data_root").toString(), this);
    }
    void invalidate() {
        ++m_epoch;
        resetClient();
        m_loading = true;
        m_games.clear();
        m_list->clear();
        m_thumbnails.clear();
        m_candidates.clear();
        m_rows.clear();
        m_seen.clear();
        m_failed = false;
        m_loading = false;
        m_imagesActive = m_thumbnailsActive = 0;
        error({});
    }
    bool ensureKey() {
        if (!m_bootstrap.value("settings").toObject().value("steamgriddb_api_key").toString().isEmpty()) return true;
        if (m_keyPrompt) return false;
        m_keyPrompt = true;
        ArtworkKeyDialog prompt(this);
        if (prompt.exec() != QDialog::Accepted) { m_keyPrompt = false; return false; }
        const int epoch = m_epoch;
        m_backend->request("save_settings", {{"settings", QJsonObject{{"steamgriddb_api_key", prompt.key->text().trimmed()}}}},
            [this, epoch](const QJsonObject &data) {
                m_keyPrompt = false;
                m_bootstrap.insert("settings", data.value("settings"));
                if (epoch == m_epoch) { const auto query = m_waitingQuery; m_waitingQuery.clear(); search(query); }
            }, [this](const QString &message) { m_keyPrompt = false; error(message); });
        return false;
    }
    void search(const QString &query, bool nextCategory = false) {
        if (m_selecting || m_keyPrompt) return;
        if (!nextCategory && query == m_query && !m_failed) return;
        if (query.isEmpty()) { invalidate(); m_query.clear(); status("Press Enter to search."); return; }
        m_waitingQuery = query;
        if (!ensureKey()) return;
        invalidate();
        m_query = query;
        if (restoreCache()) return;
        const int epoch = m_epoch;
        if (m_gameId > 0 && query == m_identityTitle) {
            m_games.append({m_gameId, 0, true});
            loadMore();
            return;
        }
        m_loading = true;
        status("Searching " + plural() + "…");
        m_backend->request("artwork_search", {{"query", query}}, [this, epoch](const QJsonObject &data) {
            if (epoch != m_epoch) return;
            for (const auto &value : data.value("games").toArray()) {
                const auto id = value.toObject().value("id").toInt();
                if (id > 0 && m_games.size() < 100) m_games.append({id, 0, true});
            }
            m_loading = false;
            if (m_games.isEmpty()) finishLoading();
            else loadMore();
        }, [this, epoch](const QString &message) { if (epoch == m_epoch) { m_loading = false; error(message); } });
    }
    void loadMore() {
        if (m_loading || m_games.isEmpty() || m_selecting || m_list->count() >= 2000) return;
        m_loading = true;
        if (m_list->count() == 0) status("Loading " + plural() + "…");
        m_candidates.clear();
        for (int index = 0; index < m_games.size(); ++index)
            if (m_games[index].more) m_candidates.enqueue(index);
        if (m_candidates.isEmpty()) { m_loading = false; return; }
        pumpImages(m_epoch);
    }
    void pumpImages(int epoch) {
        while (epoch == m_epoch && m_imagesActive < 3 && !m_candidates.isEmpty()) {
            const int index = m_candidates.dequeue();
            const auto game = m_games[index];
            ++m_imagesActive;
            m_backend->request("artwork_images", {{"game_id", game.id}, {"kind", currentKind()}, {"page", game.page}},
                [this, epoch, index](const QJsonObject &data) {
                    if (epoch != m_epoch) return;
                    const auto images = data.value("images").toArray();
                    m_games[index].more = images.size() == 30 && m_games[index].page < 1000;
                    ++m_games[index].page;
                    for (const auto &value : images) {
                        const auto image = value.toObject();
                        const auto url = image.value("url").toString();
                        if (m_seen.contains(url) || m_list->count() >= 2000) continue;
                        m_seen.insert(url);
                        m_rows.append({url, image.value("thumb").toString(), {}});
                        auto *item = new QListWidgetItem(m_list);
                        item->setData(Qt::UserRole, url);
                        item->setData(Qt::UserRole + 1, image.value("thumb"));
                        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
                        item->setSizeHint(QSize(164, 164));
                        m_thumbnails.enqueue(m_list->count() - 1);
                    }
                    --m_imagesActive;
                    pumpImages(epoch);
                    pumpThumbnails(epoch);
                }, [this, epoch, index](const QString &message) {
                    if (epoch != m_epoch) return;
                    m_games[index].more = false;
                    --m_imagesActive;
                    error(message);
                    pumpImages(epoch);
                });
        }
        if (m_imagesActive == 0 && m_candidates.isEmpty()) {
            m_loading = false;
            finishLoading();
        }
    }
    void pumpThumbnails(int epoch) {
        while (epoch == m_epoch && m_thumbnailsActive < 3 && !m_thumbnails.isEmpty()) {
            const int row = m_thumbnails.dequeue();
            ++m_thumbnailsActive;
            m_backend->request("artwork_download", {{"url", m_list->item(row)->data(Qt::UserRole + 1).toString()}},
                [this, epoch, row](const QJsonObject &data) {
                    if (epoch != m_epoch) return;
                    const auto image = readArtworkImage(data.value("path").toString());
                    if (auto *item = m_list->item(row); item && !image.isNull()) {
                        item->setIcon(QPixmap::fromImage(image.scaled(QSize(150, 150), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
                        item->setFlags(item->flags() | Qt::ItemIsEnabled);
                        m_rows[row].path = data.value("path").toString();
                        status({});
                    } else { error("Could not decode the downloaded thumbnail."); }
                    --m_thumbnailsActive;
                    pumpThumbnails(epoch);
                    finishLoading();
                }, [this, epoch](const QString &message) { if (epoch == m_epoch) { --m_thumbnailsActive; error(message); pumpThumbnails(epoch); finishLoading(); } });
        }
    }
    void select(QListWidgetItem *item) {
        if (m_selecting || m_selectionClosed || !(item->flags() & Qt::ItemIsEnabled)) return;
        const auto selectedKind = currentKind();
        const auto url = item->data(Qt::UserRole).toString();
        m_selectionUrls.insert(selectedKind, url);
        // Move on before downloading/decoding the full image. Browser resets must
        // never cancel selected-image requests, which have their own backend.
        if (m_sequence) {
            if (m_step < 3) goToStep(m_step + 1);
        } else {
            m_selecting = true;
            m_list->setEnabled(false);
            m_search->setEnabled(false);
        }
        downloadSelection(selectedKind, url);
    }
    void downloadSelection(const QString &kind, const QString &url) {
        const int revision = ++m_selectionRevisions[kind];
        m_pendingSelections.insert(kind);
        m_selectionFailures.remove(kind);
        updateSelectionStatus();
        m_selectionBackend->request("artwork_download", {{"url", url}},
            [this, kind, revision](const QJsonObject &data) {
                if (!isCurrentSelection(kind, revision)) return;
                importArtworkImage(m_selectionBackend, data.value("path").toString(),
                    [this, kind, revision](const QJsonObject &normalized) {
                        if (!isCurrentSelection(kind, revision)) return;
                        m_artwork.insert(kind, normalized.value("path"));
                        m_pendingSelections.remove(kind);
                        if (selectionChanged) selectionChanged(m_artwork);
                        updateSelectionStatus();
                    }, [this, kind, revision](const QString &message) { selectionError(kind, revision, message); });
            }, [this, kind, revision](const QString &message) { selectionError(kind, revision, message); });
    }
    bool isCurrentSelection(const QString &kind, int revision) const {
        // Back/reselect may finish a newer download before an older one.
        return !m_selectionClosed && m_selectionRevisions.value(kind) == revision;
    }
    void selectionError(const QString &kind, int revision, const QString &message) {
        if (!isCurrentSelection(kind, revision)) return;
        m_pendingSelections.remove(kind);
        m_selectionFailures.insert(kind, message);
        updateSelectionStatus();
    }
    void updateSelectionStatus() {
        if (m_selectionClosed) return;
        QStringList failures;
        for (auto it = m_selectionFailures.cbegin(); it != m_selectionFailures.cend(); ++it)
            failures.append(it.key() + ": " + it.value());
        QString message = !failures.isEmpty() ? failures.join("\n")
            : !m_pendingSelections.isEmpty() ? QString("Finishing selected image downloads…") : QString();
        if (m_sequence) {
            m_apply->setEnabled(canApply());
            if (message.isEmpty()) {
                int count = 0;
                for (const auto &kind : kinds()) if (!m_artwork.value(kind).toString().isEmpty()) ++count;
                if (count < 4) message = QString("%1 of 4 selected.").arg(count);
            }
        }
        m_selectionStatus->setText(message);
        m_selectionStatus->setVisible(!message.isEmpty());
        m_retrySelections->setVisible(!m_selectionFailures.isEmpty());
        if (m_selecting) {
            status(!m_pendingSelections.isEmpty() && m_selectionFailures.isEmpty()
                ? "Finishing selected image downloads…" : QString());
            if (m_pendingSelections.isEmpty() && m_selectionFailures.isEmpty()) accept();
        }
    }
    struct Game { int id, page; bool more; };
    struct Row { QString url, thumb, path; };
    struct Cached { QString key; QList<Game> games; QList<Row> rows; qsizetype bytes; };
    inline static QList<Cached> s_cache; // Application-session only, metadata/paths, no credentials or pixmaps.
    QString cacheKey() const {
        const auto frontend = m_bootstrap.value("frontend").toObject();
        QJsonArray scope{frontend.value("backend"), frontend.value("data_root"),
            m_bootstrap.value("settings").toObject().value("steamgriddb_api_key"), m_query, currentKind(),
            m_query == m_identityTitle ? m_gameId : 0};
        return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(scope).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
    }
    void finishLoading() {
        if (m_loading || m_imagesActive || m_thumbnailsActive || !m_candidates.isEmpty() || !m_thumbnails.isEmpty()) return;
        if (m_rows.isEmpty() && !m_failed) status("No " + plural() + " found.");
        if (m_failed || m_query.isEmpty()) return;
        qsizetype bytes = m_games.size() * 32 + 128;
        for (const auto &row : m_rows) bytes += 2 * (row.url.size() + row.thumb.size() + row.path.size()) + 128;
        if (bytes > 8 * 1024 * 1024) return;
        const auto key = cacheKey();
        for (int i = s_cache.size() - 1; i >= 0; --i) if (s_cache[i].key == key) s_cache.removeAt(i);
        s_cache.append({key, m_games, m_rows, bytes});
        qsizetype total = 0;
        for (const auto &entry : s_cache) total += entry.bytes;
        while (s_cache.size() > 32 || total > 8 * 1024 * 1024) { total -= s_cache.first().bytes; s_cache.removeFirst(); }
    }
    bool restoreCache() {
        const auto key = cacheKey();
        for (int index = 0; index < s_cache.size(); ++index) {
            if (s_cache[index].key != key) continue;
            const auto entry = s_cache.takeAt(index);
            s_cache.append(entry);
            m_loading = true;
            m_games = entry.games;
            m_rows = entry.rows;
            for (int row = 0; row < m_rows.size(); ++row) {
                const auto &cached = m_rows[row];
                auto *item = new QListWidgetItem(m_list);
                item->setSizeHint(QSize(164, 164));
                item->setData(Qt::UserRole, cached.url);
                item->setData(Qt::UserRole + 1, cached.thumb);
                m_seen.insert(cached.url);
                const auto image = readArtworkImage(cached.path);
                if (!image.isNull()) item->setIcon(QPixmap::fromImage(image.scaled(QSize(150, 150), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
                else { item->setFlags(item->flags() & ~Qt::ItemIsEnabled); m_thumbnails.enqueue(row); }
            }
            m_loading = false;
            status(m_rows.isEmpty() ? "No " + plural() + " found." : m_thumbnails.isEmpty() ? QString() : "Loading " + plural() + "…");
            pumpThumbnails(m_epoch);
            return true;
        }
        return false;
    }
    QJsonObject m_bootstrap, m_artwork;
    QVBoxLayout *m_layout;
    QLineEdit *m_search;
    QListWidget *m_list;
    QLabel *m_error, *m_status;
    QLabel *m_selectionStatus;
    QPushButton *m_retrySelections;
    QPushButton *m_back = nullptr, *m_next = nullptr, *m_apply = nullptr;
    QLabel *m_page = nullptr;
    BackendClient *m_selectionBackend;
    QMap<QString, QString> m_selectionUrls, m_selectionFailures;
    QMap<QString, int> m_selectionRevisions;
    QSet<QString> m_pendingSelections;
    bool m_selectionClosed = false;
    QString m_query, m_waitingQuery;
    QString m_identityTitle;
    int m_gameId = 0;
    QList<Row> m_rows;
    BackendClient *m_backend = nullptr;
    QList<Game> m_games;
    QQueue<int> m_candidates, m_thumbnails;
    QSet<QString> m_seen;
    int m_epoch = 0, m_step = 0, m_imagesActive = 0, m_thumbnailsActive = 0;
    bool m_sequence = false, m_loading = false, m_selecting = false, m_keyPrompt = false, m_failed = false;
};
