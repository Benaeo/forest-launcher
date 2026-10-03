#pragma once

#include <QDialog>
#include <QJsonObject>

class GameOptionsWidget;
class QLabel;
class QLineEdit;
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
    QLabel *m_pathLabel;
    QLineEdit *m_path;
    QPushButton *m_browse;
    GameOptionsWidget *m_options;
    QLabel *m_error;
};
