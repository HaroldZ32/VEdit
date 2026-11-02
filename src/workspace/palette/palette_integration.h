#pragma once

// App-facing adapter of the Ctrl+P palette (task T22).
//
// The palette core (fuzzy matcher / shared file index / widget) deliberately
// does not depend on src/app. This adapter is the only file that includes the
// application shell. It registers the `file.goto-file` command through the
// shell's CommandRegistry - so rebinding through IShortcutStore keeps working -
// and it owns the shared WorkspaceFileIndex that the palette filters.
//
// File opening is a callback seam: the palette reports the absolute path and
// this adapter forwards it to the editor. Until edit-core lands it loads the
// file into the shell's demo editor (a QPlainTextEdit); an injected handler
// replaces that bridge at any time (setFileOpenHandler), so the app layer never
// depends on the editor lane.
#include <QAction>
#include <QString>

#include <functional>
#include <memory>

#include "../integration.h"

class QObject;

namespace vedit::app {
class ApplicationShell;
}

namespace vedit::workspace {

class CommandPalette;
class WorkspaceFileIndex;

class PaletteIntegration final : public QObject {
    Q_OBJECT
public:
    ~PaletteIntegration() override;

    // Idempotent: a second install on the same shell updates the project root
    // and returns the existing adapter.
    static PaletteIntegration *install(vedit::app::ApplicationShell &shell,
                                       const QString &projectRoot);
    static PaletteIntegration *instance(const vedit::app::ApplicationShell &shell);

    WorkspaceFileIndex *fileIndex();
    const WorkspaceFileIndex *fileIndex() const;
    CommandPalette *palette();
    QAction *action() const;

    void openFilePalette();
    void setProjectRoot(const QString &projectRoot);
    void setFileOpenHandler(std::function<void(const QString &)> handler);

    // Last path the palette activated (test / evidence surface).
    const QString &openedPath() const;

    // Publishes the command to the shell (called by install()).
    void registerCommand();

private:
    explicit PaletteIntegration(vedit::app::ApplicationShell *shell);

    void ensureIndexReady();
    void openInEditor(const QString &absolutePath);

    vedit::app::ApplicationShell *shell_ = nullptr;
    std::unique_ptr<WorkspaceFileIndex> index_;
    CommandPalette *palette_ = nullptr; // owned by the shell (QWidget parent)
    QAction *action_ = nullptr;
    std::function<void(const QString &)> openHandler_;
    QString openedPath_;
};

// Convention used by src/workspace/integration.cpp: every workspace lane
// exports install<Lane>(shell, options) from <lane>_integration.h.
void installPalette(vedit::app::ApplicationShell &shell, const IntegrationOptions &options);

} // namespace vedit::workspace
