#pragma once

// Session persistence for the application shell.
//
// One JSON file per state directory holds everything the shell must restore on
// the next start: the split layout (pane tree + active pane), the open documents
// of every tab (path, caret, scroll), the inserted docks and their QMainWindow
// dock state, the window geometry, the theme and the project root.
// Malformed/unreadable files are reported and ignored — the shell must still
// start (bad-config robustness is a product requirement; the settings lane owns
// the user-facing diagnostics later).
//
// --- archive format (schema v2) ---------------------------------------------
// The document is ONE JSON object; the keys below are the whole contract:
//
//   {
//     "version": 2,                       // archive schema version
//     "project": "/abs/project/root",     // empty => no project. The project lane
//                                         // reads this same key: project_integration.cpp
//     "theme": "Breeze Dark",             // empty => theme default
//     "uiTheme": "bohe",                  // UI-Mosaic T3: product theme id;
//                                         // empty => not chosen yet. Read and
//                                         // written by ApplicationShell itself
//                                         // (restoreSession/saveSession) so this
//                                         // store's typed contract is unchanged.
//     "geometry": "<base64>",             // QMainWindow::saveGeometry()
//     "dockState": "<base64>",            // QMainWindow::saveState()
//     "panels": ["file-tree", "outline"], // inserted dock ids, in insert order
//     "editorLayout": {                   // EditorArea::saveLayout() (v2)
//       "version": 2, "active": "pane-1", "nextPane": 3,
//       "root": <layout node>             // leaf/split tree, see below
//     }
//   }
//
// A layout leaf (v2) is the pane's persisted document state, defined by
// SessionPane below:
//
//   { "type": "leaf", "pane": "pane-1", "provider": "ktexteditor",
//     "activeTab": 1,
//     "documents": [ { "path": "/abs/a.sv",
//                      "caret": { "line": 12, "column": 4 },
//                      "scroll": 480 }, ... ] }
//
// Scope decisions (requirements C13): the terminal scrollback, the VCS/search
// panel's transient state and FIM ghost text are deliberately NOT archived; only
// what is listed above is restored. A dirty document is saved (or the close is
// refused) BEFORE the archive is written, so the archive never has to carry
// unsaved buffers — see ApplicationShell::closeEvent().
//
// Tolerated input: version 1 archives (no per-pane "documents"/"activeTab", no
// caret/scroll) and any unknown key or missing field. Whatever can be read is
// used and the rest keeps its default; this store never throws and never makes
// the shell refuse to start.
#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace vedit::app {

// One open document as the archive stores it. The caret is 0-based (line = the
// document line, column = the column inside that line) and `scrollTop` is the
// vertical scrollbar value of the provider's viewport, in that provider's own
// units. A negative value means "not known" and is simply not applied; the
// `provider` field of the enclosing pane keeps an offset from being replayed
// into a different editor implementation.
struct SessionDocument {
    QString path;
    int caretLine = -1;
    int caretColumn = -1;
    int scrollTop = -1;

    bool hasPath() const;
    bool hasCaret() const;
    bool hasScroll() const;
    bool isValid() const;

    QJsonObject toJson() const;
    // Tolerant: a malformed entry yields the defaults above, never an error.
    static SessionDocument fromJson(const QJsonObject &object);
};

// One editor pane of the split layout, plus every tab it holds. This IS the
// archive leaf; `toJson()` writes the complete leaf object (including
// "type": "leaf") so the layout tree writer and the schema stay side by side.
struct SessionPane {
    QString paneId;
    QString provider; // provider id the caret/scroll offsets were captured with
    int activeTab = 0;
    QList<SessionDocument> documents;

    bool isEmpty() const;

    QJsonObject toJson() const;
    static SessionPane fromJson(const QJsonObject &leaf);
};

// The whole session archive. The shell fills it before saving and reads it back
// after loading; the layout tree itself stays owned by EditorArea (it is the
// editor area's model, not the store's), so `editorLayout` is carried as the
// JSON object EditorArea::saveLayout() produced.
struct SessionState {
    static constexpr int kCurrentVersion = 2;

    int version = kCurrentVersion;
    QString projectRoot;
    QString theme;
    // UI-Mosaic T3: the product (chrome) theme id. Empty means "not chosen yet".
    // ApplicationShell serialises this ONE key itself (see the archive format
    // note above), which is why toJson()/fromJson() do not mention it.
    QString uiTheme;
    QByteArray geometry;
    QByteArray dockState;
    QStringList panels;
    QJsonObject editorLayout;

    bool hasWindowState() const;
    bool isValid() const;

    QJsonObject toJson() const;
    // Tolerant: every field falls back to its default; the parsed `version` is
    // kept so a caller can report an upgrade from a v1 archive.
    static SessionState fromJson(const QJsonObject &object);
};

class SessionStore {
public:
    explicit SessionStore(QString filePath);

    QString path() const;

    // Reads the session. A missing file returns an empty object with no error.
    QJsonObject load(QString *error) const;

    // Atomically writes the session (creates parent directories).
    bool save(const QJsonObject &object, QString *error) const;

    // Typed access to the same document. `state.isValid()` is false when the
    // file is missing or empty; a file that exists but cannot be parsed is
    // reported through `error` exactly like load().
    SessionState loadState(QString *error) const;
    bool saveState(const SessionState &state, QString *error) const;

    static QString defaultFilePath();

private:
    QString path_;
};

} // namespace vedit::app
