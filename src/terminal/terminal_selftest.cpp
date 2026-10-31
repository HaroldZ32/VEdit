// Self-check for the terminal panel (task T35).
//
//   vedit-terminal-selftest --selfcheck              headless checks (offscreen)
//   vedit-terminal-selftest --screenshots <dir>      capture the two panel states
//
// The checks are the acceptance evidence of the contract: a local shell session
// really runs (interactive input, pty size, exit code and signal visible), the VT
// emulator parses the sequences a shell and full-screen tools use, and the panel
// is dockable, closable and rebuildable. Every check prints one machine-readable
// line, like the app shell's self-check.
#include "pty_session.h"
#include "terminal_panel.h"
#include "terminal_widget.h"
#include "vt_screen.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QMainWindow>
#include <QPalette>
#include <QStringList>
#include <QThread>
#include <QToolButton>

#include <cstdio>
#include <functional>
#include <memory>

using vedit::terminal::PtySession;
using vedit::terminal::Screen;
using vedit::terminal::TerminalOptions;
using vedit::terminal::TerminalPanel;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const char *name, bool ok)
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s\n", name, ok ? "PASS" : "FAIL");
    std::fflush(stdout);
}

bool waitFor(const std::function<bool()> &predicate, int timeoutMs = 8000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents();
        if (predicate()) {
            return true;
        }
        QThread::msleep(5);
    }
    QCoreApplication::processEvents();
    return predicate();
}

void pump(int milliseconds = 60)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < milliseconds) {
        QCoreApplication::processEvents();
        QThread::msleep(2);
    }
}

std::string lastLine(const Screen &screen)
{
    return screen.bufferLineText(screen.bufferLines() - 1);
}

// --- the emulator -----------------------------------------------------------

void checkVtBasics()
{
    Screen screen(20, 5);
    screen.feed(std::string("hello"));
    check("vt.print", screen.bufferLineText(0) == "hello");
    check("vt.cursor.after-print", screen.cursorRow() == 0 && screen.cursorColumn() == 5);

    screen.reset();
    screen.feed(std::string("ab\r\ncd"));
    check("vt.newline", screen.bufferLineText(0) == "ab" && screen.bufferLineText(1) == "cd");

    screen.reset();
    screen.feed(std::string("abc\rX"));
    check("vt.carriage-return", screen.bufferLineText(0) == "Xbc");

    screen.reset();
    screen.feed(std::string("ab\bX"));
    check("vt.backspace", screen.bufferLineText(0) == "aX");

    screen.reset();
    screen.feed(std::string("a\tb"));
    check("vt.tab-stops", screen.cellAt(0, 8).codepoint == U'b');

    screen.reset();
    screen.feed(std::string("h\xc3\xa9llo"));
    check("vt.utf8", screen.bufferLineText(0) == "h\xc3\xa9llo");

    screen.reset();
    screen.feed(std::string("abcdefghijklmnopqrstuv"));
    check("vt.wrap", screen.bufferLineText(0) == "abcdefghijklmnopqrst" && screen.bufferLineText(1) == "uv");
}

void checkVtControlSequences()
{
    Screen screen(20, 5);
    screen.feed(std::string("\x1b[3;5HX"));
    check("vt.csi.cup", screen.cellAt(2, 4).codepoint == U'X');

    screen.reset();
    screen.feed(std::string("abcdef\x1b[1;3H\x1b[K"));
    check("vt.csi.erase-to-end", screen.bufferLineText(0) == "ab");

    screen.reset();
    screen.feed(std::string("abcdef\x1b[1;3H\x1b[2P"));
    check("vt.csi.delete-chars", screen.bufferLineText(0) == "abef");

    screen.reset();
    screen.feed(std::string("abcdef\x1b[1;3H\x1b[2@"));
    check("vt.csi.insert-chars", screen.bufferLineText(0) == "ab  cdef");

    screen.reset();
    screen.feed(std::string("abc\x1b[2J"));
    check("vt.csi.erase-display", lastLine(screen).empty() && screen.bufferLineText(0).empty());

    screen.reset();
    screen.feed(std::string("one\r\ntwo\r\nthree\x1b[1;1H\x1b[1L"));
    check("vt.csi.insert-lines", screen.bufferLineText(0).empty() && screen.bufferLineText(1) == "one");
    check("vt.csi.insert-lines.shift", screen.bufferLineText(2) == "two");

    screen.reset();
    screen.feed(std::string("abcdef\x1b[6n"));
    check("vt.csi.dsr", screen.cursorColumn() == 6);
}

void checkVtSgr()
{
    Screen screen(20, 5);
    screen.feed(std::string("\x1b[1;31mX"));
    const auto &cell = screen.cellAt(0, 0);
    check("vt.sgr.bold", cell.attributes.bold);
    check("vt.sgr.red", cell.attributes.foreground == vedit::terminal::Color::indexed(1));

    screen.feed(std::string("\x1b[0mY"));
    check("vt.sgr.reset", screen.cellAt(0, 1).attributes.foreground.kind == vedit::terminal::Color::Kind::Default);

    screen.feed(std::string("\x1b[38;2;10;20;30mZ"));
    check("vt.sgr.rgb", screen.cellAt(0, 2).attributes.foreground == vedit::terminal::Color::rgb(10, 20, 30));

    screen.feed(std::string("\x1b[38;5;196mW"));
    check("vt.sgr.256", screen.cellAt(0, 3).attributes.foreground == vedit::terminal::Color::indexed(196));
}

void checkVtModes()
{
    Screen screen(20, 5);
    screen.feed(std::string("\x1b[?1h"));
    check("vt.mode.app-cursor-keys", screen.applicationCursorKeys());
    screen.feed(std::string("\x1b[?1l"));
    check("vt.mode.app-cursor-keys-off", !screen.applicationCursorKeys());

    screen.feed(std::string("\x1b[?2004h"));
    check("vt.mode.bracketed-paste", screen.bracketedPaste());

    screen.feed(std::string("\x1b[?25l"));
    check("vt.mode.cursor-hidden", !screen.cursorVisible());

    screen.reset();
    screen.feed(std::string("MAIN"));
    screen.feed(std::string("\x1b[?1049h"));
    screen.feed(std::string("ALT"));
    const bool altVisible = screen.bufferLineText(0) == "ALT" && screen.altScreen();
    screen.feed(std::string("\x1b[?1049l"));
    check("vt.alt-screen", altVisible && !screen.altScreen() && screen.bufferLineText(0) == "MAIN");

    screen.reset();
    screen.feed(std::string("\x1b]0;vedit terminal\x07"));
    check("vt.osc.title", screen.title() == "vedit terminal");
}

void checkVtScrollback()
{
    Screen screen(20, 5, 100);
    for (int line = 0; line < 30; ++line) {
        if (line > 0) {
            screen.feed(std::string("\r\n"));
        }
        char buffer[8];
        std::snprintf(buffer, sizeof buffer, "L%02d", line);
        screen.feed(std::string(buffer));
    }
    check("vt.scrollback.grew", screen.scrollbackSize() > 0);
    check("vt.scrollback.last-line", lastLine(screen) == "L29");
    const int total = screen.bufferLines();
    check("vt.scrollback.keeps-row-count", total == screen.scrollbackSize() + screen.rows());

    screen.setScrollOffset(3);
    check("vt.scrollback.offset", screen.scrollOffset() == 3 && screen.isScrolledBack());
    screen.setScrollOffset(0);
    check("vt.scrollback.back-to-live", screen.scrollOffset() == 0);

    Screen limited(20, 5, 10);
    for (int line = 0; line < 100; ++line) {
        limited.feed(std::string("x\r\n"));
    }
    check("vt.scrollback.limit", limited.scrollbackSize() == 10);
}

void checkVtReplies()
{
    Screen screen(20, 5);
    std::string reply;
    screen.setResponder([&reply](const std::string &bytes) { reply += bytes; });
    screen.feed(std::string("\x1b[3;5H\x1b[6n"));
    check("vt.reply.cursor-position", reply == "\x1b[3;5R");
    reply.clear();
    screen.feed(std::string("\x1b[c"));
    check("vt.reply.device-attributes", reply == "\x1b[?1;2c");
}

void checkVtSelection()
{
    Screen screen(20, 5);
    screen.feed(std::string("hello\r\nworld"));
    check("vt.selection.text", screen.textBetween(0, 0, 1, 3) == "hello\nwor");
    check("vt.selection.reversed", screen.textBetween(1, 3, 0, 0) == "hello\nwor");
}

void checkVtResize()
{
    Screen screen(20, 5);
    screen.feed(std::string("ABCDEFGH"));
    screen.resize(10, 3);
    check("vt.resize.columns", screen.columns() == 10);
    check("vt.resize.rows", screen.rows() == 3);
    check("vt.resize.keeps-text", screen.bufferLineText(0) == "ABCDEFGH");
}

// --- the local shell session ------------------------------------------------

void checkPtyExitCode()
{
    PtySession session;
    QByteArray output;
    QObject::connect(&session, &PtySession::outputReady, &session,
                     [&output](const QByteArray &data) { output += data; });
    QString error;
    const bool started = session.start(QStringLiteral("/bin/sh"), QString(),
                                       QStringList{QStringLiteral("-c"), QStringLiteral("printf hello; exit 7")},
                                       80, 24, &error);
    check("pty.start", started);
    check("pty.start.error-empty", error.isEmpty());
    check("pty.pid", session.pid() > 0);

    const bool finished = waitFor([&session]() { return !session.isRunning(); });
    check("pty.finished", finished);
    check("pty.output", output.contains("hello"));
    check("pty.exit-code", session.exitCode() == 7);
    check("pty.exit-known", session.exitKnown());
    check("pty.exit-description", session.exitDescription().contains(QStringLiteral("exited with code 7")));
    check("pty.exit-description-visible", session.exitDescription() == QStringLiteral("exited with code 7"));
}

void checkPtySignal()
{
    PtySession session;
    QString error;
    const bool started = session.start(QStringLiteral("/bin/sh"), QString(),
                                       QStringList{QStringLiteral("-c"), QStringLiteral("kill -TERM $$")}, 80, 24,
                                       &error);
    check("pty.signal.start", started);
    waitFor([&session]() { return !session.isRunning(); });
    check("pty.signal.termination-signal", session.terminationSignal() == 15);
    check("pty.signal.exit-code-128", session.exitCode() == 143);
    check("pty.signal.description", session.exitDescription().contains(QStringLiteral("SIGTERM")));
}

void checkPtyInteractive()
{
    PtySession session;
    QByteArray output;
    QObject::connect(&session, &PtySession::outputReady, &session,
                     [&output](const QByteArray &data) { output += data; });
    QString error;
    check("pty.interactive.start",
          session.start(QStringLiteral("/bin/sh"), QString(), QStringList(), 80, 24, &error));
    session.write("echo VEDIT_SESSION_READY\r");
    const bool sawMarker = waitFor([&output]() { return output.contains("VEDIT_SESSION_READY"); });
    check("pty.interactive.output", sawMarker);
    session.write("exit 3\r");
    const bool finished = waitFor([&session]() { return !session.isRunning(); });
    check("pty.interactive.exit", finished && session.exitCode() == 3);
}

void checkPtyResize()
{
    PtySession session;
    QByteArray output;
    QObject::connect(&session, &PtySession::outputReady, &session,
                     [&output](const QByteArray &data) { output += data; });
    QString error;
    check("pty.resize.start", session.start(QStringLiteral("/bin/sh"), QString(), QStringList(), 80, 24, &error));
    pump(100);
    session.resize(40, 12);
    session.write("stty size\r");
    const bool sawSize = waitFor([&output]() { return output.contains("12 40"); });
    check("pty.resize.tty-size", sawSize);
    session.write("exit 0\r");
    waitFor([&session]() { return !session.isRunning(); });
}

void checkPtyBadShell()
{
    PtySession session;
    QString error;
    const bool started = session.start(QStringLiteral("/nonexistent/shell"), QString(), QStringList(), 80, 24, &error);
    check("pty.bad-shell.refused", !started);
    check("pty.bad-shell.error", !error.isEmpty());
    check("pty.bad-shell.not-running", !session.isRunning());
}

// --- the panel --------------------------------------------------------------

void checkPanelRuns()
{
    TerminalOptions options;
    options.shell = QStringLiteral("/bin/sh");
    TerminalPanel panel(options);
    check("panel.auto-start", panel.isRunning());
    check("panel.pid", panel.session() != nullptr && panel.session()->pid() > 0);
    check("panel.shell-path", panel.shellPath() == QStringLiteral("/bin/sh"));
    check("panel.state-running", panel.stateText().contains(QStringLiteral("Running")));
    check("panel.state-has-pid", panel.stateText().contains(QStringLiteral("pid")));
    check("panel.banner-hidden", panel.bannerText().isEmpty());

    panel.session()->write("printf PANEL_OUTPUT\r");
    const bool sawOutput = waitFor([&panel]() {
        const auto &screen = panel.terminal()->screen();
        for (int line = screen.scrollbackSize(); line < screen.bufferLines(); ++line) {
            if (screen.bufferLineText(line).find("PANEL_OUTPUT") != std::string::npos) {
                return true;
            }
        }
        return false;
    });
    check("panel.output-reaches-screen", sawOutput);

    const QImage image = panel.terminal()->grab().toImage();
    check("panel.renders", !image.isNull() && image.width() > 0 && image.height() > 0);
    bool multipleColors = false;
    if (!image.isNull()) {
        const QRgb first = image.pixel(image.width() / 2, image.height() / 2);
        for (int y = 0; y < image.height() && !multipleColors; y += 3) {
            for (int x = 0; x < image.width(); x += 3) {
                if (image.pixel(x, y) != first) {
                    multipleColors = true;
                    break;
                }
            }
        }
    }
    check("panel.renders.two-colors", multipleColors);

    panel.session()->write("exit 0\r");
    waitFor([&panel]() { return !panel.isRunning(); });
}

void checkPanelExitStatusVisible()
{
    TerminalOptions options;
    options.shell = QStringLiteral("/bin/sh");
    TerminalPanel panel(options);
    check("panel.exit.start",
          panel.startShell(QStringLiteral("/bin/sh"), QString(),
                           QStringList{QStringLiteral("-c"), QStringLiteral("printf done; exit 42")}));
    const bool finished = waitFor([&panel]() { return !panel.isRunning(); });
    check("panel.exit.finished", finished);
    check("panel.exit.state-code", panel.stateText().contains(QStringLiteral("42")));
    check("panel.exit.banner-visible", panel.bannerText().contains(QStringLiteral("42")));
    check("panel.exit.banner-hint", panel.bannerText().contains(QStringLiteral("Restart")));
    bool sawDone = false;
    const auto &screen = panel.terminal()->screen();
    for (int line = screen.scrollbackSize(); line < screen.bufferLines(); ++line) {
        if (screen.bufferLineText(line).find("done") != std::string::npos) {
            sawDone = true;
            break;
        }
    }
    check("panel.exit.output-kept", sawDone);
}

void checkPanelRestart()
{
    TerminalOptions options;
    options.shell = QStringLiteral("/bin/sh");
    TerminalPanel panel(options);
    const qint64 firstPid = panel.session()->pid();
    check("panel.restart.start", panel.restart());
    check("panel.restart.running", panel.isRunning());
    check("panel.restart.new-pid", panel.session()->pid() > 0 && panel.session()->pid() != firstPid);
    check("panel.restart.banner-cleared", panel.bannerText().isEmpty());
    check("panel.restart.state-running", panel.stateText().contains(QStringLiteral("Running")));
    check("panel.restart.screen-cleared", panel.terminal()->screen().bufferLines() > 0
                                             && panel.terminal()->screen().bufferLineText(0).empty());
}

void checkPanelEnterRestarts()
{
    TerminalOptions options;
    options.shell = QStringLiteral("/bin/sh");
    TerminalPanel panel(options);
    panel.startShell(QStringLiteral("/bin/sh"), QString(),
                     QStringList{QStringLiteral("-c"), QStringLiteral("exit 5")});
    waitFor([&panel]() { return !panel.isRunning(); });
    QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(panel.terminal(), &press);
    check("panel.enter-restarts", waitFor([&panel]() { return panel.isRunning(); }, 4000));
}

void checkPanelDockable()
{
    QMainWindow window;
    auto *panel = new TerminalPanel(TerminalOptions{});
    auto *dock = new QDockWidget(QStringLiteral("Terminal"), &window);
    dock->setObjectName(QStringLiteral("terminal-dock"));
    dock->setWidget(panel);
    window.addDockWidget(Qt::BottomDockWidgetArea, dock);
    window.resize(900, 500);
    window.show();
    pump(120);

    check("panel.dock.content", dock->widget() == panel);
    check("panel.dock.visible", dock->isVisible());
    check("panel.dock.area", window.dockWidgetArea(dock) == Qt::BottomDockWidgetArea);

    dock->setFloating(true);
    pump(60);
    check("panel.dock.float", dock->isFloating());
    dock->setFloating(false);
    pump(60);

    dock->close();
    pump(60);
    check("panel.dock.closed", !dock->isVisible());
    dock->show();
    pump(60);
    check("panel.dock.reopened", dock->isVisible());

    // The pty follows the widget: ask the shell itself for its tty size.
    QByteArray output;
    QObject::connect(panel->session(), &PtySession::outputReady, panel->session(),
                     [&output](const QByteArray &data) { output += data; });
    panel->session()->write("stty size\r");
    const int columns = panel->terminal()->screen().columns();
    const int rows = panel->terminal()->screen().rows();
    const QString expected = QStringLiteral("%1 %2").arg(rows).arg(columns);
    const bool sawSize = waitFor([&output, &expected]() { return output.contains(expected.toUtf8()); });
    check("panel.dock.pty-size-follows-widget", sawSize);
}

void checkPanelCloseButton()
{
    QMainWindow window;
    auto *panel = new TerminalPanel(TerminalOptions{});
    auto *dock = new QDockWidget(QStringLiteral("Terminal"), &window);
    dock->setWidget(panel);
    window.addDockWidget(Qt::BottomDockWidgetArea, dock);
    window.show();
    pump(100);

    bool closeRequested = false;
    QObject::connect(panel, &TerminalPanel::closeRequested, panel, [&closeRequested]() { closeRequested = true; });
    auto *button = panel->findChild<QToolButton *>(QStringLiteral("terminal-close"));
    check("panel.close-button.exists", button != nullptr);
    if (button != nullptr) {
        QMetaObject::invokeMethod(button, "click", Qt::DirectConnection);
    }
    pump(80);
    check("panel.close-button.signal", closeRequested);
    check("panel.close-button.hides-dock", !dock->isVisible());
}

void checkPanelRestartButton()
{
    TerminalOptions options;
    options.shell = QStringLiteral("/bin/sh");
    TerminalPanel panel(options);
    const qint64 firstPid = panel.session()->pid();
    auto *button = panel.findChild<QToolButton *>(QStringLiteral("terminal-restart"));
    check("panel.restart-button.exists", button != nullptr);
    if (button != nullptr) {
        QMetaObject::invokeMethod(button, "click", Qt::DirectConnection);
    }
    pump(100);
    check("panel.restart-button.new-pid", panel.session() != nullptr && panel.session()->pid() != firstPid);
    check("panel.restart-button.running", panel.isRunning());
}

void checkPanelFactory()
{
    QWidget *widget = vedit::terminal::createTerminalPanel();
    check("panel.factory.widget", widget != nullptr);
    auto *panel = qobject_cast<TerminalPanel *>(widget);
    check("panel.factory.type", panel != nullptr);
    if (panel != nullptr) {
        check("panel.factory.starts-shell", panel->isRunning());
    }
    delete widget;

    auto factory = vedit::terminal::terminalPanelFactory();
    QWidget *second = factory();
    check("panel.factory.function", second != nullptr);
    delete second;
}

void checkWidgetKeys()
{
    TerminalOptions options;
    options.shell = QStringLiteral("/bin/sh");
    TerminalPanel panel(options);
    panel.show();
    pump(100);
    panel.terminal()->setFocus();
    for (const QString &text : {QStringLiteral("e"), QStringLiteral("c"), QStringLiteral("h"), QStringLiteral("o")}) {
        QKeyEvent press(QEvent::KeyPress, text.at(0).toUpper().unicode(), Qt::NoModifier, text);
        QApplication::sendEvent(panel.terminal(), &press);
    }
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
    QApplication::sendEvent(panel.terminal(), &enter);
    const bool saw = waitFor([&panel]() {
        const auto &screen = panel.terminal()->screen();
        for (int line = screen.scrollbackSize(); line < screen.bufferLines(); ++line) {
            if (screen.bufferLineText(line).find("echo") != std::string::npos) {
                return true;
            }
        }
        return false;
    });
    check("widget.key-events-to-pty", saw);
    panel.session()->write("exit 0\r");
    waitFor([&panel]() { return !panel.isRunning(); });
}

int runSelfCheck()
{
    checkVtBasics();
    checkVtControlSequences();
    checkVtSgr();
    checkVtModes();
    checkVtScrollback();
    checkVtReplies();
    checkVtSelection();
    checkVtResize();
    checkPtyExitCode();
    checkPtySignal();
    checkPtyInteractive();
    checkPtyResize();
    checkPtyBadShell();
    checkPanelRuns();
    checkPanelExitStatusVisible();
    checkPanelRestart();
    checkPanelEnterRestarts();
    checkPanelDockable();
    checkPanelCloseButton();
    checkPanelRestartButton();
    checkPanelFactory();
    checkWidgetKeys();

    std::printf("TERMINAL-SELFTEST ok=%d checks=%d failures=%d\n", g_failures == 0 ? 1 : 0, g_checks, g_failures);
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}

int runScreenshots(const QString &directory)
{
    if (!QDir().mkpath(directory)) {
        std::printf("TERMINAL-SCREENSHOTS ok=0 reason=cannot-create-%s\n", qPrintable(directory));
        return 1;
    }

    // The application shell applies its theme as a Qt palette at startup
    // (ThemeManager, task T13); the screenshot uses the same dark palette so the
    // panel's chrome matches what the product shows.
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(0x2B, 0x2B, 0x2B));
    darkPalette.setColor(QPalette::WindowText, QColor(0xD4, 0xD4, 0xD4));
    darkPalette.setColor(QPalette::Base, QColor(0x1E, 0x1E, 0x1E));
    darkPalette.setColor(QPalette::AlternateBase, QColor(0x25, 0x25, 0x25));
    darkPalette.setColor(QPalette::Text, QColor(0xD4, 0xD4, 0xD4));
    darkPalette.setColor(QPalette::Button, QColor(0x33, 0x33, 0x33));
    darkPalette.setColor(QPalette::ButtonText, QColor(0xD4, 0xD4, 0xD4));
    darkPalette.setColor(QPalette::Highlight, QColor(0x26, 0x4F, 0x78));
    darkPalette.setColor(QPalette::HighlightedText, QColor(0xFF, 0xFF, 0xFF));
    darkPalette.setColor(QPalette::ToolTipBase, QColor(0x1E, 0x1E, 0x1E));
    darkPalette.setColor(QPalette::ToolTipText, QColor(0xD4, 0xD4, 0xD4));
    QApplication::setPalette(darkPalette);

    QMainWindow window;
    window.resize(1100, 640);
    window.setWindowTitle(QStringLiteral("VEdit — Terminal panel"));
    window.setStyleSheet(QStringLiteral("QDockWidget::title { background: #2b2b2b; color: #d4d4d4;"
                                        " padding: 3px 8px; }"));

    // The shell owns the central editor area (src/app); this stand-in keeps the
    // dock a real *bottom* panel in the screenshot instead of a full-window dock.
    auto *editorArea = new QLabel(QStringLiteral("Editor area — owned by src/app (application shell)"));
    editorArea->setAlignment(Qt::AlignCenter);
    editorArea->setStyleSheet(QStringLiteral("background: #232323; color: #6f6f6f;"));
    window.setCentralWidget(editorArea);

    TerminalOptions options;
    options.shell = QStringLiteral("/bin/bash");
    auto *panel = new TerminalPanel(options);
    auto *dock = new QDockWidget(QStringLiteral("Terminal"), &window);
    dock->setObjectName(QStringLiteral("terminal-dock"));
    dock->setWidget(panel);
    window.addDockWidget(Qt::BottomDockWidgetArea, dock);
    window.show();

    if (!waitFor([panel]() { return panel->isRunning(); })) {
        std::printf("TERMINAL-SCREENSHOTS ok=0 reason=shell-did-not-start\n");
        return 1;
    }
    // Wait for the dock to be laid out before running anything: output produced
    // against a collapsed widget would be scrolled out by the later resize.
    window.resizeDocks({dock}, {400}, Qt::Vertical);
    if (!waitFor([panel]() { return panel->terminal()->screen().columns() > 60; })) {
        std::printf("TERMINAL-SCREENSHOTS ok=0 reason=panel-not-laid-out\n");
        return 1;
    }
    pump(200);
    std::printf("INFO terminal widget=%dx%d screen=%dx%d cell=%dx%d\n", panel->terminal()->width(),
                panel->terminal()->height(), panel->terminal()->screen().columns(),
                panel->terminal()->screen().rows(), panel->terminal()->cellSize().width(),
                panel->terminal()->cellSize().height());

    // The escapes are typed as printf's own \033 escapes: typing a raw ESC into
    // the shell line editor would be eaten by readline before printf sees it.
    panel->session()->write("clear\r");
    pump(200);
    panel->session()->write(
        "printf '\\033[1;32mvedit\\033[0m terminal panel \xe2\x80\x94 local shell session over a pty\\n'\r");
    panel->session()->write("ls --color=always -1 -d src/*/ cmake/ docs/\r");
    panel->session()->write("printf '\\033[33mexit code demo:\\033[0m '; false; echo $?\r");
    waitFor([panel]() {
        const auto &screen = panel->terminal()->screen();
        for (int line = screen.scrollbackSize(); line < screen.bufferLines(); ++line) {
            if (screen.bufferLineText(line).find("exit code demo: 1") != std::string::npos) {
                return true;
            }
        }
        return false;
    });
    pump(400);

    const QString running = directory + QStringLiteral("/10-terminal-running.png");
    const bool runningSaved = window.grab().save(running);
    std::printf("SCREENSHOT %s %s\n", runningSaved ? "OK" : "FAIL", qPrintable(running));

    panel->startShell(QStringLiteral("/bin/sh"), QString(),
                      QStringList{QStringLiteral("-c"), QStringLiteral("printf 'work done; exiting\\n'; exit 42")});
    waitFor([panel]() { return !panel->isRunning(); });
    pump(300);

    const QString exited = directory + QStringLiteral("/11-terminal-exited-code42.png");
    const bool exitedSaved = window.grab().save(exited);
    std::printf("SCREENSHOT %s %s\n", exitedSaved ? "OK" : "FAIL", qPrintable(exited));
    std::printf("TERMINAL-SCREENSHOTS ok=%d dir=%s\n", runningSaved && exitedSaved ? 1 : 0,
                qPrintable(directory));
    std::fflush(stdout);
    return runningSaved && exitedSaved ? 0 : 1;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-terminal-selftest"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Terminal panel self-check (T35)"));
    parser.addHelpOption();
    const QCommandLineOption selfcheck(QStringLiteral("selfcheck"), QStringLiteral("Run the headless checks."));
    const QCommandLineOption screenshots(QStringLiteral("screenshots"),
                                        QStringLiteral("Capture the panel screenshots into <dir>."),
                                        QStringLiteral("dir"));
    parser.addOption(selfcheck);
    parser.addOption(screenshots);
    parser.process(application);

    if (parser.isSet(screenshots)) {
        return runScreenshots(parser.value(screenshots));
    }
    if (parser.isSet(selfcheck)) {
        return runSelfCheck();
    }
    std::printf("usage: vedit-terminal-selftest --selfcheck | --screenshots <dir>\n");
    return 2;
}
