#pragma once

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

class RemoveGameDialog final : public QDialog {
public:
    RemoveGameDialog(const QJsonObject &game, const QJsonObject &info, QWidget *parent = nullptr) : QDialog(parent) {
        setObjectName("removeGameDialog");
        setWindowTitle("Remove from library");
        setMinimumWidth(480);
        auto *layout = new QVBoxLayout(this);
        auto addText = [this, layout](const QString &text, const char *name) {
            auto *label = new QLabel(text, this);
            label->setObjectName(name);
            label->setTextFormat(Qt::PlainText);
            label->setWordWrap(true);
            layout->addWidget(label);
        };
        addText("Remove “" + game.value("title").toString() + "” from the library?\nForest-created shortcuts will also be removed.", "removeGameQuestion");
        auto prefix = info.value("prefix").toString();
        const auto home = QDir::homePath();
        if (prefix == home) prefix = "~";
        else if (prefix.startsWith(home + "/")) prefix = "~" + prefix.mid(home.size());
        m_delete = new QCheckBox("Also delete the prefix" + (prefix.isEmpty() ? QString() : ": " + prefix), this);
        m_delete->setObjectName("deletePrefixCheck");
        m_delete->setEnabled(info.value("can_delete").toBool());
        layout->addWidget(m_delete);
        addText("Unchecked: the prefix is kept. Checked: all files inside that prefix are permanently deleted.", "prefixDeletionWarning");
        if (!info.value("reason").toString().isEmpty()) addText(info.value("reason").toString(), "prefixDeletionReason");
        QStringList users;
        for (const auto &user : info.value("users").toArray()) users << user.toObject().value("title").toString();
        if (!users.isEmpty()) addText("Games using this prefix:\n• " + users.join("\n• "), "prefixUsers");
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Yes | QDialogButtonBox::No, this);
        buttons->button(QDialogButtonBox::No)->setDefault(true);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }
    bool deletePrefix() const { return m_delete->isEnabled() && m_delete->isChecked(); }
private:
    QCheckBox *m_delete;
};
