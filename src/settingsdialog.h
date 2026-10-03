#pragma once

#include <QDialog>
#include <QJsonObject>

class GameOptionsWidget;
class QCheckBox;
class QComboBox;
class QLineEdit;

class SettingsDialog : public QDialog {
public:
    SettingsDialog(const QJsonObject &bootstrap, QWidget *parent = nullptr);
    QJsonObject settingsData() const;
private:
    QJsonObject m_original;
    QLineEdit *m_prefixRoot;
    QCheckBox *m_closeAfter;
    GameOptionsWidget *m_defaults;
};
