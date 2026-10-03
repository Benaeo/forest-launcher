#pragma once

#include <QComboBox>
#include <QStyleOptionComboBox>
#include <QStylePainter>

// The layout determines the width, never the length of an installed runner name.
class ElidingComboBox final : public QComboBox {
public:
    explicit ElidingComboBox(QWidget *parent = nullptr) : QComboBox(parent) {
        setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        setMinimumContentsLength(18);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    QString displayedText() const {
        QStyleOptionComboBox option;
        initStyleOption(&option);
        auto field = style()->subControlRect(QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxEditField, this);
        int available = field.width() - 4;
        if (!option.currentIcon.isNull()) available -= option.iconSize.width() + 4;
        return fontMetrics().elidedText(currentText(), Qt::ElideRight, qMax(0, available));
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QStylePainter painter(this);
        QStyleOptionComboBox option;
        initStyleOption(&option);
        painter.drawComplexControl(QStyle::CC_ComboBox, option);
        option.currentText = displayedText();
        painter.drawControl(QStyle::CE_ComboBoxLabel, option);
    }
};
