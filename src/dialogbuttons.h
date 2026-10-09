#pragma once

#include <QBoxLayout>
#include <QEvent>
#include <QDialogButtonBox>
#include <QPushButton>

// Keep Qt's accept/reject roles and native styling, but use the entire footer.
class WideDialogButtons final : public QDialogButtonBox {
public:
    explicit WideDialogButtons(QWidget *parent = nullptr)
        : QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close, parent) {
        setObjectName("dialogFooterButtons");
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button(QDialogButtonBox::Save)->setDefault(true);
        button(QDialogButtonBox::Close)->setAutoDefault(false);
        expandButtons();
    }
protected:
    void changeEvent(QEvent *event) override {
        QDialogButtonBox::changeEvent(event);
        // Qt rebuilds its standard button layout when the native style changes.
        // Reapply our fixed Close | Save order and equal-width layout afterward.
        if (event->type() == QEvent::StyleChange || event->type() == QEvent::LayoutDirectionChange)
            expandButtons();
    }
    void showEvent(QShowEvent *event) override {
        QDialogButtonBox::showEvent(event);
        expandButtons();
    }
    void resizeEvent(QResizeEvent *event) override {
        QDialogButtonBox::resizeEvent(event);
        expandButtons();
    }
private:
    void expandButtons() {
        auto *row = qobject_cast<QBoxLayout *>(layout());
        if (!row) return;
        while (auto *item = row->takeAt(0)) delete item;
        row->setContentsMargins(0, 0, 0, 0);
        for (auto role : {QDialogButtonBox::Close, QDialogButtonBox::Save}) {
            auto *control = button(role);
            control->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            control->setMinimumHeight(34);
            row->addWidget(control, 1);
        }
    }
};
