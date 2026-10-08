#pragma once

#include <QDialog>
#include <QJsonObject>

class QLineEdit;

class WelcomeDialog final : public QDialog {
public:
    explicit WelcomeDialog(const QJsonObject &bootstrap, QWidget *parent = nullptr);
    QJsonObject settingsData() const;
private:
    QJsonObject m_draft;
    QLineEdit *m_key;
};
