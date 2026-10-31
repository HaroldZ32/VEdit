#include "editor_provider.h"

#include <QFontDatabase>
#include <QPlainTextEdit>
#include <QStringLiteral>

namespace vedit::app {

namespace {
int g_nextDemoIndex = 1;
} // namespace

QString DemoEditorProvider::id() const
{
    return QStringLiteral("demo");
}

QString DemoEditorProvider::displayName() const
{
    return QStringLiteral("demo editor (shell placeholder)");
}

QWidget *DemoEditorProvider::createEditor(QWidget *parent)
{
    auto *editor = new QPlainTextEdit(parent);
    editor->setObjectName(QStringLiteral("demo-editor"));
    editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    editor->setTabStopDistance(32);
    const int index = g_nextDemoIndex++;
    editor->setPlainText(QStringLiteral(
                             "VEdit application shell — demo editor pane #%1\n"
                             "\n"
                             "This text view is the shell's placeholder editor. The real editor\n"
                             "widget is injected by src/editor (edit-core) through the\n"
                             "EditorProvider seam; nothing in the shell depends on KTextEditor.\n"
                             "\n"
                             "Layout operations worth trying:\n"
                             "  * Ctrl+Shift+H  split horizontally (side by side)\n"
                             "  * Ctrl+Shift+V  split vertically (stacked)\n"
                             "  * F6            focus the next pane\n"
                             "  * Ctrl+W        close the focused pane\n"
                             "  * View menu     insert / hide the panel docks on demand\n")
                             .arg(index));
    return editor;
}

} // namespace vedit::app
