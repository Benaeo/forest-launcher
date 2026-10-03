#pragma once

#include <QDialog>
#include <QJsonObject>

class GameOptionsWidget;
class QCheckBox;

class SettingsDialog : public QDialog {
public:
    SettingsDialog(const QJsonObject &bootstrap, QWidget *parent = nullptr);
    QJsonObject settingsData() const;
private:
    QJsonObject m_original;
    QCheckBox *m_closeAfter;
    GameOptionsWidget *m_defaults;
};
