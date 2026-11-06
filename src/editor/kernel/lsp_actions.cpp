#include "lsp_actions.h"

#include "atomic_save.h"
#include "multi_cursor.h"

#include <KTextEditor/Cursor>
#include <KTextEditor/Document>
#include <KTextEditor/Range>
#include <KTextEditor/View>

#include <QAction>
#include <QApplication>
#include <QChildEvent>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QKeyEvent>
#include <QMainWindow>
#include <QSet>
#include <QStatusBar>
#include <QWidget>

#include <algorithm>
#include <cstdio>

namespace vedit::editor {
namespace {

constexpr auto kChannelObjectName = "vedit-lsp-action-channel";
constexpr auto kNavigatorObjectName = "vedit-diagnostic-navigator";
constexpr auto kGatesProperty = "vedit.diagnostics.gates";
constexpr auto kCapabilitiesProperty = "vedit.diagnostics.capabilities";
constexpr auto kActionObjectPrefix = "action-";
constexpr auto kOpenFilePathProperty = "vedit.openFilePath";

const QList<EditorAction> &menuOrder()
{
    static const QList<EditorAction> order{
        EditorAction::Definition, EditorAction::References, EditorAction::Rename,
        EditorAction::Format,     EditorAction::Hover,      EditorAction::NextDiagnostic,
        EditorAction::PreviousDiagnostic};
    return order;
}

bool isNavigation(EditorAction action)
{
    return action == EditorAction::NextDiagnostic || action == EditorAction::PreviousDiagnostic;
}

/// This build's request paths: every LSP action the diagnostics lane wires
/// through a shell command has one (definition / references / rename / format /
/// hover). Hover became a real request path in T16 (`textDocument/hover` via
/// the C ABI), so an advertising backend now enables it instead of greying it.
bool hasRequestPath(EditorAction action)
{
    switch (action) {
    case EditorAction::Definition:
    case EditorAction::References:
    case EditorAction::Rename:
    case EditorAction::Format:
    case EditorAction::Hover:
        return true;
    case EditorAction::NextDiagnostic:
    case EditorAction::PreviousDiagnostic:
        return false;
    }
    return false;
}

QMainWindow *windowOf(const KTextEditor::View *view)
{
    return view != nullptr ? qobject_cast<QMainWindow *>(view->window()) : nullptr;
}

QList<DiagnosticAnnotation> annotationsOf(const KTextEditor::View *view)
{
    if (view == nullptr) {
        return {};
    }
    DiagnosticsAnnotations *binding = DiagnosticsAnnotations::forView(view);
    return binding != nullptr ? binding->entries() : QList<DiagnosticAnnotation>{};
}

QString describeDiagnostic(const DiagnosticAnnotation &entry, int index, int total)
{
    const QString message =
        entry.message.size() > 120 ? entry.message.left(117) + QStringLiteral("...") : entry.message;
    // T27: user-visible (F8 puts it in the status bar), so both the template and
    // the severity word it embeds go through the translator.
    return QCoreApplication::translate("VEdit", "Diagnostic %1/%2 - %3: %4")
        .arg(index + 1)
        .arg(total)
        .arg(diagnosticsSeverityName(entry.severity), message);
}


// ---------------------------------------------------------------------------
// LSP navigation results (AC-2.1/AC-2.2): the engine behind applyWorkspaceEdit()
// and buildReferenceRows().
// ---------------------------------------------------------------------------

constexpr int kSnippetLength = 200;

/// Every KTextEditor view of the process: panes and tabs are QWidget children of
/// the shell's top-level windows, so walking them covers every open tab (not only
/// the visible one). A view without a parent widget is itself a top-level widget
/// and is picked up by the same loop; the object-tree walk catches a view owned
/// by a non-widget parent.
QList<KTextEditor::View *> allViews()
{
    QList<KTextEditor::View *> views;
    QSet<KTextEditor::View *> seen;
    const auto add = [&views, &seen](KTextEditor::View *view) {
        if (view != nullptr && !seen.contains(view)) {
            seen.insert(view);
            views.append(view);
        }
    };
    QCoreApplication *application = QCoreApplication::instance();
    if (application == nullptr) {
        return views;
    }
    if (auto *widgets = qobject_cast<QApplication *>(application)) {
        for (QWidget *widget : widgets->topLevelWidgets()) {
            for (KTextEditor::View *view : widget->findChildren<KTextEditor::View *>()) {
                add(view);
            }
            add(qobject_cast<KTextEditor::View *>(widget));
        }
    }
    for (KTextEditor::View *view : application->findChildren<KTextEditor::View *>()) {
        add(view);
    }
    return views;
}

/// The open documents, keyed by absolute path. The path both providers record on
/// the view is the primary key (editor_provider.h); the document URL is the
/// fallback for a view opened some other way.
QHash<QString, KTextEditor::Document *> openDocumentsByPath()
{
    QHash<QString, KTextEditor::Document *> documents;
    for (KTextEditor::View *view : allViews()) {
        KTextEditor::Document *document = view != nullptr ? view->document() : nullptr;
        if (document == nullptr) {
            continue;
        }
        QString path = view->property(kOpenFilePathProperty).toString();
        if (path.isEmpty() && document->url().isLocalFile()) {
            path = document->url().toLocalFile();
        }
        if (path.isEmpty()) {
            continue;
        }
        documents.insert(QFileInfo(path).absoluteFilePath(), document);
    }
    return documents;
}

struct FilePlan {
    QString path;
    KTextEditor::Document *document = nullptr; // non-null => open in the editor
    QString text;                              // disk text, when not open
    QList<WorkspaceTextEdit> edits;            // later positions first
};

/// Applies the edits to a plain string. Positions are UTF-16 columns measured in
/// the QString, which is exactly what LSP reports; only the edited spans are
/// replaced, so the rest of the file is preserved byte for byte.
bool applyEditsToText(QString *text, const QList<WorkspaceTextEdit> &edits, QString *error)
{
    QList<int> lineStarts;
    lineStarts.append(0);
    for (int index = 0; index < text->size(); ++index) {
        if (text->at(index) == QLatin1Char('\n') && index + 1 <= text->size()) {
            lineStarts.append(index + 1);
        }
    }
    const auto offsetOf = [&lineStarts, text](int line, int column, bool *ok) -> int {
        if (line < 0 || line >= lineStarts.size()) {
            *ok = false;
            return 0;
        }
        const int start = lineStarts.at(line);
        int end = start;
        while (end < text->size() && text->at(end) != QLatin1Char('\n')) {
            ++end;
        }
        if (column < 0 || start + column > end) {
            *ok = false;
            return 0;
        }
        *ok = true;
        return start + column;
    };
    for (const WorkspaceTextEdit &edit : edits) {
        bool startOk = false;
        bool endOk = false;
        const int start = offsetOf(edit.startLine, edit.startColumn, &startOk);
        const int end = offsetOf(edit.endLine, edit.endColumn, &endOk);
        if (!startOk || !endOk || end < start) {
            if (error != nullptr) {
                *error = QCoreApplication::translate(
                             "VEdit", "edit %1:%2-%3:%4 is outside the file")
                             .arg(edit.startLine + 1)
                             .arg(edit.startColumn + 1)
                             .arg(edit.endLine + 1)
                             .arg(edit.endColumn + 1);
            }
            return false;
        }
        text->replace(start, end - start, edit.newText);
    }
    return true;
}

bool validateAgainstDocument(const WorkspaceTextEdit &edit, KTextEditor::Document *document,
                             const QString &path, QString *error)
{
    if (document == nullptr) {
        return true;
    }
    const int lines = document->lines();
    if (edit.startLine < 0 || edit.endLine < edit.startLine || edit.startLine >= lines
        || edit.endLine >= lines) {
        if (error != nullptr) {
            *error = QCoreApplication::translate("VEdit", "edit line %1-%2 is outside %3 (%4 line(s))")
                         .arg(edit.startLine + 1)
                         .arg(edit.endLine + 1)
                         .arg(QFileInfo(path).fileName())
                         .arg(lines);
        }
        return false;
    }
    const int startLength = document->lineLength(edit.startLine);
    const int endLength = document->lineLength(edit.endLine);
    if (edit.startColumn < 0 || edit.startColumn > startLength || edit.endColumn < 0
        || edit.endColumn > endLength) {
        if (error != nullptr) {
            *error = QCoreApplication::translate("VEdit", "edit column %1-%2 is outside the line")
                         .arg(edit.startColumn + 1)
                         .arg(edit.endColumn + 1);
        }
        return false;
    }
    return true;
}

bool readTextFile(const QString &path, QString *text, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = QCoreApplication::translate("VEdit", "cannot change %1: %2")
                         .arg(path, file.errorString());
        }
        return false;
    }
    const QByteArray bytes = file.readAll();
    if (bytes.contains('\0')) {
        if (error != nullptr) {
            *error = QCoreApplication::translate("VEdit", "cannot change %1: it looks like a binary file")
                         .arg(path);
        }
        return false;
    }
    *text = QString::fromUtf8(bytes);
    return true;
}

/// The source line of `path`, from the OPEN buffer when the file is open.
QString lineText(const QString &path, int line,
                 const QHash<QString, KTextEditor::Document *> &documents)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    if (KTextEditor::Document *document = documents.value(absolute, nullptr)) {
        return line >= 0 && line < document->lines() ? document->line(line) : QString();
    }
    QFile file(absolute);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    int current = 0;
    while (!file.atEnd()) {
        const QByteArray raw = file.readLine();
        if (current == line) {
            const QString text = QString::fromUtf8(raw);
            return text.endsWith(QLatin1Char('\n')) ? text.left(text.size() - 1) : text;
        }
        ++current;
    }
    return QString();
}

} // namespace

QString editorActionCapability(EditorAction action)
{
    switch (action) {
    case EditorAction::Definition:
        return QStringLiteral("definition");
    case EditorAction::References:
        return QStringLiteral("references");
    case EditorAction::Rename:
        return QStringLiteral("rename");
    case EditorAction::Format:
        return QStringLiteral("formatting");
    case EditorAction::Hover:
        return QStringLiteral("hover");
    case EditorAction::NextDiagnostic:
    case EditorAction::PreviousDiagnostic:
        return QString();
    }
    return QString();
}

QString editorActionCommand(EditorAction action)
{
    switch (action) {
    case EditorAction::Definition:
        return QStringLiteral("diagnostics.definition");
    case EditorAction::References:
        return QStringLiteral("diagnostics.references");
    case EditorAction::Rename:
        return QStringLiteral("diagnostics.rename");
    case EditorAction::Format:
        return QStringLiteral("diagnostics.format");
    case EditorAction::Hover:
        return QStringLiteral("diagnostics.hover");
    case EditorAction::NextDiagnostic:
    case EditorAction::PreviousDiagnostic:
        return QString();
    }
    return QString();
}

QString editorActionLabel(EditorAction action)
{
    // UX batch 2 (item 7): these seven labels are the editor's right-click menu
    // (five language-server actions + the two diagnostic navigators). They are
    // user-visible strings, so they go through the translator like every other
    // label; the SAME source spellings are the command texts the diagnostics
    // lane registers, so one catalogue entry serves both surfaces.
    switch (action) {
    case EditorAction::Definition:
        return QCoreApplication::translate("VEdit", "Go to &Definition");
    case EditorAction::References:
        return QCoreApplication::translate("VEdit", "Find &References");
    case EditorAction::Rename:
        return QCoreApplication::translate("VEdit", "&Rename Symbol...");
    case EditorAction::Format:
        return QCoreApplication::translate("VEdit", "&Format Document");
    case EditorAction::Hover:
        return QCoreApplication::translate("VEdit", "Show &Hover");
    case EditorAction::NextDiagnostic:
        return QCoreApplication::translate("VEdit", "&Next Diagnostic");
    case EditorAction::PreviousDiagnostic:
        return QCoreApplication::translate("VEdit", "&Previous Diagnostic");
    }
    return QString();
}

QKeySequence editorActionShortcut(EditorAction action)
{
    switch (action) {
    case EditorAction::Definition:
        return QKeySequence(QStringLiteral("F12"));
    case EditorAction::References:
        return QKeySequence(QStringLiteral("Shift+F12"));
    case EditorAction::Rename:
        return QKeySequence(QStringLiteral("F2"));
    case EditorAction::Format:
        // U3r2-F2: the ONE default for the format command. The registry's
        // `diagnostics.format` registration derives from this function
        // (src/workspace/diagnostics/diagnostics_integration.cpp), so the
        // context menu's display fallback and the menu/toolbar QAction (which
        // shares the registry action) can never disagree.
        return QKeySequence(QStringLiteral("Ctrl+Shift+F"));
    case EditorAction::Hover:
        return QKeySequence();
    case EditorAction::NextDiagnostic:
        return QKeySequence(QStringLiteral("F8"));
    case EditorAction::PreviousDiagnostic:
        return QKeySequence(QStringLiteral("Shift+F8"));
    }
    return QKeySequence();
}

QString editorActionName(EditorAction action)
{
    switch (action) {
    case EditorAction::Definition:
        return QStringLiteral("definition");
    case EditorAction::References:
        return QStringLiteral("references");
    case EditorAction::Rename:
        return QStringLiteral("rename");
    case EditorAction::Format:
        return QStringLiteral("format");
    case EditorAction::Hover:
        return QStringLiteral("hover");
    case EditorAction::NextDiagnostic:
        return QStringLiteral("next-diagnostic");
    case EditorAction::PreviousDiagnostic:
        return QStringLiteral("previous-diagnostic");
    }
    return QString();
}

QVariantMap publishedGates(QWidget *host)
{
    return host != nullptr ? host->property(kGatesProperty).toMap() : QVariantMap();
}

QString publishedBackend(QWidget *host)
{
    return host != nullptr
               ? host->property(kCapabilitiesProperty).toMap().value(QStringLiteral("backend"))
                     .toString()
               : QString();
}

QAction *shellActionFor(QWidget *host, EditorAction action)
{
    const QString command = editorActionCommand(action);
    if (host == nullptr || command.isEmpty()) {
        return nullptr;
    }
    return host->findChild<QAction *>(QLatin1String(kActionObjectPrefix) + command);
}

QString viewFilePath(const KTextEditor::View *view)
{
    if (view == nullptr) {
        return QString();
    }
    const QVariant property = view->property(kOpenFilePathProperty);
    if (property.isValid() && !property.toString().isEmpty()) {
        return property.toString();
    }
    KTextEditor::Document *document = view->document();
    return document != nullptr ? document->url().toLocalFile() : QString();
}

void logEditorActionLine(const QString &line)
{
    if (qEnvironmentVariableIsSet("VEDIT_QUIET")) {
        return;
    }
    std::printf("%s\n", line.toUtf8().constData());
    std::fflush(stdout);
}

// --- LspActionChannel ------------------------------------------------------

LspActionChannel::LspActionChannel(KTextEditor::View *view)
    : QObject(view)
    , view_(view)
    , dispatches_(menuOrder().size(), 0)
    , refusals_(menuOrder().size(), 0)
{
    setObjectName(QString::fromLatin1(kChannelObjectName));
}

LspActionChannel *LspActionChannel::install(KTextEditor::View *view)
{
    if (view == nullptr) {
        return nullptr;
    }
    if (LspActionChannel *existing = instanceFor(view)) {
        return existing;
    }
    return new LspActionChannel(view);
}

LspActionChannel *LspActionChannel::instanceFor(const KTextEditor::View *view)
{
    return view != nullptr
               ? view->findChild<LspActionChannel *>(QString::fromLatin1(kChannelObjectName))
               : nullptr;
}

int LspActionChannel::indexOf(EditorAction action)
{
    return static_cast<int>(action);
}

QList<EditorAction> LspActionChannel::actions() const
{
    return menuOrder();
}

ActionAvailability LspActionChannel::availability(EditorAction action) const
{
    ActionAvailability result;
    if (isNavigation(action)) {
        const QList<DiagnosticAnnotation> entries = orderedDiagnostics(annotationsOf(view_));
        result.source = QStringLiteral("editor");
        result.enabled = !entries.isEmpty();
        if (!result.enabled) {
            result.reason = QStringLiteral("no diagnostics in this file");
        }
        return result;
    }

    QWidget *host = view_ != nullptr ? view_->window() : nullptr;
    if (host == nullptr || viewFilePath(view_).isEmpty()) {
        result.enabled = false;
        result.source = QStringLiteral("editor");
        result.reason = QStringLiteral("no file is open in this editor");
        return result;
    }
    const QVariantMap gates = publishedGates(host);
    if (gates.isEmpty()) {
        result.enabled = false;
        result.known = false;
        result.source = QStringLiteral("none");
        result.reason =
            QStringLiteral("no diagnostics backend is installed in this window (no "
                           "'vedit.diagnostics.gates' was published)");
        return result;
    }

    const QString capability = editorActionCapability(action);
    const QVariantMap row = gates.value(capability).toMap();
    if (row.isEmpty()) {
        result.enabled = false;
        result.known = false;
        result.source = QStringLiteral("none");
        result.reason = QStringLiteral("the backend capability table has no '%1' row").arg(capability);
        return result;
    }

    result.enabled = row.value(QStringLiteral("enabled")).toBool();
    result.known = row.value(QStringLiteral("known"), true).toBool();
    result.source = row.value(QStringLiteral("source")).toString();
    result.reason = row.value(QStringLiteral("reason")).toString();
    if (!result.enabled) {
        if (result.reason.isEmpty()) {
            result.reason = result.known
                                ? QStringLiteral("the %1 backend does not support %2")
                                      .arg(publishedBackend(host), capability)
                                : QStringLiteral("connect to %1 to know whether it supports %2")
                                      .arg(publishedBackend(host).isEmpty() ? QStringLiteral("the backend")
                                                                            : publishedBackend(host),
                                           capability);
        }
        return result;
    }

    if (!hasRequestPath(action)) {
        result.enabled = false;
        result.known = false;
        result.source = QStringLiteral("unwired");
        const QString backend = publishedBackend(host);
        result.reason =
            QStringLiteral("%1 advertises %2, but this build has no %2 request in the LSP action "
                           "channel")
                .arg(backend.isEmpty() ? QStringLiteral("the backend") : backend, capability);
        return result;
    }

    QAction *wired = shellAction(action);
    if (wired == nullptr) {
        result.enabled = false;
        result.source = QStringLiteral("unwired");
        result.reason = QStringLiteral("no '%1' action is registered in this window")
                            .arg(editorActionCommand(action));
        return result;
    }
    if (!wired->isEnabled()) {
        result.enabled = false;
        if (!wired->toolTip().isEmpty()) {
            result.reason = wired->toolTip();
        } else if (result.reason.isEmpty()) {
            result.reason = QStringLiteral("the '%1' action is disabled")
                                .arg(editorActionCommand(action));
        }
    }
    return result;
}

QAction *LspActionChannel::shellAction(EditorAction action) const
{
    return shellActionFor(view_ != nullptr ? view_->window() : nullptr, action);
}

ActionDispatch LspActionChannel::dispatch(EditorAction action)
{
    ActionDispatch result;
    const ActionAvailability state = availability(action);
    if (!state.enabled) {
        result.reason = state.reason;
        lastRefusal_ = result.reason;
        ++refusals_[indexOf(action)];
        logEditorActionLine(
            QStringLiteral("ACTION-REFUSED action=%1 enabled=0 known=%2 source=%3 reason='%4'")
                .arg(editorActionName(action))
                .arg(state.known ? 1 : 0)
                .arg(state.source, state.reason));
        return result;
    }

    if (isNavigation(action)) {
        DiagnosticNavigator *navigator = DiagnosticNavigator::instanceFor(view_);
        if (navigator == nullptr) {
            result.reason = QStringLiteral("the diagnostics navigator is not installed on this view");
            lastRefusal_ = result.reason;
            ++refusals_[indexOf(action)];
            logEditorActionLine(QStringLiteral("ACTION-REFUSED action=%1 reason='%2'")
                                    .arg(editorActionName(action), result.reason));
            return result;
        }
        navigator->navigate(action == EditorAction::NextDiagnostic);
        ++dispatches_[indexOf(action)];
        result.dispatched = true;
        return result;
    }

    QAction *action_ = shellAction(action);
    if (action_ == nullptr || !action_->isEnabled()) {
        result.reason =
            action_ == nullptr
                ? QStringLiteral("no '%1' action is registered in this window")
                      .arg(editorActionCommand(action))
                : (action_->toolTip().isEmpty()
                       ? QStringLiteral("the '%1' action is disabled")
                             .arg(editorActionCommand(action))
                       : action_->toolTip());
        lastRefusal_ = result.reason;
        ++refusals_[indexOf(action)];
        logEditorActionLine(
            QStringLiteral("ACTION-REFUSED action=%1 reason='%2'").arg(editorActionName(action), result.reason));
        return result;
    }

    ++dispatches_[indexOf(action)];
    result.dispatched = true;
    logEditorActionLine(QStringLiteral("ACTION action=%1 dispatched=1 via='%2'")
                            .arg(editorActionName(action), editorActionCommand(action)));
    action_->trigger();
    return result;
}

int LspActionChannel::dispatchCount(EditorAction action) const
{
    return dispatches_.value(indexOf(action));
}

int LspActionChannel::refusalCount(EditorAction action) const
{
    return refusals_.value(indexOf(action));
}

ActionDispatch dispatchDefinitionAt(KTextEditor::View *view, const KTextEditor::Cursor &position)
{
    ActionDispatch result;
    if (view == nullptr || !position.isValid()) {
        result.reason = QStringLiteral("the click is not over a valid text position");
        return result;
    }
    LspActionChannel *channel = LspActionChannel::instanceFor(view);
    if (channel == nullptr) {
        result.reason = QStringLiteral("the LSP action channel is not installed on this view");
        return result;
    }

    // A plain click would focus this editor; Ctrl+click consumes the press, so
    // the focus is requested here. This gives the clicked view keyboard focus;
    // it does NOT by itself move the shell's EditorArea active pane (that is
    // updated by EditorPane's own filter, see the lane evidence for the bound).
    view->setFocus(Qt::MouseFocusReason);

    const ActionAvailability state = channel->availability(EditorAction::Definition);
    if (!state.enabled) {
        result.reason = state.reason.isEmpty()
                            ? QStringLiteral("the backend cannot go to definition")
                            : state.reason;
        return result;
    }

    // The request path reads the view's caret, so the clicked position must BE
    // the caret before the dispatch — this is the only thing Ctrl+click adds.
    // Install it as the SINGLE cursor position: Ctrl+click is a click, and the
    // consumed press suppresses the native click that would collapse a
    // multi-cursor set (setCursorPosition() would move only the primary and
    // leave the stale cursors alive).
    view->setCursorPositions({position});
    result = channel->dispatch(EditorAction::Definition);
    if (!result.dispatched && result.reason.isEmpty()) {
        result.reason = QStringLiteral("the definition action was not dispatched");
    }
    return result;
}

// --- F8 / Shift+F8 ---------------------------------------------------------

QList<DiagnosticAnnotation> orderedDiagnostics(const QList<DiagnosticAnnotation> &entries)
{
    QList<DiagnosticAnnotation> ordered;
    ordered.reserve(entries.size());
    for (const DiagnosticAnnotation &entry : entries) {
        if (entry.line >= 0) {
            ordered.append(entry);
        }
    }
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const DiagnosticAnnotation &left, const DiagnosticAnnotation &right) {
                         if (left.line != right.line) {
                             return left.line < right.line;
                         }
                         return std::max(0, left.column) < std::max(0, right.column);
                     });
    return ordered;
}

DiagnosticStep nextDiagnostic(const QList<DiagnosticAnnotation> &ordered, int line, int column)
{
    DiagnosticStep step;
    if (ordered.isEmpty()) {
        return step;
    }
    step.found = true;
    for (int index = 0; index < ordered.size(); ++index) {
        const DiagnosticAnnotation &entry = ordered.at(index);
        const int entryColumn = std::max(0, entry.column);
        if (entry.line > line || (entry.line == line && entryColumn > column)) {
            step.index = index;
            step.line = entry.line;
            step.column = entryColumn;
            return step;
        }
    }
    const DiagnosticAnnotation &first = ordered.first();
    step.index = 0;
    step.line = first.line;
    step.column = std::max(0, first.column);
    step.wrapped = true;
    return step;
}

DiagnosticStep previousDiagnostic(const QList<DiagnosticAnnotation> &ordered, int line, int column)
{
    DiagnosticStep step;
    if (ordered.isEmpty()) {
        return step;
    }
    step.found = true;
    for (int index = ordered.size() - 1; index >= 0; --index) {
        const DiagnosticAnnotation &entry = ordered.at(index);
        const int entryColumn = std::max(0, entry.column);
        if (entry.line < line || (entry.line == line && entryColumn < column)) {
            step.index = index;
            step.line = entry.line;
            step.column = entryColumn;
            return step;
        }
    }
    const DiagnosticAnnotation &last = ordered.last();
    step.index = ordered.size() - 1;
    step.line = last.line;
    step.column = std::max(0, last.column);
    step.wrapped = true;
    return step;
}

DiagnosticNavigator::DiagnosticNavigator(KTextEditor::View *view)
    : QObject(view)
    , view_(view)
{
    setObjectName(QString::fromLatin1(kNavigatorObjectName));
}

DiagnosticNavigator *DiagnosticNavigator::install(KTextEditor::View *view)
{
    if (view == nullptr) {
        return nullptr;
    }
    if (DiagnosticNavigator *existing = instanceFor(view)) {
        return existing;
    }
    auto *navigator = new DiagnosticNavigator(view);
    installEditorEventFilter(view, navigator);
    return navigator;
}

DiagnosticNavigator *DiagnosticNavigator::instanceFor(const KTextEditor::View *view)
{
    return view != nullptr
               ? view->findChild<DiagnosticNavigator *>(QString::fromLatin1(kNavigatorObjectName))
               : nullptr;
}

QList<DiagnosticAnnotation> DiagnosticNavigator::diagnostics() const
{
    return orderedDiagnostics(annotationsOf(view_));
}

DiagnosticStep DiagnosticNavigator::navigate(bool forward)
{
    const QList<DiagnosticAnnotation> entries = diagnostics();
    const QString file = viewFilePath(view_);
    if (entries.isEmpty()) {
        if (QMainWindow *window = windowOf(view_); window != nullptr && window->statusBar() != nullptr) {
            window->statusBar()->showMessage(
                QCoreApplication::translate("VEdit", "No diagnostics in this file"), 5000);
        }
        logEditorActionLine(QStringLiteral("DIAG-NAV action=%1 count=0 moved=0 file='%2'")
                                .arg(forward ? QStringLiteral("next") : QStringLiteral("previous"), file));
        DiagnosticStep step;
        return step;
    }

    const KTextEditor::Cursor cursor = view_->cursorPosition();
    const DiagnosticStep step = forward ? nextDiagnostic(entries, cursor.line(), cursor.column())
                                        : previousDiagnostic(entries, cursor.line(), cursor.column());
    const KTextEditor::Cursor target(step.line, step.column);
    view_->setCursorPosition(target);
    // A cursor set on an off-screen line must end up visible, otherwise F8
    // "works" without showing the user anything.
    const int firstVisible = view_->firstDisplayedLine(KTextEditor::View::RealLine);
    const int lastVisible = view_->lastDisplayedLine(KTextEditor::View::RealLine);
    if (step.line < firstVisible || step.line > lastVisible) {
        view_->setScrollPosition(KTextEditor::Cursor(std::max(0, step.line - 3), 0));
    }
    ++steps_;

    const DiagnosticAnnotation &entry = entries.at(step.index);
    if (QMainWindow *window = windowOf(view_); window != nullptr && window->statusBar() != nullptr) {
        window->statusBar()->showMessage(describeDiagnostic(entry, step.index, entries.size()), 6000);
    }
    logEditorActionLine(
        QStringLiteral("DIAG-NAV action=%1 count=%2 index=%3 from=%4:%5 to=%6:%7 wrapped=%8 "
                       "severity=%9 message='%10' file='%11'")
            .arg(forward ? QStringLiteral("next") : QStringLiteral("previous"))
            .arg(entries.size())
            .arg(step.index)
            .arg(cursor.line())
            .arg(cursor.column())
            .arg(step.line)
            .arg(step.column)
            .arg(step.wrapped ? 1 : 0)
            .arg(diagnosticsSeverityName(entry.severity), entry.message.left(160), file));
    return step;
}

bool DiagnosticNavigator::eventFilter(QObject *watched, QEvent *event)
{
    if (view_ == nullptr) {
        return QObject::eventFilter(watched, event);
    }
    if (event->type() == QEvent::ChildAdded && isEditorOwnedWidget(view_, watched)) {
        auto *child = static_cast<QChildEvent *>(event)->child();
        if (child != nullptr && child->isWidgetType()) {
            installEditorEventFilter(view_, this);
        }
        return QObject::eventFilter(watched, event);
    }
    if (!isEditorOwnedWidget(view_, watched)) {
        return QObject::eventFilter(watched, event);
    }
    switch (event->type()) {
    case QEvent::ShortcutOverride: {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_F8) {
            event->accept();
            return true;
        }
        break;
    }
    case QEvent::KeyPress: {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_F8) {
            navigate(!key->modifiers().testFlag(Qt::ShiftModifier));
            return true;
        }
        break;
    }
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}


QString ReferenceRow::location() const
{
    // 1-based, exactly like the jump contract and the editor's own line numbers.
    return QStringLiteral("%1:%2:%3").arg(filePath).arg(line + 1).arg(column + 1);
}

QList<ReferenceRow> buildReferenceRows(const QList<WorkspaceLocation> &locations)
{
    QList<ReferenceRow> rows;
    if (locations.isEmpty()) {
        return rows;
    }
    QList<WorkspaceLocation> sorted = locations;
    std::sort(sorted.begin(), sorted.end(),
              [](const WorkspaceLocation &left, const WorkspaceLocation &right) {
                  if (left.filePath != right.filePath) {
                      return left.filePath < right.filePath;
                  }
                  if (left.line != right.line) {
                      return left.line < right.line;
                  }
                  return left.column < right.column;
              });
    QSet<QString> seen;
    const QHash<QString, KTextEditor::Document *> documents = openDocumentsByPath();
    for (const WorkspaceLocation &location : sorted) {
        if (location.filePath.isEmpty() || location.line < 0) {
            continue;
        }
        const QString absolute = QFileInfo(location.filePath).absoluteFilePath();
        const QString key =
            QStringLiteral("%1:%2:%3").arg(absolute).arg(location.line).arg(location.column);
        if (seen.contains(key)) {
            continue;
        }
        seen.insert(key);
        ReferenceRow row;
        row.filePath = absolute;
        row.line = location.line;
        row.column = location.column;
        QString text = lineText(absolute, location.line, documents).trimmed();
        if (text.size() > kSnippetLength) {
            text = text.left(kSnippetLength) + QStringLiteral("…");
        }
        row.snippet = text;
        rows.append(row);
    }
    return rows;
}

int openDocumentCount()
{
    return openDocumentsByPath().size();
}

QString WorkspaceEditReport::summary() const
{
    return QCoreApplication::translate(
               "VEdit",
               "Rename applied: %1 edit(s) in %2 file(s) — %3 document(s) changed in the editor, "
               "%4 file(s) written on disk")
        .arg(editCount)
        .arg(fileCount)
        .arg(openDocumentCount)
        .arg(writtenFileCount);
}

WorkspaceEditReport applyWorkspaceEdit(const QList<WorkspaceTextEdit> &edits)
{
    WorkspaceEditReport report;
    if (edits.isEmpty()) {
        report.error = QCoreApplication::translate("VEdit", "the rename answer contained no edits");
        return report;
    }
    report.editCount = edits.size();

    // ---- phase 1: group, resolve and validate everything -------------------
    QHash<QString, FilePlan> plans;
    QStringList order;
    const QHash<QString, KTextEditor::Document *> documents = openDocumentsByPath();
    for (const WorkspaceTextEdit &edit : edits) {
        if (edit.filePath.isEmpty()) {
            report.error = QCoreApplication::translate("VEdit", "an edit has no target file");
            return report;
        }
        const QString absolute = QFileInfo(edit.filePath).absoluteFilePath();
        if (!plans.contains(absolute)) {
            FilePlan plan;
            plan.path = absolute;
            plan.document = documents.value(absolute, nullptr);
            if (plan.document == nullptr && !readTextFile(absolute, &plan.text, &report.error)) {
                return report;
            }
            plans.insert(absolute, plan);
            order.append(absolute);
        }
        FilePlan &plan = plans[absolute];
        QString rangeError;
        if (!validateAgainstDocument(edit, plan.document, absolute, &rangeError)) {
            report.error = QStringLiteral("%1: %2").arg(absolute, rangeError);
            return report;
        }
        plan.edits.append(edit);
    }

    // Later positions first, so applying one edit never invalidates the next.
    const auto descending = [](const WorkspaceTextEdit &left, const WorkspaceTextEdit &right) {
        if (left.startLine != right.startLine) {
            return left.startLine > right.startLine;
        }
        return left.startColumn > right.startColumn;
    };
    for (const QString &path : order) {
        QList<WorkspaceTextEdit> &list = plans[path].edits;
        std::sort(list.begin(), list.end(), descending);
    }

    // ---- phase 2: apply ----------------------------------------------------
    report.fileCount = order.size();
    for (const QString &path : order) {
        FilePlan &plan = plans[path];
        if (plan.document != nullptr) {
            // ONE editing transaction => the whole file's rename is ONE undo step.
            KTextEditor::Document::EditingTransaction transaction(plan.document);
            for (const WorkspaceTextEdit &edit : plan.edits) {
                const KTextEditor::Range range(edit.startLine, edit.startColumn, edit.endLine,
                                               edit.endColumn);
                if (!plan.document->replaceText(range, edit.newText)) {
                    report.error = QCoreApplication::translate(
                                       "VEdit", "KTextEditor refused to replace %1:%2")
                                       .arg(plan.path)
                                       .arg(edit.startLine + 1);
                    return report;
                }
            }
            ++report.openDocumentCount;
            report.perFile.append(QStringLiteral("%1: %2 edit(s) [editor]")
                                      .arg(plan.path)
                                      .arg(plan.edits.size()));
            continue;
        }
        QString textError;
        if (!applyEditsToText(&plan.text, plan.edits, &textError)) {
            report.error = QStringLiteral("%1: %2").arg(plan.path, textError);
            return report;
        }
        const AtomicSaveResult saved = atomicSaveText(plan.path, plan.text.toUtf8());
        if (!saved.ok) {
            report.error = QStringLiteral("%1: %2").arg(plan.path, saved.error);
            return report;
        }
        ++report.writtenFileCount;
        report.perFile.append(QStringLiteral("%1: %2 edit(s) [disk]")
                                  .arg(plan.path)
                                  .arg(plan.edits.size()));
    }

    report.ok = true;
    return report;
}

} // namespace vedit::editor
