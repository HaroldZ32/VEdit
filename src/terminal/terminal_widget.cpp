#include "terminal_widget.h"

#include "pty_session.h"

#include <QApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cstdint>
#include <string>

namespace vedit::terminal {

namespace {

constexpr int kScrollLinesPerWheelStep = 3;
constexpr int kMinimumColumns = 20;
constexpr int kMinimumRows = 4;
constexpr qreal kPadding = 4.0;

const QColor kSelectionBackground(0x26, 0x4F, 0x78);
const QColor kSelectionForeground(0xF0, 0xF0, 0xF0);

QColor paletteColor(std::uint8_t index)
{
    static const QRgb base[16] = {
        0x000000, 0xCD0000, 0x00CD00, 0xCDCD00, 0x0000EE, 0xCD00CD, 0x00CDCD, 0xE5E5E5,
        0x7F7F7F, 0xFF0000, 0x00FF00, 0xFFFF00, 0x5C5CFF, 0xFF00FF, 0x00FFFF, 0xFFFFFF,
    };
    if (index < 16) {
        return QColor::fromRgb(base[index]);
    }
    if (index < 232) {
        static const int steps[6] = {0, 95, 135, 175, 215, 255};
        const int entry = static_cast<int>(index) - 16;
        return QColor(steps[entry / 36], steps[(entry / 6) % 6], steps[entry % 6]);
    }
    const int level = 8 + (static_cast<int>(index) - 232) * 10;
    return QColor(level, level, level);
}

QByteArray functionKeySequence(int key)
{
    switch (key) {
    case Qt::Key_F1:
        return "\x1bOP";
    case Qt::Key_F2:
        return "\x1bOQ";
    case Qt::Key_F3:
        return "\x1bOR";
    case Qt::Key_F4:
        return "\x1bOS";
    case Qt::Key_F5:
        return "\x1b[15~";
    case Qt::Key_F6:
        return "\x1b[17~";
    case Qt::Key_F7:
        return "\x1b[18~";
    case Qt::Key_F8:
        return "\x1b[19~";
    case Qt::Key_F9:
        return "\x1b[20~";
    case Qt::Key_F10:
        return "\x1b[21~";
    case Qt::Key_F11:
        return "\x1b[23~";
    case Qt::Key_F12:
        return "\x1b[24~";
    default:
        return {};
    }
}

} // namespace

TerminalWidget::TerminalWidget(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("terminal-widget"));
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    font_ = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (font_.family().isEmpty()) {
        font_.setFamily(QStringLiteral("DejaVu Sans Mono"));
    }
    font_.setStyleHint(QFont::Monospace);
    font_.setPointSizeF(10.0);
    recomputeMetrics();

    screen_.setResponder([this](const std::string &bytes) {
        send(QByteArray(bytes.data(), static_cast<int>(bytes.size())));
    });
}

TerminalWidget::~TerminalWidget() = default;

void TerminalWidget::recomputeMetrics()
{
    const QFontMetricsF metrics(font_);
    cellWidth_ = std::max(1.0, metrics.horizontalAdvance(QStringLiteral("M")));
    cellHeight_ = std::max(1.0, metrics.lineSpacing());
    ascent_ = metrics.ascent();
    padding_ = kPadding;
}

void TerminalWidget::attach(PtySession *session)
{
    if (session_ != nullptr) {
        disconnect(session_, nullptr, this, nullptr);
    }
    session_ = session;
    if (session_ != nullptr) {
        connect(session_, &PtySession::outputReady, this, [this](const QByteArray &data) {
            screen_.feed(data.constData(), static_cast<std::size_t>(data.size()));
            emit scrollRangeChanged();
            update();
        });
        connect(session_, &PtySession::finished, this, [this](int, int, const QString &) {
            emit scrollRangeChanged();
            update();
        });
    }
    applyGeometryToScreen();
    update();
}

QSize TerminalWidget::sizeHint() const
{
    return QSize(static_cast<int>(cellWidth_ * 80.0 + 2.0 * padding_),
                 static_cast<int>(cellHeight_ * 24.0 + 2.0 * padding_));
}

QSize TerminalWidget::minimumSizeHint() const
{
    return QSize(static_cast<int>(cellWidth_ * 20.0 + 2.0 * padding_),
                 static_cast<int>(cellHeight_ * 4.0 + 2.0 * padding_));
}

QSize TerminalWidget::cellSize() const
{
    return QSize(static_cast<int>(cellWidth_), static_cast<int>(cellHeight_));
}

int TerminalWidget::visibleRows() const
{
    const auto rows = static_cast<int>((height() - 2.0 * padding_) / cellHeight_);
    return std::max(1, rows);
}

void TerminalWidget::applyGeometryToScreen()
{
    // A panel that is not on screen (never shown, hidden dock) keeps its pty at
    // the size it had: shrinking it to the layout's minimum would wrap the shell
    // output into a useless 4-column screen.
    if (!isVisible()) {
        return;
    }
    const auto columns = std::max(kMinimumColumns, static_cast<int>((width() - 2.0 * padding_) / cellWidth_));
    const int rows = std::max(kMinimumRows, visibleRows());
    if (columns == screen_.columns() && rows == screen_.rows()) {
        return;
    }
    screen_.resize(columns, rows);
    if (session_ != nullptr && session_->isRunning()) {
        session_->resize(columns, rows);
    }
    emit scrollRangeChanged();
}

void TerminalWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    applyGeometryToScreen();
    update();
}

void TerminalWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    applyGeometryToScreen();
}

void TerminalWidget::setScrollOffset(int lines)
{
    screen_.setScrollOffset(lines);
    emit scrollRangeChanged();
    update();
}

int TerminalWidget::scrollOffset() const
{
    return screen_.scrollOffset();
}

int TerminalWidget::maximumScrollOffset() const
{
    return screen_.maximumScrollOffset();
}

void TerminalWidget::scrollToBottom()
{
    if (screen_.scrollOffset() != 0) {
        setScrollOffset(0);
    }
}

bool TerminalWidget::hasSelection() const
{
    return hasSelection_ && (anchor_.line != cursor_.line || anchor_.column != cursor_.column);
}

void TerminalWidget::clearSelection()
{
    if (!hasSelection_ && !selecting_) {
        return;
    }
    hasSelection_ = false;
    selecting_ = false;
    emit selectionChanged(false);
    update();
}

void TerminalWidget::normalizeSelection(int &fromLine, int &fromColumn, int &toLine, int &toColumn) const
{
    fromLine = anchor_.line;
    fromColumn = anchor_.column;
    toLine = cursor_.line;
    toColumn = cursor_.column;
    if (toLine < fromLine || (toLine == fromLine && toColumn < fromColumn)) {
        std::swap(fromLine, toLine);
        std::swap(fromColumn, toColumn);
    }
}

bool TerminalWidget::isSelected(int line, int column) const
{
    if (!hasSelection()) {
        return false;
    }
    int fromLine = 0;
    int fromColumn = 0;
    int toLine = 0;
    int toColumn = 0;
    normalizeSelection(fromLine, fromColumn, toLine, toColumn);
    if (line < fromLine || line > toLine) {
        return false;
    }
    if (line == fromLine && column < fromColumn) {
        return false;
    }
    if (line == toLine && column >= toColumn) {
        return false;
    }
    return true;
}

QString TerminalWidget::selectedText() const
{
    if (!hasSelection()) {
        return {};
    }
    int fromLine = 0;
    int fromColumn = 0;
    int toLine = 0;
    int toColumn = 0;
    normalizeSelection(fromLine, fromColumn, toLine, toColumn);
    return QString::fromStdString(screen_.textBetween(fromLine, fromColumn, toLine, toColumn));
}

QColor TerminalWidget::colorFor(const Color &color, const QColor &fallback) const
{
    switch (color.kind) {
    case Color::Kind::Default:
        return fallback;
    case Color::Kind::Indexed:
        return paletteColor(color.index);
    case Color::Kind::Rgb:
        return QColor(color.red, color.green, color.blue);
    }
    return fallback;
}

QColor TerminalWidget::effectiveForeground(const Attributes &attributes) const
{
    const QColor base = colorFor(attributes.foreground, paletteForeground_);
    if (attributes.faint) {
        QColor faint = base;
        faint.setAlphaF(0.6);
        return faint;
    }
    return attributes.inverse ? colorFor(attributes.background, paletteBackground_) : base;
}

QColor TerminalWidget::effectiveBackground(const Attributes &attributes) const
{
    return attributes.inverse ? colorFor(attributes.foreground, paletteForeground_)
                              : colorFor(attributes.background, paletteBackground_);
}

void TerminalWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), paletteBackground_);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const int rows = std::min(visibleRows(), screen_.rows());
    const int columns = screen_.columns();
    const int firstBufferLine = screen_.scrollbackSize() - screen_.scrollOffset();

    for (int row = 0; row < rows; ++row) {
        const int bufferLine = firstBufferLine + row;
        const qreal y = padding_ + static_cast<qreal>(row) * cellHeight_;

        int column = 0;
        while (column < columns) {
            const bool selected = isSelected(bufferLine, column);
            QColor background = selected ? kSelectionBackground
                                         : effectiveBackground(screen_.bufferCell(bufferLine, column).attributes);
            int end = column + 1;
            while (end < columns) {
                const bool nextSelected = isSelected(bufferLine, end);
                const QColor next = nextSelected
                                        ? kSelectionBackground
                                        : effectiveBackground(screen_.bufferCell(bufferLine, end).attributes);
                if (next != background) {
                    break;
                }
                ++end;
            }
            painter.fillRect(QRectF(padding_ + static_cast<qreal>(column) * cellWidth_, y,
                                    static_cast<qreal>(end - column) * cellWidth_, cellHeight_),
                             background);
            column = end;
        }

        column = 0;
        while (column < columns) {
            const Cell first = screen_.bufferCell(bufferLine, column);
            const bool firstSelected = isSelected(bufferLine, column);
            const QColor runColor = firstSelected ? kSelectionForeground : effectiveForeground(first.attributes);
            const Attributes runAttributes = first.attributes;

            std::u32string run;
            const int runStart = column;
            while (column < columns) {
                const Cell cell = screen_.bufferCell(bufferLine, column);
                const bool selected = isSelected(bufferLine, column);
                const QColor color = selected ? kSelectionForeground : effectiveForeground(cell.attributes);
                if (color != runColor || cell.attributes.bold != runAttributes.bold
                    || cell.attributes.italic != runAttributes.italic
                    || cell.attributes.underline != runAttributes.underline
                    || cell.attributes.strike != runAttributes.strike) {
                    break;
                }
                run.push_back(cell.codepoint);
                ++column;
            }
            if (run.empty()) {
                continue;
            }

            QFont runFont = font_;
            runFont.setBold(runAttributes.bold);
            runFont.setItalic(runAttributes.italic);
            runFont.setUnderline(runAttributes.underline);
            runFont.setStrikeOut(runAttributes.strike);
            painter.setFont(runFont);
            painter.setPen(runColor);
            painter.drawText(QPointF(padding_ + static_cast<qreal>(runStart) * cellWidth_, y + ascent_),
                             QString::fromUcs4(run.data(), static_cast<qsizetype>(run.size())));
        }
    }

    if (screen_.cursorVisible() && screen_.scrollOffset() == 0 && screen_.cursorRow() < rows) {
        const QRectF caret(padding_ + static_cast<qreal>(screen_.cursorColumn()) * cellWidth_,
                           padding_ + static_cast<qreal>(screen_.cursorRow()) * cellHeight_, cellWidth_,
                           cellHeight_);
        const Cell cell = screen_.cellAt(screen_.cursorRow(), screen_.cursorColumn());
        painter.fillRect(caret, paletteForeground_);
        if (cell.codepoint != U' ') {
            painter.setFont(font_);
            painter.setPen(paletteBackground_);
            painter.drawText(QPointF(caret.left(), caret.top() + ascent_),
                             QString::fromUcs4(&cell.codepoint, 1));
        }
    }
}

TerminalWidget::Position TerminalWidget::positionAt(const QPoint &point) const
{
    Position position;
    const int columns = std::max(1, screen_.columns());
    int column = static_cast<int>((static_cast<qreal>(point.x()) - padding_) / cellWidth_);
    int row = static_cast<int>((static_cast<qreal>(point.y()) - padding_) / cellHeight_);
    column = std::max(0, std::min(column, columns - 1));
    row = std::max(0, std::min(row, visibleRows() - 1));
    position.column = column;
    position.line = screen_.scrollbackSize() - screen_.scrollOffset() + row;
    return position;
}

void TerminalWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    setFocus(Qt::MouseFocusReason);
    clearSelection();
    selecting_ = true;
    anchor_ = positionAt(event->position().toPoint());
    cursor_ = anchor_;
    hasSelection_ = false;
    event->accept();
}

void TerminalWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!selecting_) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    cursor_ = positionAt(event->position().toPoint());
    hasSelection_ = anchor_.line != cursor_.line || anchor_.column != cursor_.column;
    emit selectionChanged(hasSelection_);
    update();
    event->accept();
}

void TerminalWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !selecting_) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    selecting_ = false;
    cursor_ = positionAt(event->position().toPoint());
    hasSelection_ = anchor_.line != cursor_.line || anchor_.column != cursor_.column;
    emit selectionChanged(hasSelection_);
    update();
    event->accept();
}

void TerminalWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }
    selectWordAt(positionAt(event->position().toPoint()));
    event->accept();
}

void TerminalWidget::selectWordAt(const Position &position)
{
    const QString line = QString::fromStdString(screen_.bufferLineText(position.line));
    const auto isWordCharacter = [](QChar character) {
        return character.isLetterOrNumber() || character == QLatin1Char('_') || character == QLatin1Char('-')
               || character == QLatin1Char('.') || character == QLatin1Char('/') || character == QLatin1Char(':')
               || character == QLatin1Char('~');
    };

    int start = std::min(position.column, static_cast<int>(line.size()));
    int end = start;
    while (start > 0 && isWordCharacter(line.at(start - 1))) {
        --start;
    }
    while (end < line.size() && isWordCharacter(line.at(end))) {
        ++end;
    }
    if (start == end) {
        return;
    }
    anchor_ = Position{position.line, start};
    cursor_ = Position{position.line, end};
    selecting_ = false;
    hasSelection_ = true;
    emit selectionChanged(true);
    update();
}

void TerminalWidget::wheelEvent(QWheelEvent *event)
{
    const int delta = event->angleDelta().y();
    if (delta == 0) {
        event->ignore();
        return;
    }
    const int steps = delta > 0 ? kScrollLinesPerWheelStep : -kScrollLinesPerWheelStep;
    setScrollOffset(scrollOffset() + steps);
    event->accept();
}

void TerminalWidget::send(const QByteArray &bytes)
{
    if (session_ != nullptr && session_->isRunning() && !bytes.isEmpty()) {
        session_->write(bytes);
    }
}

void TerminalWidget::sendText(const QString &text)
{
    if (text.isEmpty()) {
        return;
    }
    if (screen_.bracketedPaste()) {
        QByteArray payload = "\x1b[200~";
        payload += text.toUtf8();
        payload += "\x1b[201~";
        send(payload);
        return;
    }
    send(text.toUtf8());
}

void TerminalWidget::keyPressEvent(QKeyEvent *event)
{
    const int key = event->key();
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    const bool control = modifiers.testFlag(Qt::ControlModifier);
    const bool shift = modifiers.testFlag(Qt::ShiftModifier);
    const bool alt = modifiers.testFlag(Qt::AltModifier);

    // The terminal's own clipboard shortcuts: plain Ctrl+C/Ctrl+V must reach the
    // shell as SIGINT / literal-next, so copy and paste are on Ctrl+Shift.
    if (control && shift && key == Qt::Key_C) {
        const QString text = selectedText();
        if (!text.isEmpty()) {
            QApplication::clipboard()->setText(text);
        }
        event->accept();
        return;
    }
    if ((control && shift && key == Qt::Key_V) || (shift && key == Qt::Key_Insert)) {
        sendText(QApplication::clipboard()->text());
        scrollToBottom();
        event->accept();
        return;
    }

    if (session_ == nullptr || !session_->isRunning()) {
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            emit idleActivated();
            event->accept();
            return;
        }
        event->ignore();
        return;
    }

    QByteArray bytes;
    const QByteArray cursorPrefix = screen_.applicationCursorKeys() ? "\x1bO" : "\x1b[";
    switch (key) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        bytes = "\r";
        break;
    case Qt::Key_Backspace:
        bytes = "\x7f";
        break;
    case Qt::Key_Tab:
        bytes = shift ? "\x1b[Z" : "\t";
        break;
    case Qt::Key_Escape:
        bytes = "\x1b";
        break;
    case Qt::Key_Left:
        bytes = cursorPrefix + "D";
        break;
    case Qt::Key_Right:
        bytes = cursorPrefix + "C";
        break;
    case Qt::Key_Up:
        bytes = cursorPrefix + "A";
        break;
    case Qt::Key_Down:
        bytes = cursorPrefix + "B";
        break;
    case Qt::Key_Home:
        bytes = cursorPrefix + "H";
        break;
    case Qt::Key_End:
        bytes = cursorPrefix + "F";
        break;
    case Qt::Key_Insert:
        bytes = "\x1b[2~";
        break;
    case Qt::Key_Delete:
        bytes = "\x1b[3~";
        break;
    case Qt::Key_PageUp:
        if (shift) {
            setScrollOffset(scrollOffset() + 10);
            event->accept();
            return;
        }
        bytes = "\x1b[5~";
        break;
    case Qt::Key_PageDown:
        if (shift) {
            setScrollOffset(scrollOffset() - 10);
            event->accept();
            return;
        }
        bytes = "\x1b[6~";
        break;
    case Qt::Key_F1:
    case Qt::Key_F2:
    case Qt::Key_F3:
    case Qt::Key_F4:
    case Qt::Key_F5:
    case Qt::Key_F6:
    case Qt::Key_F7:
    case Qt::Key_F8:
    case Qt::Key_F9:
    case Qt::Key_F10:
    case Qt::Key_F11:
    case Qt::Key_F12:
        bytes = functionKeySequence(key);
        break;
    default: {
        if (control && key >= Qt::Key_A && key <= Qt::Key_Z) {
            bytes.append(static_cast<char>(key - Qt::Key_A + 1));
        } else if (control && key == Qt::Key_BracketLeft) {
            bytes = "\x1b";
        } else if (control && key == Qt::Key_Backslash) {
            bytes = "\x1c";
        } else if (control && key == Qt::Key_BracketRight) {
            bytes = "\x1d";
        } else if (control && key == Qt::Key_Space) {
            bytes = QByteArray(1, '\0');
        } else if (alt && !event->text().isEmpty()) {
            bytes = QByteArray("\x1b") + event->text().toUtf8();
        } else {
            bytes = event->text().toUtf8();
        }
        break;
    }
    }

    if (bytes.isEmpty()) {
        event->ignore();
        return;
    }
    send(bytes);
    scrollToBottom();
    event->accept();
}

} // namespace vedit::terminal
