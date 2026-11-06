#include "breadcrumb_bar.h"
#include <QCoreApplication>

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QSizePolicy>
#include <QToolButton>

namespace vedit::breadcrumb {

namespace {
const char *const kSegmentObjectName = "breadcrumb-segment";
}

Bar::Bar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("breadcrumb-bar"));
    layout_ = new QHBoxLayout(this);
    // Same margins as the QLabel this bar replaces, so swapping it into the
    // editor pane keeps the visual rhythm of the pane header.
    layout_->setContentsMargins(6, 0, 6, 2);
    layout_->setSpacing(2);
    setVisible(false);
}

Bar::~Bar() = default;

void Bar::setPath(const QString &filePath, const std::vector<Entry> &path)
{
    filePath_ = filePath;
    path_ = path;
    rebuild();
}

void Bar::clear()
{
    filePath_.clear();
    path_.clear();
    rebuild();
}

QString Bar::filePath() const
{
    return filePath_;
}

int Bar::segmentCount() const
{
    return static_cast<int>(segments_.size());
}

QWidget *Bar::segmentWidget(int index) const
{
    if (index < 0 || index >= static_cast<int>(segments_.size())) {
        return nullptr;
    }
    return segments_[static_cast<std::size_t>(index)];
}

QString Bar::segmentText(int index) const
{
    if (index < 0 || index >= static_cast<int>(path_.size())) {
        return QString();
    }
    return segmentLabel(path_[static_cast<std::size_t>(index)]);
}

int Bar::segmentLine(int index) const
{
    if (index < 0 || index >= static_cast<int>(path_.size())) {
        return 0;
    }
    // Entry::line is the syntax source's 0-based line; the jump contract is
    // 1-based, like everything the user sees.
    return path_[static_cast<std::size_t>(index)].line + 1;
}

bool Bar::activateSegment(int index)
{
    if (index < 0 || index >= static_cast<int>(path_.size()) || filePath_.isEmpty()) {
        return false;
    }
    lastSegment_ = index;
    lastStatus_ =
        vedit::workspace::jump::jumpTo(filePath_, segmentLine(index), 1).status;
    return lastStatus_ == vedit::workspace::jump::JumpStatus::Delivered;
}

vedit::workspace::jump::JumpStatus Bar::lastStatus() const
{
    return lastStatus_;
}

int Bar::lastSegment() const
{
    return lastSegment_;
}

QString Bar::segmentLabel(const Entry &entry) const
{
    if (!entry.name.empty()) {
        return QString::fromStdString(entry.name);
    }
    // A container the structure query named nothing for keeps its kind, exactly
    // like Model::display().
    return QString::fromStdString(entry.kind);
}

void Bar::clearSegments()
{
    while (QLayoutItem *item = layout_->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            // Removed from the layout but still a child: hide it so a rebuild
            // cannot paint the previous path under the new one.
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    segments_.clear();
}

void Bar::rebuild()
{
    clearSegments();

    if (path_.empty() || filePath_.isEmpty()) {
        setVisible(false);
        return;
    }

    for (std::size_t i = 0; i < path_.size(); ++i) {
        if (i > 0) {
            auto *separator = new QLabel(QStringLiteral("›"), this);
            separator->setObjectName(QStringLiteral("breadcrumb-separator"));
            separator->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
            layout_->addWidget(separator);
        }

        const int index = static_cast<int>(i);
        auto *button = new QToolButton(this);
        button->setObjectName(QStringLiteral("%1-%2").arg(QLatin1String(kSegmentObjectName))
                                   .arg(index));
        button->setText(segmentLabel(path_[i]));
        button->setToolTip(QCoreApplication::translate("VEdit", "%1: %2 (line %3)")
                               .arg(QString::fromStdString(path_[i].kind),
                                    segmentLabel(path_[i]),
                                    QString::number(path_[i].line + 1)));
        button->setAutoRaise(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
        // Affordance without a theme-dependent stylesheet: the segment looks
        // like a link while hover/press stay the widget style's own.
        QFont font = button->font();
        font.setUnderline(true);
        button->setFont(font);
        connect(button, &QToolButton::clicked, this, [this, index]() { activateSegment(index); });

        layout_->addWidget(button);
        segments_.push_back(button);
    }
    layout_->addStretch(1);
    setVisible(true);
}

} // namespace vedit::breadcrumb
