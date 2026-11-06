#pragma once

// Bridge between the search dock and the editor's OPEN buffers (ux-batch-2 B2,
// the unsaved-buffer rule; b2-repair Blocker 1).
//
// A project-wide replace rewrites files on disk. If a file is also open in an
// editor with unsaved changes, writing the disk silently would make the buffer
// and the file disagree — and a later save would resurrect the old text on top
// of the replacement (or the replacement would be lost). The rule this bridge
// implements:
//
//   * before 「替换」 / 「全部替换」 every affected OPEN document that is
//     modified is saved first (the in-memory text wins, then the replacement is
//     applied on top of it);
//   * when such a document cannot be saved, the replace is REFUSED with the
//     reason — the files already saved stay saved, and nothing is replaced;
//   * after the replacement the affected open documents are reloaded from disk
//     ONLY while they still hold the content that was written. The caller
//     captures a content token when it saves; `refresh()` compares the CURRENT
//     buffer with that token and leaves a changed buffer untouched (reporting
//     it), so text typed while the batch runs is never discarded. The guard
//     lives in `refresh()` itself, so no caller can bypass it.
//
// The default implementation talks to the product's KTextEditor views; a build
// without the editor kernel returns nullptr and the dock reports that it cannot
// guarantee the rule (it does not silently skip it).
#include <QString>
#include <QStringList>

namespace vedit::workspace::search {

// What one `refresh()` did.
enum class RefreshStatus {
    Reloaded, // the buffer now shows the file's current bytes
    NotOpen,  // nothing open for that path: nothing to reload
    Changed,  // the buffer no longer holds the written content: left untouched
};

class OpenDocumentBridge
{
public:
    virtual ~OpenDocumentBridge() = default;

    // Absolute paths of open documents whose buffer differs from disk.
    virtual QStringList modifiedPaths() const = 0;
    virtual bool isOpen(const QString &absolutePath) const = 0;
    // Saves the buffer through the product's atomic save path.
    virtual bool save(const QString &absolutePath, QString *error) = 0;
    // The OPEN buffer's current text, or an empty string when the document is
    // not open. The replace path uses it only to decide whether a modified
    // document OUTSIDE the disk scan's hit files could contain a match that
    // exists only in the buffer (b2-repair-r2); it never feeds a replacement
    // range. Empty means "no buffer text to match", which for a non-empty
    // query is the correct answer.
    virtual QString bufferText(const QString &absolutePath) const
    {
        Q_UNUSED(absolutePath);
        return QString();
    }
    // Opaque token of the OPEN buffer's current text (empty when not open).
    // Captured at save time and handed back to refresh(): the reload is skipped
    // when the buffer no longer holds that exact text.
    virtual QString contentToken(const QString &absolutePath) const = 0;
    // Replaces the buffer with the file's current bytes ONLY when it still
    // holds `expectedToken` (an empty token means "was not open at capture
    // time": the reload then requires an unmodified buffer). A buffer that
    // changed meanwhile is reported as Changed and never touched.
    virtual RefreshStatus refresh(const QString &absolutePath, const QString &expectedToken,
                                  QString *error) = 0;
};

// The product bridge, or nullptr when KTextEditor is not linked in.
OpenDocumentBridge *defaultOpenDocumentBridge();

// Test/alternative injection. nullptr restores the default.
void setOpenDocumentBridge(OpenDocumentBridge *bridge);
OpenDocumentBridge *openDocumentBridge();

} // namespace vedit::workspace::search
