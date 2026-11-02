#pragma once

// VT100/xterm screen model for the terminal panel (task T35).
//
// This is the "equivalent" of QTermWidget the contract allows: the pinned build
// image ships neither qtermwidget nor libvterm, and QTermWidget is GPL-2.0+ while
// VEdit itself is MIT, so the panel carries its own small emulator instead of
// linking one (see src/terminal/README.md and evidence/term-panel/00-pins.txt).
//
// The class is deliberately Qt-free: the parser, the cell grid, the scrollback
// ring and the cursor are plain C++, so the emulator is unit-testable headless
// and the Qt widget only paints what is in here.
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace vedit::terminal {

// A colour as the emulator sees it: the terminal's default, one of the 256
// palette entries, or a direct RGB value (SGR 38;2;r;g;b).
struct Color {
    enum class Kind : std::uint8_t { Default, Indexed, Rgb };

    Kind kind = Kind::Default;
    std::uint8_t index = 0;
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;

    static Color indexed(std::uint8_t value);
    static Color rgb(std::uint8_t red, std::uint8_t green, std::uint8_t blue);

    bool operator==(const Color &) const = default;
};

struct Attributes {
    Color foreground;
    Color background;
    bool bold = false;
    bool faint = false;
    bool italic = false;
    bool underline = false;
    bool inverse = false;
    bool strike = false;

    bool operator==(const Attributes &) const = default;
};

struct Cell {
    char32_t codepoint = U' ';
    Attributes attributes;

    bool operator==(const Cell &) const = default;
};

class Screen {
public:
    explicit Screen(int columns = 80, int rows = 24, int scrollbackLines = 2000);

    void reset();
    void resize(int columns, int rows);

    int columns() const { return columns_; }
    int rows() const { return rows_; }
    int scrollbackLimit() const { return scrollbackLimit_; }
    int scrollbackSize() const { return static_cast<int>(scrollback_.size()); }
    int bufferLines() const { return scrollbackSize() + rows_; }

    // Active screen, viewport coordinates (row 0 is the top of the viewport).
    const Cell &cellAt(int row, int column) const;
    const std::vector<Cell> &rowCells(int row) const;

    // The whole scroll buffer: [0, scrollbackSize()) is history, the rest are
    // the screen rows. Columns outside a line read as blank cells.
    int bufferCellCount(int bufferLine) const;
    Cell bufferCell(int bufferLine, int column) const;
    std::string bufferLineText(int bufferLine) const;

    // 0 is the live view; a positive offset scrolls the viewport up into history.
    int scrollOffset() const { return scrollOffset_; }
    void setScrollOffset(int lines);
    void scrollBy(int lines);
    bool isScrolledBack() const { return scrollOffset_ > 0; }
    int maximumScrollOffset() const { return scrollbackSize(); }

    int cursorRow() const { return cursorRow_; }
    int cursorColumn() const { return cursorColumn_; }
    bool cursorVisible() const { return cursorVisible_; }
    bool applicationCursorKeys() const { return applicationCursorKeys_; }
    bool bracketedPaste() const { return bracketedPaste_; }
    bool altScreen() const { return altScreen_; }
    const std::string &title() const { return title_; }
    bool autowrap() const { return autowrap_; }

    // Parser input (bytes straight off the pty; UTF-8 is decoded here).
    void feed(const char *data, std::size_t size);
    void feed(const std::string &text);

    // Replies the terminal owes the application (cursor position report, device
    // attributes). The owner writes them back to the pty.
    void setResponder(std::function<void(const std::string &)> responder);

    // Text between two buffer positions, for the widget's copy action.
    std::string textBetween(int fromLine, int fromColumn, int toLine, int toColumn) const;

    // Trims the trailing blanks of one stored line (test/diagnostic helper).
    static std::string lineText(const std::vector<Cell> &cells);

private:
    enum class ParserState { Ground, Escape, EscapeIntermediate, Csi, Osc, OscEscape, Dcs, DcsEscape };

    std::vector<Cell> blankLine() const;
    Cell blankCell() const;

    void feedBytes(const char *data, std::size_t size);
    void processChar(char32_t value);
    void executeControl(char32_t value);
    void escapeChar(char32_t value);
    void dispatchCsi(char32_t final);
    void finishOsc();
    void printChar(char32_t value);
    void lineFeed();
    void reverseIndex();
    void carriageReturn();
    void backspace();
    void tabForward();

    void scrollUp(int lines);
    void scrollDown(int lines);
    void insertLines(int count);
    void deleteLines(int count);
    void insertChars(int count);
    void deleteChars(int count);
    void eraseChars(int count);

    void applySgr(const std::vector<int> &values);
    void setMode(int mode, bool enabled, bool privateMode);
    void saveCursor();
    void restoreCursor();
    void respond(const std::string &bytes);

    void clearLineRange(int row, int from, int to);
    void clampCursor();

    int columns_ = 80;
    int rows_ = 24;
    int scrollbackLimit_ = 2000;

    std::vector<std::vector<Cell>> lines_;
    std::deque<std::vector<Cell>> scrollback_;
    std::vector<std::vector<Cell>> savedMain_;

    int cursorRow_ = 0;
    int cursorColumn_ = 0;
    bool cursorVisible_ = true;
    int savedCursorRow_ = 0;
    int savedCursorColumn_ = 0;
    int scrollTop_ = 0;
    int scrollBottom_ = 23;

    Attributes attributes_{};
    Attributes savedAttributes_{};
    int scrollOffset_ = 0;

    bool autowrap_ = true;
    bool applicationCursorKeys_ = false;
    bool bracketedPaste_ = false;
    bool insertMode_ = false;
    bool altScreen_ = false;
    bool originMode_ = false;

    ParserState state_ = ParserState::Ground;
    std::string sequence_;
    std::string osc_;
    std::string title_;
    std::function<void(const std::string &)> responder_;

    char32_t lastPrinted_ = 0;
    char32_t codePoint_ = 0;
    int utf8Remaining_ = 0;
};

} // namespace vedit::terminal
