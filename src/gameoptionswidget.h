#pragma once

#include <QJsonObject>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

class GameOptionsWidget : public QWidget {
public:
    GameOptionsWidget(const QJsonObject &options, const QJsonObject &bootstrap, QWidget *parent = nullptr,
                      bool defaultsEditor = false);
    QJsonObject optionsData() const;
    QComboBox *kindSelector() const { return m_kind; }
    QString kind() const;
    QString protonSelection() const;
    void setKind(const QString &kind);
private:
    void updateKind();
    QComboBox *m_kind;
    QLineEdit *m_prefix;
    QPushButton *m_prefixBrowse;
    QComboBox *m_proton;
    QLineEdit *m_arguments;
    QLineEdit *m_tags;
    QCheckBox *m_onlineFix;
    QPlainTextEdit *m_environment;
};
