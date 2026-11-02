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

// Replaces the document buffer with the file's UTF-8 content and records the
// path. The kernel reads the bytes itself instead of KParts::openUrl(): the
// KParts open path starts a KIO job and can spin (or wait for an event loop)
// under the headless/offscreen platform this product is verified on, which made
// `--selfcheck` hang. Reading + setText keeps the open path synchronous and
// deterministic; the atomic save path is unaffected.
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
