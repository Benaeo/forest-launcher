#pragma once

#include "backendclient.h"
#include "missingdlldialog.h"

#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QWidget>
#include <functional>
#include <utility>

// Shared by library Play and the windowless --launch shortcut entrypoint.
// DLL relocation is resolved before launching; Steam restart consent is scoped
// to the account/current Steam session and is never saved in settings.
inline void launchGameWithConfirmation(BackendClient *backend, QWidget *parent, const QString &id,
                                       BackendClient::Success success, BackendClient::Failure failure,
                                       std::function<void()> cancelled, const QJsonObject &consent = {}) {
    QJsonObject params{{"id", id}};
    if (!consent.isEmpty()) params.insert("steam_restart_consent", consent);
    const QPointer<BackendClient> clientGuard(backend);
    const QPointer<QWidget> parentGuard(parent);
    const bool hasParent = parent != nullptr;
    backend->request("launch_game", params,
        [clientGuard, parentGuard, hasParent, id, success = std::move(success),
         failure, cancelled = std::move(cancelled), consent](const QJsonObject &data) {
            if (!clientGuard || (hasParent && !parentGuard)) return;
            if (data.contains("lossless_confirmation")) {
                const auto missing = data.value("lossless_confirmation").toObject();
                const auto title = missing.value("title").toString();
                if (title.isEmpty() || !missing.value("old_path").isString()) {
                    failure("The backend returned an invalid DLL relocation request.");
                    return;
                }
                MissingDllDialog prompt(clientGuard.data(), id, title, missing.value("old_path").toString(), parentGuard.data());
                const auto result = prompt.exec();
                if (!clientGuard || (hasParent && !parentGuard)) return;
                if (result != QDialog::Accepted) { cancelled(); return; }
                launchGameWithConfirmation(clientGuard.data(), parentGuard.data(), id, success, failure, cancelled, consent);
                return;
            }
            if (!data.contains("steam_restart_confirmation")) {
                success(data);
                return;
            }
            const auto confirmation = data.value("steam_restart_confirmation").toObject();
            const auto account = confirmation.value("account").toString();
            const auto session = confirmation.value("session").toString();
            if (account.isEmpty() || session.isEmpty()) {
                failure("The backend returned an invalid Steam restart confirmation.");
                return;
            }
            const auto name = confirmation.value("name").toString(account);
            QMessageBox prompt(QMessageBox::Warning, "Switch Steam account",
                "Steam is open. Restart Steam on “" + name + "” (" + account + ") before launching this game?",
                QMessageBox::NoButton, parentGuard.data());
            prompt.setObjectName("steamRestartConfirmation");
            prompt.setTextFormat(Qt::PlainText);
            prompt.setInformativeText("This will close Steam and may interrupt running games or downloads. "
                                      "Save your progress first. Steam may ask you to sign in.\n\n"
                                      "Cancel leaves Steam untouched and does not start the game.");
            auto *accept = prompt.addButton("Accept", QMessageBox::AcceptRole);
            auto *cancel = prompt.addButton(QMessageBox::Cancel);
            prompt.setDefaultButton(cancel);
            prompt.setEscapeButton(cancel);
            prompt.exec();
            if (!clientGuard || (hasParent && !parentGuard)) return;
            if (prompt.clickedButton() != accept) {
                cancelled();
                return;
            }
            // Revalidate in the backend. If Steam's session/account requirement
            // changed while the popup was open, a new confirmation is necessary.
            launchGameWithConfirmation(clientGuard.data(), parentGuard.data(), id, success, failure, cancelled,
                                       {{"account", account}, {"session", session}});
        }, failure);
}
