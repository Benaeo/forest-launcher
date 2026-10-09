#pragma once

#include <QAbstractItemModel>
#include <QEvent>
#include <QListWidget>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShowEvent>
#include <QStyle>
#include <QTimer>

// Keep native item rendering/navigation while centering the entire column grid.
// A short final row starts at the same columns, not at a new centered origin.
class ArtworkList final : public QListWidget {
public:
    explicit ArtworkList(QWidget *parent = nullptr) : QListWidget(parent) {
        setViewMode(QListView::IconMode);
        setResizeMode(QListView::Adjust);
        setMovement(QListView::Static);
        setFlow(QListView::LeftToRight);
        setWrapping(true);
        setUniformItemSizes(true);
        setSpacing(0); // Gaps are included in the explicit grid cells.
        setItemAlignment(Qt::AlignCenter);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        // Reserve this space from the outset, avoiding column changes as images load.
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        setBackgroundRole(QPalette::Base);
        setAutoFillBackground(true);
        connect(model(), &QAbstractItemModel::rowsInserted, this, [this] { scheduleCenter(); });
        connect(model(), &QAbstractItemModel::rowsRemoved, this, [this] { scheduleCenter(); });
        connect(model(), &QAbstractItemModel::modelReset, this, [this] { scheduleCenter(); });
    }
    void setThumbnailSize(const QSize &size) {
        setIconSize(size);
        setGridSize(cellSize(size));
        scheduleCenter();
    }
    QSize tileSize() const { return iconSize() + QSize(14, 14); }
    QSize minimumSizeHint() const override {
        // Centering margins are disposable space, not a new minimum after enlarging.
        const int scrollbar = style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, this);
        return {widthForColumns(1, iconSize()), 2 * frameWidth() + 2 * scrollbar};
    }
    int widthForColumns(int columns, const QSize &thumbnail) const {
        const auto margins = contentsMargins();
        const int frame = qMax(2 * frameWidth(), margins.left() + margins.right());
        const int scrollbar = qMax(verticalScrollBar()->sizeHint().width(),
            style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, verticalScrollBar()));
        return columns * cellSize(thumbnail).width() + layoutSlack + frame + scrollbar;
    }
protected:
    void resizeEvent(QResizeEvent *event) override {
        QListWidget::resizeEvent(event);
        scheduleCenter();
    }
    void showEvent(QShowEvent *event) override {
        QListWidget::showEvent(event);
        scheduleCenter();
    }
    void changeEvent(QEvent *event) override {
        QListWidget::changeEvent(event);
        if (event->type() == QEvent::StyleChange || event->type() == QEvent::LayoutDirectionChange
            || event->type() == QEvent::FontChange) scheduleCenter();
    }
    bool viewportEvent(QEvent *event) override {
        const bool result = QListWidget::viewportEvent(event);
        if (event->type() == QEvent::Resize) scheduleCenter();
        return result;
    }
private:
    // Qt's icon-mode wrap compares the cell end against QRect::right(), which
    // is width - 1. An exact N-cell viewport therefore needs this extra pixel.
    static constexpr int layoutSlack = 1;
    static QSize cellSize(const QSize &thumbnail) { return thumbnail + QSize(26, 26); }
    void scheduleCenter() {
        if (m_scheduled || m_centering) return;
        m_scheduled = true;
        QTimer::singleShot(0, this, [this] {
            m_scheduled = false;
            centerGrid();
        });
    }
    void centerGrid() {
        const int cellWidth = gridSize().width();
        if (cellWidth <= 0) return;
        m_centering = true;
        const auto margins = viewportMargins();
        const int available = viewport()->width() + margins.left() + margins.right();
        // Fit as many complete cells as the original artwork area allows;
        // only the unavoidable remainder becomes balanced side space.
        int columns = qMax(1, (available - layoutSlack) / cellWidth);
        if (count() > 0) columns = qMin(columns, count());
        const int remainder = qMax(0, available - columns * cellWidth - layoutSlack);
        const int left = remainder / 2;
        setViewportMargins(left, 0, remainder - left, 0);
        doItemsLayout();
        m_centering = false;
    }
    bool m_scheduled = false;
    bool m_centering = false;
};
