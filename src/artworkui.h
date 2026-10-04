#pragma once

#include "backendclient.h"
#include "dialogbuttons.h"
#include <QCheckBox>
#include <QFile>
#include <QImageReader>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <memory>

inline QImage readArtworkImage(const QString &path) {
    QImageReader::setAllocationLimit(64);
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const auto format = reader.format();
    if (format != "png" && format != "jpeg" && format != "webp" && format != "ico") return {};
    if (format == "ico") {
        int best = 0;
        qint64 area = -1;
        const int count = reader.imageCount();
        if (count <= 0 || count > 128) return {};
        for (int index = 0; index < count; ++index) {
            if (!reader.jumpToImage(index)) continue;
            const auto size = reader.size();
            const qint64 candidate = qint64(size.width()) * size.height();
            if (size.isValid() && candidate >= area) { best = index; area = candidate; }
        }
        if (!reader.jumpToImage(best)) return {};
    }
    const auto size = reader.size();
    if (!size.isValid() || size.width() > 8192 || size.height() > 8192
        || qint64(size.width()) * size.height() > 16000000) return {};
    return reader.read();
}

// Keep image parsing in native Qt; Python only validates/stores bounded bytes.
// Normalize selections to PNG so desktop and Linux Steam need no ICO decoder.
inline void importArtworkImage(BackendClient *client, const QString &path,
                               BackendClient::Success success, BackendClient::Failure failure) {
    const auto image = readArtworkImage(path);
    if (image.isNull()) { failure("Could not decode this image, or its dimensions exceed the limit."); return; }
    auto temporary = std::make_shared<QTemporaryDir>();
    const auto filename = temporary->filePath("image.png");
    if (!temporary->isValid() || !image.save(filename, "PNG")) { failure("Could not prepare the selected image."); return; }
    client->request("import_artwork", {{"path", filename}},
        [temporary, success](const QJsonObject &data) { success(data); },
        [temporary, failure](const QString &error) { failure(error); });
}

class ArtworkQuestion : public QDialog {
public:
    ArtworkQuestion(const QString &title, const QString &message, QWidget *parent = nullptr) : QDialog(parent) {
        setObjectName("artworkQuestion");
        setWindowTitle(title);
        setMinimumWidth(460);
        auto *layout = new QVBoxLayout(this);
        auto *label = new QLabel(message, this);
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        layout->addWidget(label);
        auto *buttons = new WideDialogButtons(this);
        buttons->button(QDialogButtonBox::Close)->setText("Cancel");
        buttons->button(QDialogButtonBox::Save)->setText("Continue");
        buttons->button(QDialogButtonBox::Save)->setDefault(false);
        buttons->button(QDialogButtonBox::Close)->setDefault(true);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }
};

class IconSourceDialog : public QDialog {
public:
    enum Choice { Extracted = 10, File = 11, SteamGridDB = 12 };
    explicit IconSourceDialog(QWidget *parent = nullptr) : QDialog(parent) {
        setObjectName("iconSourceDialog");
        setWindowTitle("Choose an icon source");
        setMinimumWidth(400);
        auto *layout = new QVBoxLayout(this);
        for (const auto &entry : {std::pair<QString, int>{"Use extracted icon", Extracted},
                                 {"Use icon from file", File}, {"Use icon from SteamGridDB", SteamGridDB}}) {
            auto *button = new QPushButton(entry.first, this);
            button->setObjectName(entry.second == Extracted ? "useExtractedIcon" : entry.second == File ? "useIconFile" : "useSteamGridDbIcon");
            button->setMinimumHeight(40);
            layout->addWidget(button);
            connect(button, &QPushButton::clicked, this, [this, value = entry.second] { done(value); });
        }
        auto *cancel = new QPushButton("Cancel", this);
        cancel->setMinimumHeight(34);
        layout->addWidget(cancel);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    }
};

class ArtworkKeyDialog : public QDialog {
public:
    QLineEdit *key;
    explicit ArtworkKeyDialog(QWidget *parent = nullptr) : QDialog(parent) {
        setObjectName("steamGridDbKeyDialog");
        setWindowTitle("SteamGridDB API key");
        setMinimumWidth(460);
        auto *layout = new QVBoxLayout(this);
        auto *message = new QLabel("Enter your SteamGridDB API key to browse artwork.\nSaved in Forest’s private settings; it is not encrypted.", this);
        message->setWordWrap(true);
        layout->addWidget(message);
        auto *link = new QLabel("<a href=\"https://www.steamgriddb.com/profile/preferences/api\">Get your API key</a>", this);
        link->setOpenExternalLinks(true);
        layout->addWidget(link);
        key = new QLineEdit(this);
        key->setObjectName("steamGridDbKeyInput");
        key->setEchoMode(QLineEdit::Password);
        layout->addWidget(key);
        auto *show = new QCheckBox("Show key", this);
        connect(show, &QCheckBox::toggled, this, [this](bool visible) { key->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password); });
        layout->addWidget(show);
        auto *buttons = new WideDialogButtons(this);
        buttons->button(QDialogButtonBox::Close)->setText("Cancel");
        buttons->button(QDialogButtonBox::Save)->setText("Continue");
        buttons->button(QDialogButtonBox::Save)->setEnabled(false);
        connect(key, &QLineEdit::textChanged, this, [buttons](const QString &text) {
            buttons->button(QDialogButtonBox::Save)->setEnabled(!text.trimmed().isEmpty());
        });
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);
    }
};
