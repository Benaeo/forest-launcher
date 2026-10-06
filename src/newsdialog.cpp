#include "newsdialog.h"
#include "backendclient.h"

#include <QBuffer>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QImageReader>
#include <QLabel>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextList>
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
    auto palette = m_news->palette();
    palette.setColor(QPalette::Link, QColor("#58a6ff"));
    palette.setColor(QPalette::LinkVisited, QColor("#58a6ff"));
    m_news->setPalette(palette);
    m_news->document()->setDocumentMargin(12);
    m_news->document()->setDefaultStyleSheet(
        "a { color: #58a6ff; text-decoration: underline; } h1 { font-size: 24px; } "
        "h2 { font-size: 20px; } p { margin-top: 8px; margin-bottom: 8px; } "
        "li { margin-top: 4px; margin-bottom: 4px; }");
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
    QTextDocument document;
    document.setMarkdown(release.value("markdown").toString(), QTextDocument::MarkdownDialectGitHub);
    QString html = document.toHtml();
    for (const auto &value : release.value("fragments").toArray()) {
        const auto fragment = value.toObject();
        const auto marker = fragment.value("marker").toString();
        const int position = html.indexOf(marker);
        if (position < 0) continue;
        const int start = html.lastIndexOf("<p", position), end = html.indexOf("</p>", position);
        if (start >= 0 && end >= 0)
            html.replace(start, end + 4 - start, fragment.value("html").toString());
    }
    m_news->setHtml(html);
    for (auto block = m_news->document()->begin(); block.isValid(); block = block.next()) {
        if (auto *list = block.textList()) {
            auto format = list->format();
            format.setStyle(format.indent() > 1 ? QTextListFormat::ListCircle : QTextListFormat::ListDisc);
            list->setFormat(format);
        }
    }
}

void NewsDialog::showRelease(int row) {
    if (row < 0 || row >= m_releases.size()) return;
    const auto release = m_releases.at(row).toObject();
    const auto generation = ++m_generation;
    m_title->setText("<a style=\"color:#58a6ff\" href=\"" + release.value("url").toString().toHtmlEscaped()
                     + "\">" + release.value("title").toString().toHtmlEscaped() + "</a>");
    auto *document = new QTextDocument(m_news);
    document->setDefaultStyleSheet(m_news->document()->defaultStyleSheet());
    document->setDocumentMargin(12);
    m_news->setDocument(document);
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
            for (const auto &value : data.value("images").toArray()) {
                const auto image = value.toObject();
                auto bytes = QByteArray::fromBase64(image.value("data").toString().toLatin1());
                if (bytes.size() > 4 * 1024 * 1024) continue;
                QBuffer buffer(&bytes);
                buffer.open(QIODevice::ReadOnly);
                QImageReader reader(&buffer);
                const auto size = reader.size();
                if (!size.isValid() || size.width() > 8192 || size.height() > 8192
                    || qint64(size.width()) * size.height() > 16 * 1024 * 1024) continue;
                const auto format = reader.format().toLower();
                if (format != "png" && format != "jpeg" && format != "jpg" && format != "webp" && format != "gif") continue;
                reader.setScaledSize(size.scaled(640, 640, Qt::KeepAspectRatio));
                const auto decoded = reader.read();
                if (!decoded.isNull()) guard->m_news->document()->addResource(QTextDocument::ImageResource,
                    QUrl("news-image:" + QString::number(image.value("index").toInt())), decoded);
            }
            guard->m_news->document()->markContentsDirty(0, guard->m_news->document()->characterCount());
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
