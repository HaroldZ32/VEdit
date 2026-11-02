#include "vt_screen.h"

#include <algorithm>
#include <utility>

namespace vedit::terminal {

namespace {

constexpr int kTabWidth = 8;

int clampInt(int value, int low, int high)
{
    return std::max(low, std::min(value, high));
}

std::string encodeUtf8(char32_t value)
{
    std::string out;
    if (value < 0x80) {
        out.push_back(static_cast<char>(value));
    } else if (value < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (value >> 6)));
        out.push_back(static_cast<char>(0x80 | (value & 0x3F)));
    } else if (value < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (value >> 12)));
        out.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (value & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (value >> 18)));
        out.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (value & 0x3F)));
    }
    return out;
}

} // namespace

Color Color::indexed(std::uint8_t value)
{
    Color color;
    color.kind = Kind::Indexed;
    color.index = value;
    return color;
}

Color Color::rgb(std::uint8_t red, std::uint8_t green, std::uint8_t blue)
{
    Color color;
    color.kind = Kind::Rgb;
    color.red = red;
    color.green = green;
    color.blue = blue;
    return color;
}

Screen::Screen(int columns, int rows, int scrollbackLines)
    : columns_(std::max(1, columns))
    , rows_(std::max(1, rows))
    , scrollbackLimit_(std::max(0, scrollbackLines))
{
    reset();
}

void Screen::reset()
{
    scrollback_.clear();
    lines_.assign(static_cast<std::size_t>(rows_), blankLine());
    savedMain_.clear();

    cursorRow_ = 0;
    cursorColumn_ = 0;
    cursorVisible_ = true;
    savedCursorRow_ = 0;
    savedCursorColumn_ = 0;
    scrollTop_ = 0;
    scrollBottom_ = rows_ - 1;
    attributes_ = Attributes{};
    savedAttributes_ = Attributes{};
    scrollOffset_ = 0;

    autowrap_ = true;
    applicationCursorKeys_ = false;
    bracketedPaste_ = false;
    insertMode_ = false;
    altScreen_ = false;
    originMode_ = false;

    state_ = ParserState::Ground;
    sequence_.clear();
    osc_.clear();
    title_.clear();
    lastPrinted_ = 0;
    codePoint_ = 0;
    utf8Remaining_ = 0;
}

void Screen::resize(int columns, int rows)
{
    columns = std::max(1, columns);
    rows = std::max(1, rows);
    if (columns == columns_ && rows == rows_) {
        return;
    }

    for (auto &line : lines_) {
        line.resize(static_cast<std::size_t>(columns));
    }
    if (rows > rows_) {
        lines_.insert(lines_.end(), static_cast<std::size_t>(rows - rows_), blankLine());
    } else if (rows < rows_) {
        lines_.erase(lines_.begin() + rows, lines_.end());
    }

    columns_ = columns;
    rows_ = rows;
    scrollTop_ = 0;
    scrollBottom_ = rows_ - 1;
    scrollOffset_ = 0;
    clampCursor();
}

std::vector<Cell> Screen::blankLine() const
{
    return std::vector<Cell>(static_cast<std::size_t>(columns_), blankCell());
}

Cell Screen::blankCell() const
{
    Cell cell;
    cell.codepoint = U' ';
    cell.attributes = attributes_;
    return cell;
}

void Screen::clampCursor()
{
    cursorRow_ = clampInt(cursorRow_, 0, rows_ - 1);
    cursorColumn_ = clampInt(cursorColumn_, 0, columns_ - 1);
}

const Cell &Screen::cellAt(int row, int column) const
{
    static const Cell empty;
    if (row < 0 || row >= rows_ || column < 0 || column >= columns_) {
        return empty;
    }
    return lines_[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)];
}

const std::vector<Cell> &Screen::rowCells(int row) const
{
    static const std::vector<Cell> empty;
    if (row < 0 || row >= rows_) {
        return empty;
    }
    return lines_[static_cast<std::size_t>(row)];
}

int Screen::bufferCellCount(int bufferLine) const
{
    const int history = scrollbackSize();
    if (bufferLine < 0) {
        return 0;
    }
    if (bufferLine < history) {
        return static_cast<int>(scrollback_[static_cast<std::size_t>(bufferLine)].size());
    }
    const int row = bufferLine - history;
    if (row >= rows_) {
        return 0;
    }
    return columns_;
}

Cell Screen::bufferCell(int bufferLine, int column) const
{
    const int history = scrollbackSize();
    if (bufferLine < 0 || column < 0) {
        return Cell{};
    }
    if (bufferLine < history) {
        const auto &line = scrollback_[static_cast<std::size_t>(bufferLine)];
        if (column >= static_cast<int>(line.size())) {
            return Cell{};
        }
        return line[static_cast<std::size_t>(column)];
    }
    const int row = bufferLine - history;
    if (row >= rows_ || column >= columns_) {
        return Cell{};
    }
    return lines_[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)];
}

std::string Screen::lineText(const std::vector<Cell> &cells)
{
    std::string text;
    text.reserve(cells.size());
    for (const Cell &cell : cells) {
        text += encodeUtf8(cell.codepoint);
    }
    while (!text.empty() && text.back() == ' ') {
        text.pop_back();
    }
    return text;
}

std::string Screen::bufferLineText(int bufferLine) const
{
    const int history = scrollbackSize();
    if (bufferLine < 0) {
        return {};
    }
    if (bufferLine < history) {
        return lineText(scrollback_[static_cast<std::size_t>(bufferLine)]);
    }
    const int row = bufferLine - history;
    if (row >= rows_) {
        return {};
    }
    return lineText(lines_[static_cast<std::size_t>(row)]);
}

std::string Screen::textBetween(int fromLine, int fromColumn, int toLine, int toColumn) const
{
    if (fromLine > toLine || (fromLine == toLine && fromColumn > toColumn)) {
        std::swap(fromLine, toLine);
        std::swap(fromColumn, toColumn);
    }
    std::string out;
    for (int line = fromLine; line <= toLine; ++line) {
        const int count = bufferCellCount(line);
        const int start = line == fromLine ? clampInt(fromColumn, 0, count) : 0;
        const int end = line == toLine ? clampInt(toColumn, start, count) : count;
        std::string text;
        for (int column = start; column < end; ++column) {
            text += encodeUtf8(bufferCell(line, column).codepoint);
        }
        while (!text.empty() && text.back() == ' ') {
            text.pop_back();
        }
        if (line != fromLine) {
            out.push_back('\n');
        }
        out += text;
    }
    return out;
}

void Screen::setScrollOffset(int lines)
{
    scrollOffset_ = clampInt(lines, 0, scrollbackSize());
}

void Screen::scrollBy(int lines)
{
    setScrollOffset(scrollOffset_ + lines);
}

void Screen::setResponder(std::function<void(const std::string &)> responder)
{
    responder_ = std::move(responder);
}

void Screen::respond(const std::string &bytes)
{
    if (responder_) {
        responder_(bytes);
    }
}

// --- parser -----------------------------------------------------------------

void Screen::feed(const char *data, std::size_t size)
{
    feedBytes(data, size);
}

void Screen::feed(const std::string &text)
{
    feedBytes(text.data(), text.size());
}

void Screen::feedBytes(const char *data, std::size_t size)
{
    for (std::size_t i = 0; i < size; ++i) {
        const auto byte = static_cast<unsigned char>(data[i]);
        if (utf8Remaining_ == 0) {
            if (byte < 0x80) {
                processChar(static_cast<char32_t>(byte));
            } else if ((byte & 0xE0) == 0xC0) {
                codePoint_ = byte & 0x1F;
                utf8Remaining_ = 1;
            } else if ((byte & 0xF0) == 0xE0) {
                codePoint_ = byte & 0x0F;
                utf8Remaining_ = 2;
            } else if ((byte & 0xF8) == 0xF0) {
                codePoint_ = byte & 0x07;
                utf8Remaining_ = 3;
            } else {
                processChar(0xFFFD);
            }
        } else if ((byte & 0xC0) == 0x80) {
            codePoint_ = (codePoint_ << 6) | (byte & 0x3F);
            if (--utf8Remaining_ == 0) {
                processChar(codePoint_);
            }
        } else {
            utf8Remaining_ = 0;
            processChar(0xFFFD);
        }
    }
}

void Screen::processChar(char32_t value)
{
    switch (state_) {
    case ParserState::Ground:
        if (value == 0x1B) {
            state_ = ParserState::Escape;
            sequence_.clear();
        } else if (value < 0x20 || value == 0x7F) {
            executeControl(value);
        } else {
            printChar(value);
        }
        return;
    case ParserState::Escape:
        escapeChar(value);
        return;
    case ParserState::EscapeIntermediate:
        state_ = ParserState::Ground;
        return;
    case ParserState::Csi:
        if (value == 0x1B) {
            state_ = ParserState::Escape;
            sequence_.clear();
        } else if (value >= 0x40 && value <= 0x7E) {
            dispatchCsi(value);
            state_ = ParserState::Ground;
        } else if (value >= 0x20 && value <= 0x3F) {
            if (sequence_.size() < 256) {
                sequence_.push_back(static_cast<char>(value));
            }
        }
        return;
    case ParserState::Osc:
        if (value == 0x07) {
            finishOsc();
            state_ = ParserState::Ground;
        } else if (value == 0x1B) {
            state_ = ParserState::OscEscape;
        } else if (osc_.size() < 4096) {
            osc_.push_back(static_cast<char>(value));
        }
        return;
    case ParserState::OscEscape:
        if (value == '\\') {
            finishOsc();
        }
        state_ = ParserState::Ground;
        return;
    case ParserState::Dcs:
        if (value == 0x1B) {
            state_ = ParserState::DcsEscape;
        } else if (value == 0x07) {
            state_ = ParserState::Ground;
        }
        return;
    case ParserState::DcsEscape:
        state_ = value == '\\' ? ParserState::Ground : ParserState::Dcs;
        return;
    }
}

void Screen::executeControl(char32_t value)
{
    switch (value) {
    case 0x08:
        backspace();
        return;
    case 0x09:
        tabForward();
        return;
    case 0x0A:
    case 0x0B:
    case 0x0C:
        lineFeed();
        return;
    case 0x0D:
        carriageReturn();
        return;
    default:
        return;
    }
}

void Screen::escapeChar(char32_t value)
{
    state_ = ParserState::Ground;
    switch (value) {
    case '[':
        state_ = ParserState::Csi;
        sequence_.clear();
        return;
    case ']':
        state_ = ParserState::Osc;
        osc_.clear();
        return;
    case 'P':
        state_ = ParserState::Dcs;
        return;
    case '(':
    case ')':
    case '*':
    case '+':
    case '-':
    case '.':
    case '/':
        state_ = ParserState::EscapeIntermediate;
        return;
    case '7':
        saveCursor();
        return;
    case '8':
        restoreCursor();
        return;
    case 'D':
        lineFeed();
        return;
    case 'E':
        cursorColumn_ = 0;
        lineFeed();
        return;
    case 'M':
        reverseIndex();
        return;
    case 'c':
        reset();
        return;
    default:
        return;
    }
}

void Screen::finishOsc()
{
    // OSC 0 / 2 set the window title; everything else (hyperlinks, clipboard,
    // palette changes) is deliberately ignored: they have no bearing on the
    // screen the panel draws.
    if (osc_.size() > 2 && (osc_.compare(0, 2, "0;") == 0 || osc_.compare(0, 2, "2;") == 0)) {
        title_ = osc_.substr(2);
    }
    osc_.clear();
}

void Screen::dispatchCsi(char32_t final)
{
    std::string params = sequence_;
    char prefix = '\0';
    if (!params.empty()) {
        const char head = params.front();
        if (head == '?' || head == '>' || head == '=' || head == '!') {
            prefix = head;
            params.erase(params.begin());
        }
    }

    std::vector<int> values;
    int current = -1;
    bool any = false;
    for (const char character : params) {
        if (character >= '0' && character <= '9') {
            if (current < 0) {
                current = 0;
            }
            current = current * 10 + (character - '0');
            any = true;
        } else if (character == ';' || character == ':') {
            values.push_back(current);
            current = -1;
            any = true;
        }
    }
    if (any) {
        values.push_back(current);
    }

    const auto argument = [&values](std::size_t index, int fallback) {
        if (index >= values.size()) {
            return fallback;
        }
        const int value = values[index];
        return value < 0 ? fallback : value;
    };

    switch (final) {
    case 'A':
        cursorRow_ = std::max(scrollTop_, cursorRow_ - argument(0, 1));
        return;
    case 'B':
    case 'e':
        cursorRow_ = std::min(scrollBottom_, cursorRow_ + argument(0, 1));
        return;
    case 'C':
    case 'a':
        cursorColumn_ = std::min(columns_ - 1, cursorColumn_ + argument(0, 1));
        return;
    case 'D':
        cursorColumn_ = std::max(0, cursorColumn_ - argument(0, 1));
        return;
    case 'E':
        cursorRow_ = std::min(scrollBottom_, cursorRow_ + argument(0, 1));
        cursorColumn_ = 0;
        return;
    case 'F':
        cursorRow_ = std::max(scrollTop_, cursorRow_ - argument(0, 1));
        cursorColumn_ = 0;
        return;
    case 'G':
    case '`':
        cursorColumn_ = clampInt(argument(0, 1) - 1, 0, columns_ - 1);
        return;
    case 'H':
    case 'f': {
        const int base = originMode_ ? scrollTop_ : 0;
        const int row = clampInt(base + argument(0, 1) - 1, base, originMode_ ? scrollBottom_ : rows_ - 1);
        cursorRow_ = row;
        cursorColumn_ = clampInt(argument(1, 1) - 1, 0, columns_ - 1);
        return;
    }
    case 'd':
        cursorRow_ = clampInt(argument(0, 1) - 1 + (originMode_ ? scrollTop_ : 0), 0, rows_ - 1);
        return;
    case 'J': {
        const int mode = argument(0, 0);
        if (mode == 0) {
            clearLineRange(cursorRow_, cursorColumn_, columns_);
            for (int row = cursorRow_ + 1; row < rows_; ++row) {
                clearLineRange(row, 0, columns_);
            }
        } else if (mode == 1) {
            for (int row = 0; row < cursorRow_; ++row) {
                clearLineRange(row, 0, columns_);
            }
            clearLineRange(cursorRow_, 0, cursorColumn_ + 1);
        } else {
            for (int row = 0; row < rows_; ++row) {
                clearLineRange(row, 0, columns_);
            }
            if (mode == 3) {
                scrollback_.clear();
                scrollOffset_ = 0;
            }
        }
        return;
    }
    case 'K': {
        const int mode = argument(0, 0);
        if (mode == 0) {
            clearLineRange(cursorRow_, cursorColumn_, columns_);
        } else if (mode == 1) {
            clearLineRange(cursorRow_, 0, cursorColumn_ + 1);
        } else {
            clearLineRange(cursorRow_, 0, columns_);
        }
        return;
    }
    case 'L':
        insertLines(argument(0, 1));
        return;
    case 'M':
        deleteLines(argument(0, 1));
        return;
    case 'P':
        deleteChars(argument(0, 1));
        return;
    case '@':
        insertChars(argument(0, 1));
        return;
    case 'X':
        eraseChars(argument(0, 1));
        return;
    case 'S':
        scrollUp(argument(0, 1));
        return;
    case 'T':
        scrollDown(argument(0, 1));
        return;
    case 'm':
        applySgr(values.empty() ? std::vector<int>{0} : values);
        return;
    case 'n': {
        const int mode = argument(0, 0);
        if (mode == 6) {
            respond("\x1b[" + std::to_string(cursorRow_ + 1) + ";" + std::to_string(cursorColumn_ + 1) + "R");
        } else if (mode == 5) {
            respond("\x1b[0n");
        }
        return;
    }
    case 'c':
        if (prefix == '>') {
            respond("\x1b[>0;1;0c");
        } else {
            respond("\x1b[?1;2c");
        }
        return;
    case 'h':
        for (const int value : values) {
            setMode(value < 0 ? 0 : value, true, prefix == '?');
        }
        return;
    case 'l':
        for (const int value : values) {
            setMode(value < 0 ? 0 : value, false, prefix == '?');
        }
        return;
    case 'r': {
        const int top = clampInt(argument(0, 1) - 1, 0, rows_ - 1);
        const int bottom = clampInt(argument(1, rows_) - 1, top, rows_ - 1);
        scrollTop_ = top;
        scrollBottom_ = bottom;
        cursorRow_ = originMode_ ? scrollTop_ : 0;
        cursorColumn_ = 0;
        return;
    }
    case 's':
        saveCursor();
        return;
    case 'u':
        restoreCursor();
        return;
    case 'b':
        if (lastPrinted_ != 0) {
            const int count = clampInt(argument(0, 1), 1, columns_ * rows_);
            for (int i = 0; i < count; ++i) {
                printChar(lastPrinted_);
            }
        }
        return;
    default:
        return;
    }
}

void Screen::printChar(char32_t value)
{
    if (value < 0x20) {
        return;
    }
    if (cursorRow_ < 0 || cursorRow_ >= rows_ || lines_.empty()) {
        return;
    }
    if (insertMode_ && columns_ > 0) {
        auto &row = lines_[static_cast<std::size_t>(cursorRow_)];
        row.insert(row.begin() + cursorColumn_, blankCell());
        row.resize(static_cast<std::size_t>(columns_));
    }
    lines_[static_cast<std::size_t>(cursorRow_)][static_cast<std::size_t>(cursorColumn_)] = Cell{value, attributes_};
    lastPrinted_ = value;
    if (cursorColumn_ + 1 < columns_) {
        ++cursorColumn_;
    } else if (autowrap_) {
        cursorColumn_ = 0;
        lineFeed();
    }
}

void Screen::lineFeed()
{
    if (cursorRow_ == scrollBottom_) {
        scrollUp(1);
    } else if (cursorRow_ < rows_ - 1) {
        ++cursorRow_;
    }
}

void Screen::reverseIndex()
{
    if (cursorRow_ == scrollTop_) {
        scrollDown(1);
    } else if (cursorRow_ > 0) {
        --cursorRow_;
    }
}

void Screen::carriageReturn()
{
    cursorColumn_ = 0;
}

void Screen::backspace()
{
    if (cursorColumn_ > 0) {
        --cursorColumn_;
    }
}

void Screen::tabForward()
{
    const int next = ((cursorColumn_ / kTabWidth) + 1) * kTabWidth;
    cursorColumn_ = std::min(next, columns_ - 1);
}

void Screen::scrollUp(int lines)
{
    if (lines <= 0) {
        return;
    }
    const int region = scrollBottom_ - scrollTop_ + 1;
    lines = std::min(lines, region);
    const bool history = !altScreen_ && scrollTop_ == 0 && scrollBottom_ == rows_ - 1;
    for (int i = 0; i < lines; ++i) {
        if (history) {
            scrollback_.push_back(lines_[0]);
            while (static_cast<int>(scrollback_.size()) > scrollbackLimit_) {
                scrollback_.pop_front();
            }
        }
        lines_.erase(lines_.begin() + scrollTop_);
        lines_.insert(lines_.begin() + scrollBottom_, blankLine());
    }
}

void Screen::scrollDown(int lines)
{
    if (lines <= 0) {
        return;
    }
    const int region = scrollBottom_ - scrollTop_ + 1;
    lines = std::min(lines, region);
    for (int i = 0; i < lines; ++i) {
        lines_.erase(lines_.begin() + scrollBottom_);
        lines_.insert(lines_.begin() + scrollTop_, blankLine());
    }
}

void Screen::insertLines(int count)
{
    if (cursorRow_ < scrollTop_ || cursorRow_ > scrollBottom_) {
        return;
    }
    count = clampInt(count, 1, scrollBottom_ - cursorRow_ + 1);
    for (int i = 0; i < count; ++i) {
        lines_.erase(lines_.begin() + scrollBottom_);
        lines_.insert(lines_.begin() + cursorRow_, blankLine());
    }
    cursorColumn_ = 0;
}

void Screen::deleteLines(int count)
{
    if (cursorRow_ < scrollTop_ || cursorRow_ > scrollBottom_) {
        return;
    }
    count = clampInt(count, 1, scrollBottom_ - cursorRow_ + 1);
    for (int i = 0; i < count; ++i) {
        lines_.erase(lines_.begin() + cursorRow_);
        lines_.insert(lines_.begin() + scrollBottom_, blankLine());
    }
    cursorColumn_ = 0;
}

void Screen::insertChars(int count)
{
    if (cursorColumn_ >= columns_) {
        return;
    }
    count = clampInt(count, 1, columns_ - cursorColumn_);
    auto &row = lines_[static_cast<std::size_t>(cursorRow_)];
    row.insert(row.begin() + cursorColumn_, static_cast<std::size_t>(count), blankCell());
    row.resize(static_cast<std::size_t>(columns_));
}

void Screen::deleteChars(int count)
{
    if (cursorColumn_ >= columns_) {
        return;
    }
    count = clampInt(count, 1, columns_ - cursorColumn_);
    auto &row = lines_[static_cast<std::size_t>(cursorRow_)];
    row.erase(row.begin() + cursorColumn_, row.begin() + cursorColumn_ + count);
    row.insert(row.end(), static_cast<std::size_t>(count), blankCell());
}

void Screen::eraseChars(int count)
{
    if (cursorColumn_ >= columns_) {
        return;
    }
    count = clampInt(count, 1, columns_ - cursorColumn_);
    clearLineRange(cursorRow_, cursorColumn_, cursorColumn_ + count);
}

void Screen::clearLineRange(int row, int from, int to)
{
    if (row < 0 || row >= rows_) {
        return;
    }
    auto &line = lines_[static_cast<std::size_t>(row)];
    from = clampInt(from, 0, columns_);
    to = clampInt(to, from, columns_);
    for (int column = from; column < to; ++column) {
        line[static_cast<std::size_t>(column)] = blankCell();
    }
}

void Screen::applySgr(const std::vector<int> &values)
{
    for (std::size_t index = 0; index < values.size(); ++index) {
        const int value = values[index] < 0 ? 0 : values[index];
        switch (value) {
        case 0:
            attributes_ = Attributes{};
            break;
        case 1:
            attributes_.bold = true;
            break;
        case 2:
            attributes_.faint = true;
            break;
        case 3:
            attributes_.italic = true;
            break;
        case 4:
            attributes_.underline = true;
            break;
        case 7:
            attributes_.inverse = true;
            break;
        case 9:
            attributes_.strike = true;
            break;
        case 21:
        case 22:
            attributes_.bold = false;
            attributes_.faint = false;
            break;
        case 23:
            attributes_.italic = false;
            break;
        case 24:
            attributes_.underline = false;
            break;
        case 27:
            attributes_.inverse = false;
            break;
        case 29:
            attributes_.strike = false;
            break;
        case 39:
            attributes_.foreground = Color{};
            break;
        case 49:
            attributes_.background = Color{};
            break;
        case 38:
        case 48: {
            Color color;
            if (index + 1 < values.size() && values[index + 1] == 5 && index + 2 < values.size()) {
                color = Color::indexed(static_cast<std::uint8_t>(clampInt(values[index + 2], 0, 255)));
                index += 2;
            } else if (index + 1 < values.size() && values[index + 1] == 2 && index + 4 < values.size()) {
                color = Color::rgb(static_cast<std::uint8_t>(clampInt(values[index + 2], 0, 255)),
                                   static_cast<std::uint8_t>(clampInt(values[index + 3], 0, 255)),
                                   static_cast<std::uint8_t>(clampInt(values[index + 4], 0, 255)));
                index += 4;
            }
            if (value == 38) {
                attributes_.foreground = color;
            } else {
                attributes_.background = color;
            }
            break;
        }
        default:
            if (value >= 30 && value <= 37) {
                attributes_.foreground = Color::indexed(static_cast<std::uint8_t>(value - 30));
            } else if (value >= 40 && value <= 47) {
                attributes_.background = Color::indexed(static_cast<std::uint8_t>(value - 40));
            } else if (value >= 90 && value <= 97) {
                attributes_.foreground = Color::indexed(static_cast<std::uint8_t>(value - 90 + 8));
            } else if (value >= 100 && value <= 107) {
                attributes_.background = Color::indexed(static_cast<std::uint8_t>(value - 100 + 8));
            }
            break;
        }
    }
}

void Screen::setMode(int mode, bool enabled, bool privateMode)
{
    if (privateMode) {
        switch (mode) {
        case 1:
            applicationCursorKeys_ = enabled;
            return;
        case 6:
            originMode_ = enabled;
            return;
        case 7:
            autowrap_ = enabled;
            return;
        case 25:
            cursorVisible_ = enabled;
            return;
        case 47:
        case 1047:
        case 1049:
            if (enabled && !altScreen_) {
                altScreen_ = true;
                savedMain_ = lines_;
                savedCursorRow_ = cursorRow_;
                savedCursorColumn_ = cursorColumn_;
                lines_.assign(static_cast<std::size_t>(rows_), blankLine());
                cursorRow_ = 0;
                cursorColumn_ = 0;
                scrollOffset_ = 0;
            } else if (!enabled && altScreen_) {
                altScreen_ = false;
                lines_ = savedMain_;
                savedMain_.clear();
                cursorRow_ = savedCursorRow_;
                cursorColumn_ = savedCursorColumn_;
                clampCursor();
            }
            return;
        case 2004:
            bracketedPaste_ = enabled;
            return;
        default:
            return;
        }
    }
    if (mode == 4) {
        insertMode_ = enabled;
    }
}

void Screen::saveCursor()
{
    savedCursorRow_ = cursorRow_;
    savedCursorColumn_ = cursorColumn_;
    savedAttributes_ = attributes_;
}

void Screen::restoreCursor()
{
    cursorRow_ = savedCursorRow_;
    cursorColumn_ = savedCursorColumn_;
    attributes_ = savedAttributes_;
    clampCursor();
}

} // namespace vedit::terminal
