#pragma once

// The terminal *view*: paints a vt_screen::Screen and turns keyboard, wheel and
// mouse events into bytes for the pty. It owns no process -- TerminalPanel owns
// the PtySession and attaches it here (task T35).
#include "vt_screen.h"

#include <QColor>
#include <QFont>
#include <QPoint>
#include <QSize>
#include <QString>
#include <QWidget>

class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QShowEvent;
class QWheelEvent;

namespace vedit::terminal {

class PtySession;

class TerminalWidget final : public QWidget {
    Q_OBJECT

public:
    explicit TerminalWidget(QWidget *parent = nullptr);
    ~TerminalWidget() override;

    void attach(PtySession *session);
    PtySession *session() const { return session_; }

    Screen &screen() { return screen_; }
    const Screen &screen() const { return screen_; }

    void setScrollOffset(int lines);
    int scrollOffset() const;
    int maximumScrollOffset() const;
    void scrollToBottom();

    QString selectedText() const;
    bool hasSelection() const;
    void clearSelection();

    QColor backgroundColor() const { return paletteBackground_; }
    QColor foregroundColor() const { return paletteForeground_; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    // One character cell in device-independent pixels (font metrics).
    QSize cellSize() const;

signals:
    void scrollRangeChanged();
    void selectionChanged(bool available);
    // A key arrived while no shell is running (the panel restarts on it).
    void idleActivated();

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    struct Position {
        int line = 0;
        int column = 0;
    };

    void recomputeMetrics();
    void applyGeometryToScreen();
    void send(const QByteArray &bytes);
    void sendText(const QString &text);

    int visibleRows() const;
    Position positionAt(const QPoint &point) const;
    bool isSelected(int line, int column) const;
    void normalizeSelection(int &fromLine, int &fromColumn, int &toLine, int &toColumn) const;

    QColor colorFor(const Color &color, const QColor &fallback) const;
    QColor effectiveForeground(const Attributes &attributes) const;
    QColor effectiveBackground(const Attributes &attributes) const;

    void selectWordAt(const Position &position);

    Screen screen_;
    PtySession *session_ = nullptr;

    QFont font_;
    qreal cellWidth_ = 8.0;
    qreal cellHeight_ = 16.0;
    qreal ascent_ = 12.0;
    qreal padding_ = 4.0;

    QColor paletteBackground_{0x1E, 0x1E, 0x1E};
    QColor paletteForeground_{0xD4, 0xD4, 0xD4};

    bool selecting_ = false;
    bool hasSelection_ = false;
    Position anchor_;
    Position cursor_;
};

} // namespace vedit::terminal
