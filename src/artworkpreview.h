#pragma once

#include "artworkui.h"
#include <QMap>
#include <QPainter>
#include <QPaintEvent>

// Optional presentation only: no labels, controls, search or storage logic.
class ArtworkPreview final : public QWidget {
public:
    explicit ArtworkPreview(QWidget *parent = nullptr) : QWidget(parent) {
        setObjectName("artworkPreview");
        setFixedSize(480, 174);
    }
    void setArtwork(const QJsonObject &artwork) {
        for (const auto &kind : {QString("icon"), QString("grid"), QString("hero"), QString("logo")}) {
            const auto path = artwork.value(kind).toString();
            if (m_paths.value(kind) == path) continue;
            m_paths.insert(kind, path);
            const auto image = path.isEmpty() ? QImage() : readArtworkImage(path);
            m_images.insert(kind, image.isNull() ? image : image.scaled(1000, 1000, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
        update();
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.fillRect(rect(), palette().brush(QPalette::Base));
        drawSlot(painter, QRect(0, 0, width(), 108), "hero", true);
        drawSlot(painter, QRect(14, 48, 210, 50), "logo", false);
        drawSlot(painter, QRect(180, 116, 56, 56), "icon", false);
        drawSlot(painter, QRect(250, 116, 38, 56), "grid", false);
    }
private:
    void drawSlot(QPainter &painter, const QRect &box, const QString &kind, bool crop) {
        const auto image = m_images.value(kind);
        if (image.isNull()) { painter.fillRect(box, palette().brush(QPalette::AlternateBase)); return; }
        const auto scaled = image.scaled(box.size(), crop ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio, Qt::SmoothTransformation);
        painter.save();
        painter.setClipRect(box);
        painter.drawImage(QPoint(box.center().x() - scaled.width() / 2, box.center().y() - scaled.height() / 2), scaled);
        painter.restore();
    }
    QMap<QString, QString> m_paths;
    QMap<QString, QImage> m_images;
};
