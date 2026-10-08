#pragma once

#include <QBuffer>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonObject>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextList>
#include <QUrl>

// Shared rendering for Help → News and the post-update release-note page.
namespace NewsRendering {
class Document final : public QTextDocument {
public:
    using QTextDocument::QTextDocument;
protected:
    // Returning a valid empty image prevents Qt's parent/filesystem fallback.
    // Only explicitly fetched and decoded images may be added as resources.
    QVariant loadResource(int, const QUrl &) override { return QVariant::fromValue(QImage()); }
};

inline void configure(QTextBrowser *browser) {
    auto palette = browser->palette();
    palette.setColor(QPalette::Link, QColor("#58a6ff"));
    palette.setColor(QPalette::LinkVisited, QColor("#58a6ff"));
    browser->setPalette(palette);
    auto *document = new Document(browser);
    document->setDocumentMargin(12);
    document->setDefaultStyleSheet(
        "a { color: #58a6ff; text-decoration: underline; } h1 { font-size: 24px; } "
        "h2 { font-size: 20px; } p { margin-top: 8px; margin-bottom: 8px; } "
        "li { margin-top: 4px; margin-bottom: 4px; }");
    browser->setDocument(document);
}

inline void render(QTextBrowser *browser, const QJsonObject &release) {
    Document source;
    source.setMarkdown(release.value("markdown").toString(), QTextDocument::MarkdownDialectGitHub);
    QString html = source.toHtml();
    for (const auto &value : release.value("fragments").toArray()) {
        const auto fragment = value.toObject();
        const auto marker = fragment.value("marker").toString();
        if (marker.isEmpty()) continue;
        const int position = html.indexOf(marker);
        if (position < 0) continue;
        const int start = html.lastIndexOf("<p", position), end = html.indexOf("</p>", position);
        if (start >= 0 && end >= 0)
            html.replace(start, end + 4 - start, fragment.value("html").toString());
    }
    browser->setHtml(html);
    for (auto block = browser->document()->begin(); block.isValid(); block = block.next()) {
        if (auto *list = block.textList()) {
            auto format = list->format();
            format.setStyle(format.indent() > 1 ? QTextListFormat::ListCircle : QTextListFormat::ListDisc);
            list->setFormat(format);
        }
    }
}

inline void addImages(QTextDocument *document, const QJsonObject &data) {
    for (const auto &value : data.value("images").toArray()) {
        const auto image = value.toObject();
        const int index = image.value("index").toInt(-1);
        if (index < 0 || index >= 12) continue;
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
        if (!decoded.isNull()) document->addResource(QTextDocument::ImageResource,
            QUrl("news-image:" + QString::number(index)), decoded);
    }
    document->markContentsDirty(0, document->characterCount());
}
}
