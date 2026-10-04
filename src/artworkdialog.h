#pragma once
#include "artworkui.h"
#include <QFileDialog>
#include <QHBoxLayout>
#include <QStyle>

// Local/extracted icon selection; online artwork browsing is a later checkpoint.
class ArtworkDialog : public QDialog {
public:
    ArtworkDialog(QJsonObject bootstrap, const QString &, QJsonObject artwork, QWidget *parent = nullptr)
        : QDialog(parent), m_bootstrap(std::move(bootstrap)), m_artwork(std::move(artwork)) {
        setObjectName("artworkDialog");
        setWindowTitle("Choose game icon");
        setMinimumWidth(460);
        auto *layout = new QVBoxLayout(this);
        m_image = new QLabel(this);
        m_image->setFixedHeight(140);
        m_image->setAlignment(Qt::AlignCenter);
        layout->addWidget(m_image);
        auto *row = new QHBoxLayout;
        auto *extracted = new QPushButton("Extracted icon", this);
        extracted->setObjectName("artworkExtractedIcon");
        extracted->setEnabled(!m_artwork.value("extracted_icon").toString().isEmpty());
        auto *local = new QPushButton("Choose local image…", this);
        local->setObjectName("artworkLocalImage");
        row->addWidget(extracted);
        row->addWidget(local);
        layout->addLayout(row);
        m_status = new QLabel("Choose the cached extracted icon or a local image.", this);
        m_status->setWordWrap(true);
        m_status->setTextFormat(Qt::PlainText);
        layout->addWidget(m_status);
        m_buttons = new WideDialogButtons(this);
        layout->addWidget(m_buttons);
        const auto frontend = m_bootstrap.value("frontend").toObject();
        m_backend = new BackendClient(frontend.value("backend").toString(), frontend.value("data_root").toString(), this);
        connect(extracted, &QPushButton::clicked, this, [this] {
            m_artwork.insert("icon", m_artwork.value("extracted_icon"));
            updateImage();
        });
        connect(local, &QPushButton::clicked, this, [this] {
            const auto path = QFileDialog::getOpenFileName(this, "Choose icon", {}, "Images (*.png *.jpg *.jpeg *.webp *.ico);;All files (*)");
            if (path.isEmpty()) return;
            m_buttons->button(QDialogButtonBox::Save)->setEnabled(false);
            importArtworkImage(m_backend, path, [this](const QJsonObject &data) {
                m_artwork.insert("icon", data.value("path"));
                updateImage();
                m_buttons->button(QDialogButtonBox::Save)->setEnabled(true);
            }, [this](const QString &error) {
                m_status->setText(error);
                m_buttons->button(QDialogButtonBox::Save)->setEnabled(true);
            });
        });
        connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        updateImage();
    }
    QJsonObject artworkData() const { return m_artwork; }
    QJsonObject settingsData() const { return m_bootstrap.value("settings").toObject(); }
private:
    void updateImage() {
        const auto image = readArtworkImage(m_artwork.value("icon").toString());
        m_image->setPixmap(image.isNull() ? style()->standardIcon(QStyle::SP_ComputerIcon).pixmap(96, 96)
                                         : QPixmap::fromImage(image).scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    QJsonObject m_bootstrap, m_artwork;
    QLabel *m_image, *m_status;
    BackendClient *m_backend;
    WideDialogButtons *m_buttons;
};
