#pragma once

#include <QJsonObject>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QVBoxLayout;

class GameOptionsWidget : public QWidget {
public:
    GameOptionsWidget(const QJsonObject &options, const QJsonObject &bootstrap, QWidget *parent = nullptr,
                      bool defaultsEditor = false);
    QJsonObject optionsData() const;
    QComboBox *kindSelector() const { return m_kind; }
    QString kind() const;
    QString protonSelection() const;
    void setKind(const QString &kind);
    void addGeneralOption(QWidget *option);
private:
    void updateKind();
    QComboBox *m_kind;
    QLineEdit *m_prefix;
    QPushButton *m_prefixBrowse;
    QComboBox *m_proton;
    QString m_preservedProton;
    QLineEdit *m_arguments;
    QLineEdit *m_tags;
    QCheckBox *m_onlineFix;
    QCheckBox *m_mangohud;
    QCheckBox *m_preferSdl;
    QCheckBox *m_noSleep;
    QPlainTextEdit *m_environment;
    QVBoxLayout *m_generalOptions = nullptr;
};
