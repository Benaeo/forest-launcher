#pragma once

#include <QDialog>
#include <QJsonObject>

class GameOptionsWidget;
class QCheckBox;
class QLineEdit;
class QComboBox;

class SettingsDialog : public QDialog {
public:
    SettingsDialog(const QJsonObject &bootstrap, QWidget *parent = nullptr);
    QJsonObject settingsData() const;
protected:
    void changeEvent(QEvent *event) override;
private:
    QJsonObject m_original;
    bool m_styleSaving = false;
    QCheckBox *m_closeAfter;
    QLineEdit *m_apiKey;
    QComboBox *m_iconSource;
    QComboBox *m_widgetStyle = nullptr;
    GameOptionsWidget *m_defaults;
};
