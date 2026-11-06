#pragma once

// Document wrapper around KTextEditor::Document (G2 / C1): the product's text
// buffer, opened from a file and saved through the kernel's atomic save.
#include <QString>

#include <memory>

namespace KTextEditor {
class Document;
class Editor;
} // namespace KTextEditor

namespace vedit::editor {

// Replaces the document buffer with the file's content and records the path.
// The load goes through KParts::ReadOnlyPart::openUrl() — the documented
// "load a document" entry — because that leaves the loaded state as the undo
// FLOOR: a later Ctrl+Z stops at the text the file was opened with.
// RM-5a: the old read + Document::setText() path recorded the whole load as ONE
// undoable step, so repeated Ctrl+Z walked the buffer back to EMPTY (the editor
// area went blank and the undo command greyed out, while the file on disk was
// intact). Measured on the pinned image: openUrl() is synchronous for a LOCAL
// file (4-6 ms, offscreen and xcb), sets the right text and leaves
// modified=false; the earlier "KIO job spins under offscreen" concern did not
// reproduce for local files. The atomic save path is unaffected.
bool loadFileIntoDocument(KTextEditor::Document *document, const QString &absolutePath,
                          QString *error = nullptr);

class EditorDocument
{
public:
    // nullptr + *error when KTextEditor::Editor::instance() is null (the
    // katepart plugin is part of the pinned image) or document creation fails.
    static std::unique_ptr<EditorDocument> create(QString *error = nullptr);

    ~EditorDocument();
    EditorDocument(const EditorDocument &) = delete;
    EditorDocument &operator=(const EditorDocument &) = delete;

    KTextEditor::Document *document() const;

    bool setText(const QString &text, QString *error = nullptr);
    QString text() const;
    int lines() const;
    int lineLength(int line) const;

    // Replaces the buffer with the file's UTF-8 content (product open path).
    bool openFile(const QString &absolutePath, QString *error = nullptr);
    QString filePath() const;
    bool isModified() const;
    void setModified(bool modified);

    // Atomic save of the current buffer to the path it was opened from
    // (or to `pathTo` when given). Clears the modified flag on success.
    bool saveAtomic(QString *error = nullptr, const QString &pathTo = QString());

private:
    EditorDocument() = default;

    KTextEditor::Document *document_ = nullptr;
    QString filePath_;
};

} // namespace vedit::editor
