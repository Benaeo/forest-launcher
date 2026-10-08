#pragma once

#include <QDialog>
#include <QJsonArray>
#include <QString>

class UpdateAnnouncementDialog final : public QDialog {
public:
    explicit UpdateAnnouncementDialog(const QString &version, QWidget *parent = nullptr);
};

class ReleaseNotesDialog final : public QDialog {
public:
    ReleaseNotesDialog(const QString &installedVersion, const QJsonArray &releases, QWidget *parent = nullptr);
};
