#pragma once

#include "backendclient.h"

#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

// Discovery runs in the backend before this dialog is ever shown.
class MissingDllDialog final : public QDialog {
public:
    MissingDllDialog(BackendClient *backend, const QString &id, const QString &title,
                     const QString &oldPath, QWidget *parent = nullptr) : QDialog(parent) {
        setObjectName("missingLosslessDllDialog");
        setWindowTitle("Lossless Scaling DLL missing");
        setMinimumWidth(580);
        auto *layout = new QVBoxLayout(this);
        layout->setSpacing(12);
        auto *description = new QLabel("The configured lsfg-vk.dll for " + title + " could not be found.", this);
        description->setTextFormat(Qt::PlainText);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *pathRow = new QHBoxLayout;
        auto *path = new QLineEdit(this);
        path->setObjectName("missingDllPath");
        path->setAccessibleName("Lossless Scaling DLL path");
        auto *browse = new QPushButton("Browse…", this);
        browse->setAutoDefault(false);
        pathRow->addWidget(path, 1);
        pathRow->addWidget(browse);
        layout->addLayout(pathRow);
        auto *warning = new QLabel(this);
        warning->setObjectName("missingDllWarning");
        warning->setTextFormat(Qt::PlainText);
        warning->setWordWrap(true);
        warning->hide();
        layout->addWidget(warning);
        auto *buttons = new QHBoxLayout;
        auto *cancel = new QPushButton("Cancel", this);
        auto *proceed = new QPushButton("Continue", this);
        cancel->setAutoDefault(false);
        proceed->setDefault(true);
        for (auto *button : {cancel, proceed}) {
            button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            button->setMinimumHeight(34);
            buttons->addWidget(button, 1);
        }
        layout->addLayout(buttons);
        const auto showWarning = [this, warning](const QString &message) {
            warning->setText(message);
            warning->show();
            this->layout()->activate();
            adjustSize();
        };
        connect(path, &QLineEdit::textEdited, this, [warning] { warning->hide(); });
        connect(browse, &QPushButton::clicked, this, [this, path, warning] {
            const auto selected = QFileDialog::getOpenFileName(this, "Choose lsfg-vk.dll", path->text(),
                "Lossless Scaling DLL (lsfg-vk.dll LSFG-VK.DLL);;DLL files (*.dll *.DLL);;All files (*)");
            if (!selected.isEmpty()) { path->setText(selected); warning->hide(); }
        });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        const QPointer<BackendClient> client(backend);
        connect(proceed, &QPushButton::clicked, this,
            [this, client, id, title, oldPath, path, browse, cancel, proceed, showWarning] {
            auto selected = path->text().trimmed();
            if (selected.startsWith("~/")) selected.replace(0, 1, QDir::homePath());
            const QFileInfo file(selected);
            if (selected.isEmpty() || !file.isFile() || !file.isReadable()
                || file.fileName().compare("lsfg-vk.dll", Qt::CaseInsensitive) != 0) {
                showWarning("⚠ Choose a valid lsfg-vk.dll location, or go back and disable Lossless Scaling for " + title + ".");
                path->setFocus();
                return;
            }
            if (!client) return;
            const auto enableControls = [path, browse, cancel, proceed](bool enabled) {
                for (auto *control : {static_cast<QWidget *>(path), static_cast<QWidget *>(browse),
                                      static_cast<QWidget *>(cancel), static_cast<QWidget *>(proceed)})
                    control->setEnabled(enabled);
            };
            enableControls(false);
            const QPointer<QDialog> guard(this);
            client->request("relocate_lossless_scaling", {{"id", id}, {"old_path", oldPath}, {"dll_path", file.absoluteFilePath()}},
                [guard](const QJsonObject &) { if (guard) guard->accept(); },
                [guard, enableControls, showWarning](const QString &error) {
                    if (!guard) return;
                    enableControls(true);
                    showWarning(error);
                });
        });
    }
};
