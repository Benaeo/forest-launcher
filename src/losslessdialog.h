#pragma once

#include "backendclient.h"
#include "dialogbuttons.h"

#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

class LosslessDialog final : public QDialog {
public:
    LosslessDialog(const QJsonObject &options, const QJsonObject &frontend, QWidget *parent = nullptr)
        : QDialog(parent) {
        setObjectName("losslessScalingDialog");
        setWindowTitle("Lossless Scaling");
        setMinimumWidth(480);
        auto *layout = new QVBoxLayout(this);
        layout->addWidget(new QLabel("Lossless Scaling Location", this));
        auto *pathRow = new QHBoxLayout;
        m_path = new QLineEdit(options.value("dll_path").toString(), this);
        m_path->setObjectName("losslessDllPath");
        m_path->setPlaceholderText("Full path to lsfg-vk.dll");
        m_path->setToolTip(m_path->text());
        auto *browse = new QPushButton("Browse…", this);
        browse->setObjectName("losslessBrowseButton");
        browse->setAutoDefault(false);
        pathRow->addWidget(m_path, 1);
        pathRow->addWidget(browse);
        layout->addLayout(pathRow);
        layout->addWidget(new QLabel("Multiplier", this));
        auto *multiplierRow = new QHBoxLayout;
        m_multiplier = new QSpinBox(this);
        m_multiplier->setObjectName("losslessMultiplier");
        m_multiplier->setRange(1, 100);
        m_multiplier->setButtonSymbols(QAbstractSpinBox::NoButtons);
        m_multiplier->setValue(options.value("multiplier").toInt(1));
        m_multiplier->setToolTip("1 = off. 2 or more = on. The multiplier includes the original rendered frame.");
        multiplierRow->addWidget(m_multiplier, 1);
        for (const bool increase : {false, true}) {
            auto *button = new QToolButton(this);
            button->setObjectName(increase ? "losslessMultiplierPlus" : "losslessMultiplierMinus");
            button->setText(increase ? "+" : "−");
            button->setToolTip(increase ? "Increase multiplier" : "Decrease multiplier");
            button->setAutoRepeat(true);
            multiplierRow->addWidget(button);
            connect(button, &QToolButton::clicked, this, [this, increase] {
                if (increase) m_multiplier->stepUp(); else m_multiplier->stepDown();
            });
        }
        layout->addLayout(multiplierRow);
        layout->addWidget(new QLabel("Flow Scale", this));
        auto *flowRow = new QHBoxLayout;
        m_flow = new QSpinBox(this);
        m_flow->setObjectName("losslessFlowScale");
        m_flow->setRange(25, 100);
        m_flow->setSuffix("%");
        m_flow->setValue(options.value("flow_scale").toInt(100));
        m_flow->setMinimumWidth(m_flow->sizeHint().width());
        m_flow->setToolTip("Motion-estimation resolution. Lower values improve performance at the cost of quality.");
        auto *slider = new QSlider(Qt::Horizontal, this);
        slider->setObjectName("losslessFlowSlider");
        slider->setRange(25, 100);
        slider->setValue(m_flow->value());
        slider->setToolTip(m_flow->toolTip());
        connect(slider, &QSlider::valueChanged, m_flow, &QSpinBox::setValue);
        connect(m_flow, &QSpinBox::valueChanged, slider, &QSlider::setValue);
        flowRow->addWidget(m_flow);
        flowRow->addWidget(slider, 1);
        layout->addLayout(flowRow);
        m_performance = new QCheckBox("Performance Mode", this);
        m_performance->setObjectName("losslessPerformanceMode");
        m_performance->setChecked(options.value("performance_mode").toBool());
        m_performance->setToolTip("Use a lighter frame-generation model for better performance with a quality trade-off.");
        layout->addWidget(m_performance);
        m_status = new QLabel(this);
        m_status->setObjectName("losslessStatus");
        m_status->setTextFormat(Qt::PlainText);
        m_status->setWordWrap(true);
        m_status->hide();
        layout->addWidget(m_status);
        auto *buttons = new WideDialogButtons(this);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(buttons, &QDialogButtonBox::accepted, this, [this] {
            m_multiplier->interpretText();
            m_flow->interpretText();
            if (m_multiplier->value() > 1) {
                const QFileInfo file(location());
                if (!file.isFile() || !file.isReadable() || file.fileName().compare("lsfg-vk.dll", Qt::CaseInsensitive) != 0) {
                    showStatus("Choose an existing, readable lsfg-vk.dll to enable Lossless Scaling.");
                    return;
                }
            }
            accept();
        });
        connect(browse, &QPushButton::clicked, this, [this] {
            const auto file = QFileDialog::getOpenFileName(this, "Choose lsfg-vk.dll", m_path->text(),
                "Lossless Scaling DLL (lsfg-vk.dll LSFG-VK.DLL);;DLL files (*.dll *.DLL);;All files (*)");
            if (!file.isEmpty()) m_path->setText(file);
        });
        connect(m_path, &QLineEdit::textChanged, this, [this] {
            ++m_pathRevision;
            m_status->hide();
            m_path->setToolTip(m_path->text());
        });
        if (m_path->text().isEmpty() && !frontend.value("backend").toString().isEmpty()) {
            auto *backend = new BackendClient(frontend.value("backend").toString(), frontend.value("data_root").toString(), this);
            QTimer::singleShot(0, this, [this, backend] {
                if (m_pathRevision != 0) return;
                showStatus("Searching for lsfg-vk.dll…");
                const auto revision = m_pathRevision;
                backend->request("discover_lossless_scaling", {},
                    [this, revision](const QJsonObject &result) {
                        // Never overwrite typing, Browse, or an explicitly cleared path.
                        if (revision != m_pathRevision) return;
                        const auto path = result.value("dll_path").toString();
                        if (!path.isEmpty()) {
                            m_path->setText(path);
                            m_status->hide();
                        } else showStatus(result.value("limited").toBool()
                            ? "Automatic search reached its limit. Use Browse to choose lsfg-vk.dll."
                            : "lsfg-vk.dll was not found. Use Browse to choose it.");
                    }, [this, revision](const QString &error) {
                        if (revision == m_pathRevision) showStatus(error);
                    });
            });
        }
    }

    QJsonObject optionsData() const {
        return {{"dll_path", location()}, {"multiplier", m_multiplier->value()},
                {"flow_scale", m_flow->value()}, {"performance_mode", m_performance->isChecked()}};
    }

private:
    QString location() const {
        auto path = m_path->text().trimmed();
        if (path.isEmpty()) return {};
        if (path == "~" || path.startsWith("~/")) path = QDir::homePath() + path.mid(1);
        return QFileInfo(path).absoluteFilePath();
    }
    void showStatus(const QString &text) { m_status->setText(text); m_status->show(); }
    QLineEdit *m_path;
    QSpinBox *m_multiplier;
    QSpinBox *m_flow;
    QCheckBox *m_performance;
    QLabel *m_status;
    int m_pathRevision = 0;
};
