#pragma once

#include <QDialog>
#include <QJsonObject>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

class GameDialog : public QDialog {
public:
    GameDialog(const QJsonObject &game, const QJsonObject &bootstrap, QWidget *parent = nullptr);
    QJsonObject gameData() const;
    void setExecutablePath(const QString &path);
private:
    void updateKind();
    void validateAndAccept();
    QJsonObject m_original;
    QLineEdit *m_title;
    QComboBox *m_kind;
    QLabel *m_pathLabel;
    QLineEdit *m_path;
    QPushButton *m_browse;
    QLabel *m_prefixLabel;
    QLineEdit *m_prefix;
    QPushButton *m_prefixBrowse;
    QLabel *m_protonLabel;
    QComboBox *m_proton;
    QLineEdit *m_arguments;
    QLineEdit *m_tags;
    QCheckBox *m_onlineFix;
    QPlainTextEdit *m_environment;
    QLabel *m_error;
};
