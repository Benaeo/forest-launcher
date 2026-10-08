#include "startupnewsdialog.h"
#include "backendclient.h"
#include "newsrendering.h"

#include <QDesktopServices>
#include <QFont>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QAbstractTextDocumentLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QResizeEvent>
#include <QScrollArea>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <cmath>

namespace {
// Use the exact News renderer, with image requests handled by the dialog.
class ReleaseBody final : public QTextBrowser {
public:
    explicit ReleaseBody(const QJsonObject &release, QWidget *parent) : QTextBrowser(parent) {
        setObjectName("releaseBody");
        setFrameShape(QFrame::NoFrame);
        setOpenLinks(false);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        NewsRendering::configure(this);
        NewsRendering::render(this, release);
        const auto url = QUrl(release.value("url").toString());
        if (safeLink(url)) {
            auto cursor = textCursor();
            cursor.movePosition(QTextCursor::End);
            cursor.insertHtml("<p><a href=\"" + url.toString().toHtmlEscaped() + "\">View release on GitHub</a></p>");
        }
        connect(this, &QTextBrowser::anchorClicked, this, [](const QUrl &url) {
            if (safeLink(url)) QDesktopServices::openUrl(url);
        });
        connect(document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged,
                this, [this] { fitHeight(); });
        fitHeight();
    }
protected:
    QVariant loadResource(int, const QUrl &) override { return {}; }
    void resizeEvent(QResizeEvent *event) override {
        QTextBrowser::resizeEvent(event);
        fitHeight();
    }
private:
    static bool safeLink(const QUrl &url) {
        return url.scheme() == "https" && !url.host().isEmpty() && url.userInfo().isEmpty();
    }
    void fitHeight() {
        if (m_fitting) return;
        m_fitting = true;
        document()->setTextWidth(qMax(1, viewport()->width()));
        setFixedHeight(static_cast<int>(std::ceil(document()->size().height())));
        m_fitting = false;
    }
    bool m_fitting = false;
};
}

UpdateAnnouncementDialog::UpdateAnnouncementDialog(const QString &version, QWidget *parent) : QDialog(parent) {
    setWindowTitle("Forest Launcher — update complete");
    setObjectName("updateAnnouncement");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);
    auto *heading = new QLabel("Forest Launcher has been updated to v" + version, this);
    heading->setObjectName("updateAnnouncementHeading");
    heading->setTextFormat(Qt::PlainText);
    auto font = heading->font();
    font.setBold(true);
    font.setPointSize(font.pointSize() + 2);
    heading->setFont(font);
    heading->setWordWrap(false);
    heading->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    layout->addWidget(heading);
    auto *description = new QLabel("Stay up to date with what’s new by reading the release notes.", this);
    description->setWordWrap(false);
    description->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    layout->addWidget(description);
    auto *actions = new QHBoxLayout;
    actions->setSpacing(8);
    auto *dismiss = new QPushButton("Dismiss", this);
    dismiss->setObjectName("updateDismiss");
    auto *read = new QPushButton("Read news", this);
    read->setObjectName("updateReadNews");
    dismiss->setMinimumHeight(28);
    read->setMinimumHeight(28);
    actions->addWidget(dismiss, 1);
    actions->addWidget(read, 1);
    layout->addLayout(actions);
    connect(dismiss, &QPushButton::clicked, this, &QDialog::reject);
    connect(read, &QPushButton::clicked, this, &QDialog::accept);
    read->setDefault(true);
    layout->activate();
    const QSize compact = layout->sizeHint();
    setMinimumSize(compact);
    resize(compact);
}

ReleaseNotesDialog::ReleaseNotesDialog(const QString &installedVersion, const QJsonArray &releases, QWidget *parent,
                                     const QString &backendDirectory, const QString &dataRoot)
    : QDialog(parent) {
    setWindowTitle("Release notes — Forest Launcher");
    setObjectName("postUpdateNews");
    resize(640, 620);
    auto *layout = new QVBoxLayout(this);
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName("postUpdateNewsScrollArea");
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *page = new QWidget(scroll);
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(16, 16, 16, 16);
    pageLayout->setSpacing(12);
    for (const auto &value : releases) {
        const auto release = value.toObject();
        const auto version = release.value("version").toString();
        auto *section = new QWidget(page);
        auto *sectionLayout = new QVBoxLayout(section);
        sectionLayout->setContentsMargins(0, 0, 0, 0);
        auto *heading = new QToolButton(section);
        heading->setObjectName("releaseHeading");
        heading->setText("v" + version + (version == installedVersion ? "  ·  Installed" : ""));
        heading->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        heading->setCheckable(true);
        heading->setChecked(true);
        heading->setArrowType(Qt::DownArrow);
        heading->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        heading->setMinimumHeight(38);
        heading->setToolTip("Click to collapse or expand these release notes.");
        auto font = heading->font();
        font.setBold(true);
        font.setPointSize(font.pointSize() + 1);
        heading->setFont(font);
        sectionLayout->addWidget(heading);
        auto *body = new ReleaseBody(release, section);
        sectionLayout->addWidget(body);
        const auto urls = release.value("images").toArray();
        if (!urls.isEmpty()) m_imageRequests.append(ImageRequest{body, urls});
        connect(heading, &QToolButton::toggled, section, [heading, body](bool expanded) {
            heading->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
            body->setVisible(expanded);
        });
        pageLayout->addWidget(section);
    }
    pageLayout->addStretch();
    scroll->setWidget(page);
    layout->addWidget(scroll, 1);
    auto *close = new QPushButton("Close", this);
    close->setObjectName("releaseNotesClose");
    close->setMinimumHeight(34);
    layout->addWidget(close);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    if (!backendDirectory.isEmpty() && !m_imageRequests.isEmpty()) {
        m_backend = new BackendClient(backendDirectory, dataRoot, this);
        QTimer::singleShot(0, this, [this] { loadNextImages(); });
    }
}

void ReleaseNotesDialog::loadNextImages() {
    if (!m_backend || m_imageRequests.isEmpty()) return;
    const auto request = m_imageRequests.takeFirst();
    QPointer<ReleaseNotesDialog> guard(this);
    // Sequential, bounded image requests keep long update histories responsive
    // and avoid spawning one download process for every release at once.
    m_backend->request("news_images", {{"urls", request.urls}},
        [guard, browser = request.browser](const QJsonObject &data) {
            if (!guard) return;
            if (browser) {
                NewsRendering::addImages(browser->document(), data);
                browser->viewport()->update();
                if (data.value("missing").toInt()) browser->setToolTip("Some release images could not be loaded.");
            }
            guard->loadNextImages();
        }, [guard, browser = request.browser](const QString &) {
            if (!guard) return;
            if (browser) browser->setToolTip("Release images could not be loaded. Check your connection.");
            guard->loadNextImages();
        });
}
