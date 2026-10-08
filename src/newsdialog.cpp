#include "newsdialog.h"
#include "backendclient.h"
#include "newsrendering.h"

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

namespace {
// Never let release Markdown read local files or make unbounded remote requests.
class NewsBrowser final : public QTextBrowser {
public:
    using QTextBrowser::QTextBrowser;
protected:
    QVariant loadResource(int, const QUrl &) override { return {}; }
};
void openLink(const QUrl &url) {
    if (url.scheme() == "https" && !url.host().isEmpty() && url.userInfo().isEmpty())
        QDesktopServices::openUrl(url);
}
}

NewsDialog::NewsDialog(const QString &backendDirectory, const QString &dataRoot, QWidget *parent)
    : QDialog(parent), m_backend(new BackendClient(backendDirectory, dataRoot, this)) {
    setObjectName("newsDialog");
    setWindowTitle("News — Forest Launcher");
    resize(900, 760);
    auto *layout = new QVBoxLayout(this);
    m_title = new QLabel(this);
    m_title->setObjectName("newsReleaseTitle");
    m_title->setAlignment(Qt::AlignCenter);
    m_title->setTextInteractionFlags(Qt::TextBrowserInteraction);
    connect(m_title, &QLabel::linkActivated, this, [](const QString &url) { openLink(QUrl(url)); });
    layout->addWidget(m_title);
    m_news = new NewsBrowser(this);
    m_news->setObjectName("newsText");
    m_news->setOpenLinks(false);
    m_news->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_news->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_news->setFrameShape(QFrame::StyledPanel);
    NewsRendering::configure(m_news);
    connect(m_news, &QTextBrowser::anchorClicked, this, &openLink);
    layout->addWidget(m_news, 1);
    m_list = new QListWidget(this);
    m_list->setObjectName("newsReleaseList");
    m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerItem);
    m_list->setUniformItemSizes(true);
    layout->addWidget(m_list);
    sizeReleaseList();
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        showRelease(row);
        if (m_more && row == m_list->count() - 1) load(true);
    });
    m_status = new QLabel(this);
    m_status->setObjectName("newsStatus");
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    auto *footer = new QHBoxLayout;
    m_retry = new QPushButton("Retry", this);
    m_retry->hide();
    footer->addWidget(m_retry);
    connect(m_retry, &QPushButton::clicked, this, [this] { load(!m_releases.isEmpty()); });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    footer->addWidget(buttons, 1);
    layout->addLayout(footer);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    QTimer::singleShot(0, this, [this] { load(); });
}

void NewsDialog::sizeReleaseList() {
    const int rowHeight = m_list->count() ? m_list->sizeHintForRow(0) : m_list->fontMetrics().height() + 4;
    for (int i = 0; i < m_list->count(); ++i)
        m_list->item(i)->setSizeHint(QSize(0, rowHeight));
    const auto margins = m_list->contentsMargins();
    m_list->setFixedHeight(3 * rowHeight + margins.top() + margins.bottom());
}

void NewsDialog::load(bool append) {
    if (m_loading) return;
    m_loading = true;
    m_retry->hide();
    m_status->setText("Loading releases…");
    const int page = append ? m_page + 1 : 1;
    QPointer<NewsDialog> guard(this);
    m_backend->request("news_releases", {{"page", page}},
        [guard, append, page](const QJsonObject &data) {
            if (!guard) return;
            auto *self = guard.data();
            self->m_loading = false;
            self->m_page = page;
            self->m_more = data.value("more").toBool() && page < 100;
            if (!append) {
                self->m_list->clear();
                self->m_releases = {};
            }
            for (const auto &value : data.value("releases").toArray()) {
                const auto release = value.toObject();
                bool duplicate = false;
                for (const auto &old : self->m_releases)
                    if (old.toObject().value("url") == release.value("url")) duplicate = true;
                if (duplicate) continue;
                self->m_releases.append(release);
                self->m_list->addItem(release.value("title").toString());
                self->m_list->item(self->m_list->count() - 1)->setToolTip(release.value("title").toString());
            }
            self->sizeReleaseList();
            self->m_status->clear();
            self->m_status->hide();
            if (self->m_releases.isEmpty()) {
                self->m_news->setPlainText("No releases published yet.");
            } else if (!append) self->m_list->setCurrentRow(0);
            if (self->m_more) {
                self->m_retry->setText("Load more releases");
                self->m_retry->show();
            }
        }, [guard](const QString &error) {
            if (!guard) return;
            guard->m_loading = false;
            guard->m_status->setText(error);
            guard->m_status->show();
            guard->m_retry->setText("Retry");
            guard->m_retry->show();
        });
    m_status->show();
}

void NewsDialog::render(const QJsonObject &release) {
    NewsRendering::render(m_news, release);
}

void NewsDialog::showRelease(int row) {
    if (row < 0 || row >= m_releases.size()) return;
    const auto release = m_releases.at(row).toObject();
    const auto generation = ++m_generation;
    m_title->setText("<a style=\"color:#58a6ff\" href=\"" + release.value("url").toString().toHtmlEscaped()
                     + "\">" + release.value("title").toString().toHtmlEscaped() + "</a>");
    NewsRendering::configure(m_news);
    render(release);
    m_news->moveCursor(QTextCursor::Start);
    const auto urls = release.value("images").toArray();
    if (urls.isEmpty() || m_loadingImages) return;
    m_loadingImages = true;
    QPointer<NewsDialog> guard(this);
    m_backend->request("news_images", {{"urls", urls}},
        [guard, generation](const QJsonObject &data) {
            if (!guard) return;
            guard->m_loadingImages = false;
            if (guard->m_generation != generation) {
                guard->showRelease(guard->m_list->currentRow());
                return;
            }
            NewsRendering::addImages(guard->m_news->document(), data);
            guard->m_news->viewport()->update();
            if (data.value("missing").toInt()) {
                guard->m_status->setText("Some release images could not be loaded.");
                guard->m_status->show();
            }
        }, [guard, generation](const QString &) {
            if (!guard) return;
            guard->m_loadingImages = false;
            if (guard->m_generation != generation) {
                guard->showRelease(guard->m_list->currentRow());
                return;
            }
            guard->m_status->setText("Release images could not be loaded.");
            guard->m_status->show();
        });
}
