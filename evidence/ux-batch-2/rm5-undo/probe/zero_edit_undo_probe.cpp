// Probe RM-5a step 7 (answers the Lead's three questions with numbers).
//
// The user's second report: "在撤无可撤的情况下，再按 Ctrl+Z 会直接把当前显示的
// 文件清空" — undo until nothing is left, press Ctrl+Z once more, the document goes
// blank. This probe separates the two candidate mechanisms:
//
//   A  the load was undoable (pre-fix: Document::setText()): a document with ZERO
//      user edits still has ONE undo step (the load), so the very first Ctrl+Z
//      empties the buffer. This is the mechanism the fix removes.
//   B  a genuinely EMPTY undo stack over a NON-empty buffer (post-fix: load via
//      openUrl()): undo/redo are then no-ops, and the text survives.
//
// Cases (each on its own document, fresh):
//   A1  setText(load) + 0 edits  -> undo()      -> text size?
//   A2  setText(load) + 0 edits  -> undo()+redo -> text size?
//   B1  openUrl(load) + 0 edits  -> undo()      -> text size?
//   B2  openUrl(load) + 0 edits  -> redo()      -> text size?
//   A3  setText(load), undo() to empty, then redo() -> text restored?
#include <KTextEditor/Document>
#include <KTextEditor/Editor>

#include <QApplication>
#include <QFile>
#include <QMetaObject>
#include <QString>
#include <QTemporaryDir>
#include <QTextStream>
#include <QUrl>

namespace {
void out(const QString &line) { QTextStream(stdout) << line << Qt::endl; }
bool undo(KTextEditor::Document *d) { return QMetaObject::invokeMethod(d, "undo"); }
bool redo(KTextEditor::Document *d) { return QMetaObject::invokeMethod(d, "redo"); }
bool undoEnabled(KTextEditor::Document *d)
{
    bool enabled = false;
    if (!QMetaObject::invokeMethod(d, "isUndoAvailable", Q_RETURN_ARG(bool, enabled))) {
        return false;
    }
    return enabled;
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (editor == nullptr) { out(QStringLiteral("ZERO editor=null FAIL")); return 2; }
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("top.sv"));
    const QString content = QStringLiteral("module top;\n  wire a;\nendmodule\n");
    { QFile f(path); if (!f.open(QIODevice::WriteOnly)) return 2; f.write(content.toUtf8()); }

    const QString pre = QStringLiteral("setText");   // pre-fix load semantics
    const QString post = QStringLiteral("openUrl");  // post-fix load semantics

    for (const QString &mode : {pre, post}) {
        const int base = mode == pre ? 0 : 1;
        Q_UNUSED(base);
    }

    // A1/A2: pre-fix semantics (setText = undoable load)
    {
        KTextEditor::Document *d = editor->createDocument(nullptr);
        d->setText(content);
        d->setModified(false);
        out(QStringLiteral("ZERO A setText_load text=%1 zero_edits=1").arg(d->text().size()));
        undo(d);
        out(QStringLiteral("ZERO A1 one_undo text=%1 (blank if 0)").arg(d->text().size()));
        redo(d);
        out(QStringLiteral("ZERO A2 one_undo_one_redo text=%1").arg(d->text().size()));
        delete d;
    }
    // A3: undo the load then redo
    {
        KTextEditor::Document *d = editor->createDocument(nullptr);
        d->setText(content);
        d->setModified(false);
        undo(d);
        const int afterUndo = d->text().size();
        redo(d);
        out(QStringLiteral("ZERO A3 load_undo_then_redo after_undo=%1 after_redo=%2")
                .arg(afterUndo).arg(d->text().size()));
        delete d;
    }
    // B1/B2: post-fix semantics (openUrl = load is the undo floor)
    for (int i = 0; i < 2; ++i) {
        KTextEditor::Document *d = editor->createDocument(nullptr);
        if (!d->openUrl(QUrl::fromLocalFile(path))) { out(QStringLiteral("ZERO B open FAIL")); return 2; }
        const int loaded = d->text().size();
        const bool ran = i == 0 ? undo(d) : redo(d);
        out(QStringLiteral("ZERO B%1 openUrl_load loaded=%2 press=%3 returned=%4 after=%5")
                .arg(i + 1).arg(loaded)
                .arg(i == 0 ? QStringLiteral("undo") : QStringLiteral("redo"))
                .arg(ran ? 1 : 0).arg(d->text().size()));
        delete d;
    }
    return 0;
}
