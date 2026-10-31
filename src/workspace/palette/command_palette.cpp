#include "command_palette.h"

#include "file_index.h"

#include <QElapsedTimer>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPainter>
#include <QSignalBlocker>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTextLayout>
#include <QTextOption>
#include <QVBoxLayout>
#include <QVariantList>

namespace vedit::workspace {
namespace {

constexpr int kRowHeight = 26;
constexpr int kMaxResults = 100;

// Draws one result row: the display path with the matched characters in the
// accent colour, so the user can see *why* a file matched (Lapce behaviour).
class PaletteItemDelegate final : public QStyledItemDelegate {
public:
    explicit PaletteItemDelegate(QObject *parent)
        : QStyledItemDelegate(parent)
    {
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        painter->save();
        painter->setClipRect(option.rect);

        const bool selected = (option.state & QStyle::State_Selected) != 0;
        painter->fillRect(option.rect, QColor(selected ? QStringLiteral("#3b4252")
                                                        : QStringLiteral("#1f2430")));

        const QString text = index.data(Qt::DisplayRole).toString();
        if (!text.isEmpty()) {
            QFont font = option.font;
            font.setPointSizeF(font.pointSizeF() + 0.5);
            const QFontMetrics metrics(font);
            const QColor baseColor(selected ? QStringLiteral("#eceff4")
                                            : QStringLiteral("#d8dee9"));
            const QColor accentColor(QStringLiteral("#88c0d0"));
            const QVariantList rawPositions = index.data(Qt::UserRole + 1).toList();

            QTextOption textOption;
            textOption.setWrapMode(QTextOption::NoWrap);
            QTextLayout layout(text, font);
            layout.setTextOption(textOption);

            QVector<QTextLayout::FormatRange> formats;
            int rangeStart = -1;
            int rangeEnd = -2;
            const auto flushRange = [&formats, &rangeStart, &rangeEnd, &accentColor]() {
                if (rangeStart < 0) {
                    return;
                }
                QTextCharFormat format;
                format.setForeground(accentColor);
                format.setFontWeight(QFont::DemiBold);
                formats.append(QTextLayout::FormatRange{
                    static_cast<int>(rangeStart), static_cast<int>(rangeEnd - rangeStart + 1),
                    format});
                rangeStart = -1;
                rangeEnd = -2;
            };
            for (const QVariant &value : rawPositions) {
                const int position = value.toInt();
                if (position < 0) {
                    continue;
                }
                if (position == rangeEnd + 1) {
                    rangeEnd = position;
                    continue;
                }
                flushRange();
                rangeStart = position;
                rangeEnd = position;
            }
            flushRange();
            layout.setFormats(formats);

            layout.beginLayout();
            QTextLine line = layout.createLine();
            if (line.isValid()) {
                line.setLineWidth(option.rect.width() - 2 * 12);
            }
            layout.endLayout();

            const qreal verticalPadding =
                option.rect.height() > metrics.height()
                    ? (option.rect.height() - metrics.height()) / 2.0
                    : 0.0;
            painter->setPen(baseColor);
            layout.draw(painter, QPointF(option.rect.left() + 12,
                                         option.rect.top() + verticalPadding));
        }
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return QSize(0, kRowHeight);
    }
};

} // namespace

CommandPalette::CommandPalette(QWidget *parent)
    : QDialog(parent, Qt::Popup | Qt::FramelessWindowHint)
{
    setObjectName(QStringLiteral("command-palette"));
    setMinimumSize(560, 320);
    resize(720, 420);
    setStyleSheet(QStringLiteral(
        "QDialog#command-palette { background: #1f2430; border: 1px solid #3b4252; }"
        "QLineEdit#palette-query { background: #272c36; color: #e5e9f0; border: none;"
        " border-bottom: 1px solid #3b4252; padding: 8px 12px; font-size: 14px;"
        " selection-background-color: #4c566a; }"
        "QListWidget#palette-results { background: #1f2430; color: #d8dee9; border: none;"
        " outline: none; }"
        "QListWidget#palette-results::item:selected { background: #3b4252; }"
        "QLabel#palette-empty { color: #7b8397; font-size: 13px; }"
        "QLabel#palette-status { color: #7b8397; font-size: 11px; background: #1a1f28;"
        " padding: 4px 12px; border-top: 1px solid #2b303b; }"
        "QScrollBar:vertical { background: #1f2430; width: 10px; margin: 0; }"
        "QScrollBar::handle:vertical { background: #3b4252; min-height: 24px;"
        " border-radius: 4px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);

    query_ = new QLineEdit(this);
    query_->setObjectName(QStringLiteral("palette-query"));
    query_->setPlaceholderText(QStringLiteral("Go to file…"));
    query_->setClearButtonEnabled(true);
    query_->setMinimumHeight(36);
    query_->installEventFilter(this);
    layout->addWidget(query_);

    results_ = new QListWidget(this);
    results_->setObjectName(QStringLiteral("palette-results"));
    results_->setUniformItemSizes(true);
    results_->setSelectionMode(QAbstractItemView::SingleSelection);
    results_->setFocusPolicy(Qt::NoFocus);
    results_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    results_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    results_->setItemDelegate(new PaletteItemDelegate(results_));
    layout->addWidget(results_, 1);

    empty_ = new QLabel(noResultsText(), this);
    empty_->setObjectName(QStringLiteral("palette-empty"));
    empty_->setAlignment(Qt::AlignCenter);
    empty_->setWordWrap(true);
    empty_->setVisible(false);
    layout->addWidget(empty_, 1);

    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("palette-status"));
    layout->addWidget(status_);

    connect(query_, &QLineEdit::textChanged, this,
            [this](const QString &) { refreshResults(); });
    connect(results_, &QListWidget::itemActivated, this,
            [this](QListWidgetItem *) { activateCurrent(); });
}

void CommandPalette::setFileIndex(const WorkspaceFileIndex *index)
{
    index_ = index;
    if (isVisible()) {
        refreshResults();
    }
}

const WorkspaceFileIndex *CommandPalette::fileIndex() const
{
    return index_;
}

void CommandPalette::setOpenHandler(std::function<void(const QString &)> handler)
{
    openHandler_ = std::move(handler);
}

void CommandPalette::setMaxResults(int maxResults)
{
    maxResults_ = maxResults > 0 ? maxResults : kMaxResults;
}

int CommandPalette::maxResults() const
{
    return maxResults_;
}

void CommandPalette::openPalette(const QString &initialQuery)
{
    if (!isVisible()) {
        showOverParent();
    }
    if (query_->text() != initialQuery) {
        query_->setText(initialQuery); // textChanged refreshes the list
    } else {
        refreshResults();
    }
    query_->setFocus(Qt::ShortcutFocusReason);
    query_->setCursorPosition(static_cast<int>(query_->text().size()));
}

void CommandPalette::closePalette()
{
    hide();
}

QString CommandPalette::queryText() const
{
    return query_->text();
}

void CommandPalette::setQueryText(const QString &text)
{
    {
        const QSignalBlocker blocker(query_);
        query_->setText(text);
    }
    refreshResults();
}

QLineEdit *CommandPalette::queryEdit() const
{
    return query_;
}

int CommandPalette::resultCount() const
{
    return static_cast<int>(matches_.size());
}

QStringList CommandPalette::visibleResults() const
{
    QStringList result;
    result.reserve(results_->count());
    for (int row = 0; row < results_->count(); ++row) {
        result.append(results_->item(row)->text());
    }
    return result;
}

int CommandPalette::currentRow() const
{
    return results_->currentRow();
}

void CommandPalette::moveSelection(int delta)
{
    const int count = results_->count();
    if (count == 0 || delta == 0) {
        return;
    }
    int row = results_->currentRow();
    if (row < 0) {
        row = delta > 0 ? -1 : 0;
    }
    row = (row + delta) % count;
    if (row < 0) {
        row += count;
    }
    results_->setCurrentRow(row);
    if (QListWidgetItem *item = results_->item(row)) {
        results_->scrollToItem(item, QAbstractItemView::EnsureVisible);
    }
}

void CommandPalette::activateCurrent()
{
    if (matches_.isEmpty() || results_->isHidden()) {
        return;
    }
    const int row = results_->currentRow();
    if (row < 0 || row >= matches_.size()) {
        return;
    }
    const int candidate = matches_.at(row).index;
    const QString absolute = index_ != nullptr ? index_->absolutePath(candidate) : QString();
    closePalette();
    if (!absolute.isEmpty() && openHandler_) {
        openHandler_(absolute);
    }
}

bool CommandPalette::emptyStateVisible() const
{
    return empty_->isVisibleTo(this);
}

QString CommandPalette::emptyStateText() const
{
    return empty_->text();
}

qint64 CommandPalette::lastFilterMicros() const
{
    return lastFilterMicros_;
}

QString CommandPalette::noResultsText()
{
    return QStringLiteral("No matching files");
}

bool CommandPalette::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == query_ && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        switch (key->key()) {
        case Qt::Key_Down:
            moveSelection(1);
            return true;
        case Qt::Key_Up:
            moveSelection(-1);
            return true;
        case Qt::Key_PageDown:
            moveSelection(10);
            return true;
        case Qt::Key_PageUp:
            moveSelection(-10);
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            activateCurrent();
            return true;
        case Qt::Key_Escape:
            closePalette();
            return true;
        default:
            break;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void CommandPalette::showOverParent()
{
    if (QWidget *owner = parentWidget()) {
        const QRect windowRect = owner->window()->geometry();
        const int width = qBound(560, windowRect.width() - 160, 900);
        resize(width, 420);
        const int x = windowRect.center().x() - width / 2;
        const int y = windowRect.top() + qMax(48, windowRect.height() / 10);
        move(x, y);
    } else {
        resize(720, 420);
    }
    show();
    raise();
    activateWindow();
}

void CommandPalette::refreshResults()
{
    QElapsedTimer timer;
    timer.start();

    const QString query = query_->text();
    matches_.clear();
    if (index_ != nullptr) {
        matches_ = FuzzyMatcher::filter(query, index_->displayPaths(), index_->basenames(),
                                        maxResults_);
    }

    results_->setUpdatesEnabled(false);
    results_->clear();
    for (int row = 0; row < matches_.size(); ++row) {
        const int candidate = matches_.at(row).index;
        const QString &display = index_->displayPaths().at(candidate);
        auto *item = new QListWidgetItem(display, results_);
        item->setData(Qt::UserRole, candidate);

        QVariantList positions;
        if (!query.isEmpty()) {
            FuzzyMatch match;
            if (FuzzyMatcher::match(query, display, index_->basenames().at(candidate), &match)) {
                positions.reserve(match.positionCount);
                for (int index = 0; index < match.positionCount; ++index) {
                    positions.append(match.positions[static_cast<std::size_t>(index)]);
                }
            }
        }
        item->setData(Qt::UserRole + 1, positions);
    }
    results_->setUpdatesEnabled(true);
    if (results_->count() > 0) {
        results_->setCurrentRow(0);
    }

    lastFilterMicros_ = timer.nsecsElapsed() / 1000;
    updateEmptyState();
    updateStatus();
}

void CommandPalette::updateEmptyState()
{
    QString message;
    if (index_ == nullptr) {
        message = QStringLiteral("No project open — open a project root to jump between files.");
    } else if (index_->isEmpty()) {
        message = QStringLiteral("No files indexed for this project yet.");
    } else if (matches_.isEmpty()) {
        message = noResultsText();
    }
    const bool showEmpty = !message.isEmpty();
    empty_->setText(message);
    empty_->setVisible(showEmpty);
    results_->setVisible(!showEmpty);
}

void CommandPalette::updateStatus()
{
    if (index_ == nullptr) {
        status_->setText(QStringLiteral("No project"));
        return;
    }
    const int total = index_->size();
    if (query_->text().isEmpty()) {
        status_->setText(QStringLiteral("%1 files · type to filter · Enter to open").arg(total));
    } else {
        status_->setText(QStringLiteral("%1 of %2 files").arg(matches_.size()).arg(total));
    }
}

} // namespace vedit::workspace
