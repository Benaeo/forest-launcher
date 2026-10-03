#pragma once

#include <QDialog>
#include <QJsonArray>

class BackendClient;
class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QCloseEvent;

class ProtonManager : public QDialog {
public:
    ProtonManager(QString backendDirectory, QString dataRoot, QWidget *parent = nullptr);
    QString selectedVersion() const { return m_selected; }
protected:
    void reject() override;
    void closeEvent(QCloseEvent *event) override;
private:
    void load(bool append = false);
    void render();
    void download(int row);
    void cancelDownload();
    void setBusy(bool busy);
    QString m_directory;
    QString m_dataRoot;
    QString m_selected;
    BackendClient *m_backend;
    QComboBox *m_family;
    QTableWidget *m_table;
    QLabel *m_status;
    QProgressBar *m_progress;
    QPushButton *m_more;
    QPushButton *m_cancel;
    QPushButton *m_close;
    QJsonArray m_versions;
    bool m_busy = false;
    bool m_downloading = false;
    bool m_hasMore = false;
    int m_page = 1;
};
