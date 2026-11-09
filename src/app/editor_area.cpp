#include "editor_area.h"

#include "crash_reporter.h"

#include "editor_provider.h"
#include "path_display.h"
#include "product_theme.h"

#if defined(VEDIT_APP_HAVE_BREADCRUMB)
#include "breadcrumb_bar.h"
#endif

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QJsonValue>
#include <QKeySequence>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPointer>
#include <QScrollBar>
#include <QSet>
#include <QSizePolicy>
#include <QSplitter>
#include <QStackedWidget>
#include <QString>
#include <QTabBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTimer>
#include <QToolButton>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVBoxLayout>

#include <algorithm>
#include <vector>

namespace vedit::app {

namespace {

// UI-Mosaic T3: the product theme the application palette currently represents.
// The palette is the one place every product-theme switch converges (the shell
// and the Preferences combo both go through ThemeManager::applyUiTheme), so the
// tab strip can follow it without a second registration seam. `canvas` is unique
// per theme; before the first application the contract default is used.
const ProductTheme &currentProductTheme()
{
    const QColor window = QApplication::palette().color(QPalette::Window);
    for (const ProductTheme &theme : productThemes()) {
        if (theme.canvas.rgb() == window.rgb()) {
            return theme;
        }
    }
    return defaultProductTheme();
}

} // namespace

namespace {

// --- session-restore view state (T19) --------------------------------------
// Where the caret and the viewport were when the archive was written. The
// provider knows its own units (the KTextEditor view reports its first displayed
// REAL line, the plain-text fallback its scrollbar value); the archive is
// provider-gated, so those numbers are only replayed into the provider that wrote
// them.
SessionDocument captureViewState(EditorProvider *provider, QWidget *editor, const QString &path)
{
    SessionDocument document;
    document.path = path;
    if (provider == nullptr || editor == nullptr) {
        return document;
    }
    const EditorProvider::ViewState state = provider->viewState(editor);
    document.caretLine = state.caretLine;
    document.caretColumn = state.caretColumn;
    document.scrollTop = state.scrollTop;
    return document;
}

void applyViewState(EditorProvider *provider, QWidget *editor, const SessionDocument &state)
{
    if (provider == nullptr || editor == nullptr) {
        return;
    }
    EditorProvider::ViewState viewState;
    viewState.caretLine = state.caretLine;
    viewState.caretColumn = state.caretColumn;
    viewState.scrollTop = state.scrollTop;
    provider->setViewState(editor, viewState);
}

} // namespace

struct EditorArea::Node {
    enum class Kind { Leaf, Split };

    Kind kind = Kind::Leaf;
    EditorPane *pane = nullptr;
    Qt::Orientation orientation = Qt::Horizontal;
    double ratio = 0.5;
    std::unique_ptr<Node> first;
    std::unique_ptr<Node> second;
};

namespace {

// T12: the per-tab close affordance. It is a real child widget (not the style's
// style-drawn close button) so it can be named, found and screenshotted.
constexpr auto kTabCloseButtonName = "tab-close-button";

// Task T80 / RM-5b: an editor widget may bring its own standard shortcuts. The
// KTextEditor view owns KStandardAction copies bound to Ctrl+S (save),
// Ctrl+Shift+S (save as), Ctrl+Z (undo), Ctrl+Shift+Z (redo) and Ctrl+P (print);
// together with the shell's own command for the same key Qt reports "Ambiguous
// shortcut overload" and runs NEITHER of them — a dead key. RM-5b measured that
// this was silently true for save-as, split-horizontal (Ctrl+Shift+H is the
// editor's Find Selected Backwards) and the VCS panel (Ctrl+Alt+V is the
// editor's input-mode switch), which is exactly the "editing a shortcut does not
// take effect" report.
//
// The shell's command registry is the single authority: the pane is told which
// sequences the shell owns (`setOwnedShortcuts`) and clears them from the
// editor widget's own actions when the widget enters the pane. The KTextEditor
// actions are not in the view's own action list: they live in a parentless
// KActionCollection and are registered on the internal KateViewInternal child
// widget, so the whole editor subtree is walked. The actions stay alive and
// triggerable from the editor's own menus; only the duplicate key binding goes
// away. This runs for every editor widget — the first tab, every added tab, a
// provider swap and every split — because that is the same code path a document
// tab is created through.
int clearOwnedShortcuts(QWidget *editor, const QStringList &owned)
{
    if (editor == nullptr || owned.isEmpty()) {
        return 0;
    }
    int removed = 0;
    const auto clear = [&owned, &removed](QAction *action) {
        if (action == nullptr) {
            return;
        }
        QList<QKeySequence> kept;
        bool changed = false;
        for (const QKeySequence &sequence : action->shortcuts()) {
            if (owned.contains(sequence.toString(QKeySequence::PortableText))) {
                changed = true;
            } else {
                kept.append(sequence);
            }
        }
        if (changed) {
            action->setShortcuts(kept);
            ++removed;
        }
    };
    for (QAction *action : editor->actions()) {
        clear(action);
    }
    const QList<QWidget *> subtree = editor->findChildren<QWidget *>();
    for (QWidget *widget : subtree) {
        for (QAction *action : widget->actions()) {
            clear(action);
        }
    }
    const QList<QAction *> nested = editor->findChildren<QAction *>();
    for (QAction *action : nested) {
        clear(action);
    }
    return removed;
}

// UX batch 2 (item 7): the editor widget's OWN undo/redo action. KTextEditor
// registers its standard actions (KStandardAction names such as "edit_undo") on
// an internal KateViewInternal child widget, whose QAction list is the only
// place they are reachable from without including a KTextEditor header. The walk
// is the same one clearEditorSaveShortcut() already does for Ctrl+S.
QAction *findEditorAction(QWidget *editor, const QStringList &objectNames)
{
    if (editor == nullptr || objectNames.isEmpty()) {
        return nullptr;
    }
    const auto matches = [&objectNames](QAction *action) {
        return action != nullptr && objectNames.contains(action->objectName());
    };
    for (QAction *action : editor->actions()) {
        if (matches(action)) {
            return action;
        }
    }
    const QList<QWidget *> subtree = editor->findChildren<QWidget *>();
    for (QWidget *widget : subtree) {
        for (QAction *action : widget->actions()) {
            if (matches(action)) {
                return action;
            }
        }
    }
    for (QAction *action : editor->findChildren<QAction *>()) {
        if (matches(action)) {
            return action;
        }
    }
    return nullptr;
}

} // namespace

// ---------------------------------------------------------------------------
// EditorPane
// ---------------------------------------------------------------------------

// One open document: the provider's editor widget plus where it came from. The
// dirty flag is never cached here: it is read from the provider on every refresh
// so a document changed behind the pane's back (a jump target, a diagnostics
// fix, a save) can never leave a stale marker.
struct EditorPane::Document {
    QWidget *editor = nullptr;
    QString path;
};

EditorPane::EditorPane(QString paneId, EditorProvider *provider, QWidget *parent)
    : QWidget(parent)
    , paneId_(std::move(paneId))
    , provider_(provider)
{
    setObjectName(QStringLiteral("editor-pane"));
    setFocusPolicy(Qt::StrongFocus);

    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(0);

    // T12 row 0: the file-tab strip. It carries the file name, the dirty
    // marker, the active-tab highlight and the per-tab close button. There is
    // deliberately no provider label anywhere on it (AC-9.3).
    auto *tabRow = new QWidget(this);
    tabRow->setObjectName(QStringLiteral("editor-pane-tab-row"));
    auto *tabRowLayout = new QHBoxLayout(tabRow);
    tabRowLayout->setContentsMargins(0, 0, 0, 0);
    tabRowLayout->setSpacing(0);

    tabs_ = new QTabBar(tabRow);
    tabs_->setObjectName(QStringLiteral("editor-tab-bar"));
    tabs_->setDocumentMode(true);
    tabs_->setDrawBase(false);
    tabs_->setExpanding(false);
    tabs_->setElideMode(Qt::ElideMiddle);
    // AC-9.5: an overflowing strip scrolls inside this row instead of widening
    // the pane (a QTabBar's minimumSizeHint stays small with scroll buttons).
    tabs_->setUsesScrollButtons(true);
    tabs_->setMovable(false);
    tabs_->setFocusPolicy(Qt::NoFocus);
    tabs_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    tabs_->setSelectionBehaviorOnRemove(QTabBar::SelectRightTab);
    tabRowLayout->addWidget(tabs_, 1);

    // The one part of the old header worth keeping: which pane of a split holds
    // the keyboard focus. It names no provider.
    activeBadge_ = new QLabel(tabRow);
    activeBadge_->setObjectName(QStringLiteral("editor-pane-active-badge"));
    activeBadge_->setContentsMargins(6, 0, 6, 0);
    // The one word of the old pane header that is still shown: the badge that
    // marks the keyboard-focused pane. Translated in the same "VEdit" context as
    // the neighbouring strings; the zh_CN catalogue row is added by the i18n
    // lane in its final pass (single writer for translations/vedit_zh_CN.ts).
    activeBadge_->setText(QCoreApplication::translate("VEdit", "active"));
    activeBadge_->setVisible(false);
    tabRowLayout->addWidget(activeBadge_, 0);

    closeButton_ = new QToolButton(tabRow);
    closeButton_->setObjectName(QStringLiteral("pane-close-button"));
    closeButton_->setText(QStringLiteral("×"));
    closeButton_->setToolTip(QCoreApplication::translate("VEdit", "Close Pane"));
    closeButton_->setAutoRaise(true);
    closeButton_->setFixedSize(22, 20);
    closeButton_->setCursor(Qt::PointingHandCursor);
    tabRowLayout->addWidget(closeButton_, 0);
    QObject::connect(closeButton_, &QToolButton::clicked, this, [this]() {
        if (onCloseRequested_ != nullptr) {
            onCloseRequested_(this);
        }
    });
    layout_->addWidget(tabRow);

    // T94: the semantic breadcrumb bar under the tab strip. Empty (and hidden)
    // when the provider has no positional context.
    // T20/A-3: when the breadcrumb lane is present the pane mounts its real
    // clickable Bar instead of a QLabel - each segment asks the application-wide
    // jump registry for the container's line (AC-7.6). The lane is optional in
    // this build system, so a tree without it keeps the plain status line.
#if defined(VEDIT_APP_HAVE_BREADCRUMB)
    breadcrumbBar_ = new vedit::breadcrumb::Bar(this);
    layout_->addWidget(breadcrumbBar_);
#else
    breadcrumb_ = new QLabel(this);
    breadcrumb_->setObjectName(QStringLiteral("editor-pane-breadcrumb"));
    breadcrumb_->setContentsMargins(6, 0, 6, 2);
    breadcrumb_->setVisible(false);
    layout_->addWidget(breadcrumb_);
#endif

    stack_ = new QStackedWidget(this);
    stack_->setObjectName(QStringLiteral("editor-pane-stack"));
    layout_->addWidget(stack_, 1);

    QObject::connect(tabs_, &QTabBar::currentChanged, this, [this](int index) {
        Q_UNUSED(index);
        syncCurrentEditor();
        updateBreadcrumb();
        notifyStateChanged();
    });
    // Clicking a tab is also "use this pane": activate it and move the keyboard
    // focus into the document the user just picked.
    QObject::connect(tabs_, &QTabBar::tabBarClicked, this, [this](int index) {
        Q_UNUSED(index);
        if (onFocusRequested_ != nullptr) {
            onFocusRequested_(this);
        }
        requestFocus();
    });

    applyTabStyle();
    addTabForDocument(createEditorWidget());
    tabs_->setCurrentIndex(0);
    syncCurrentEditor();
    updateActiveBadge();
}

EditorPane::~EditorPane()
{
    // The provider is NOT touched here. It is owned by the shell and can already
    // be destroyed when the window tears its child widgets down (the provider is
    // a member of the shell, its panes are children of the central widget), so
    // unsubscribing from it here would be a use-after-free. The AREA hands the
    // live provider a chance to forget these widgets first, via
    // releaseListeners(); the widgets themselves die with this pane's stack.
    for (Document *document : documents_) {
        if (document != nullptr && document->editor != nullptr) {
            document->editor->removeEventFilter(this);
        }
        delete document;
    }
    documents_.clear();
}

void EditorPane::releaseListeners()
{
    for (Document *document : documents_) {
        if (document != nullptr) {
            unwireEditor(provider_, document->editor);
        }
    }
}

QList<SessionDocument> EditorPane::documentStates(int *activeTab) const
{
    if (activeTab != nullptr) {
        *activeTab = -1;
    }
    QList<SessionDocument> states;
    const int activeIndex = activeTabIndex();
    for (int index = 0; index < documents_.size(); ++index) {
        const Document *document = documentAt(index);
        if (document == nullptr || document->path.isEmpty() || document->editor == nullptr) {
            continue;
        }
        if (index == activeIndex && activeTab != nullptr) {
            *activeTab = states.size();
        }
        states.append(captureViewState(provider_, document->editor, document->path));
    }
    return states;
}

int EditorPane::restoreDocuments(const QList<SessionDocument> &documents, int activeTab,
                                 bool applyPositions, QStringList *diagnostics)
{
    int opened = 0;
    int activeIndex = -1;
    for (int index = 0; index < documents.size(); ++index) {
        const SessionDocument &state = documents.at(index);
        if (!state.hasPath()) {
            continue;
        }
        QString error;
        if (!openFile(state.path, &error)) {
            if (diagnostics != nullptr) {
                diagnostics->append(
                    QStringLiteral("session: cannot reopen %1: %2")
                        .arg(state.path,
                             error.isEmpty() ? QStringLiteral("unknown error") : error));
            }
            continue;
        }
        if (applyPositions) {
            Document *document = documentAt(tabIndexForPath(state.path));
            if (document != nullptr && document->editor != nullptr) {
                pendingPositions_.insert(document->editor, state);
                applyPendingPosition(document->editor);
            }
        }
        if (index == activeTab) {
            activeIndex = opened;
        }
        ++opened;
    }
    if (activeIndex < 0 && opened > 0) {
        // The archived active tab was an untitled one (not archived) or the
        // stored index is out of range: the last restored document is the
        // closest honest answer, and the pane must never end up with no active
        // tab.
        activeIndex = opened - 1;
    }
    if (activeIndex >= 0) {
        setActiveTab(activeIndex);
    }
    return opened;
}

QWidget *EditorPane::fallbackEditor()
{
    auto *fallback = new QPlainTextEdit();
    fallback->setObjectName(QStringLiteral("editor-pane-fallback"));
    fallback->setReadOnly(true);
    fallback->setPlainText(QCoreApplication::translate("VEdit", "No editor provider connected."));
    return fallback;
}

QWidget *EditorPane::createEditorWidget()
{
    QWidget *editor = provider_ != nullptr ? provider_->createEditor(this) : nullptr;
    if (editor == nullptr) {
        editor = fallbackEditor();
    }
    return editor;
}

void EditorPane::addTabForDocument(QWidget *editor, const QString &absolutePath)
{
    if (editor == nullptr) {
        return;
    }
    auto *document = new Document;
    document->editor = editor;
    document->path = absolutePath;
    documents_.append(document);
    stack_->addWidget(editor);
    wireEditor(editor);

    const int index = tabs_->addTab(QString());
    auto *close = new QToolButton(tabs_);
    close->setObjectName(QString::fromLatin1(kTabCloseButtonName));
    close->setText(QStringLiteral("×"));
    close->setToolTip(QCoreApplication::translate("VEdit", "Close File"));
    close->setAutoRaise(true);
    close->setFixedSize(16, 16);
    close->setCursor(Qt::PointingHandCursor);
    // The button belongs to this document, not to a tab index (indices move);
    // the close is deferred because removing the tab destroys this very button,
    // which must never happen inside its own clicked() emission.
    const QPointer<QWidget> guardedEditor(editor);
    QObject::connect(close, &QToolButton::clicked, this, [this, guardedEditor]() {
        if (guardedEditor.isNull()) {
            return;
        }
        QTimer::singleShot(0, this, [this, guardedEditor]() {
            const QWidget *editor = guardedEditor.data();
            if (editor == nullptr) {
                return;
            }
            for (int i = 0; i < documents_.size(); ++i) {
                if (documents_.at(i) != nullptr && documents_.at(i)->editor == editor) {
                    closeTab(i);
                    return;
                }
            }
        });
    });
    tabs_->setTabButton(index, QTabBar::RightSide, close);
    refreshTab(index);
}

void EditorPane::removeTab(int index)
{
    Document *document = documentAt(index);
    if (document == nullptr) {
        return;
    }
    documents_.removeAt(index);
    QWidget *editor = document->editor;
    if (editor != nullptr) {
        pendingPositions_.remove(editor);
        unwireEditor(provider_, editor);
        const int stackIndex = stack_->indexOf(editor);
        if (stackIndex >= 0) {
            stack_->removeWidget(editor);
        }
        editor->setParent(nullptr);
        delete editor;
    }
    delete document;
    // The tab's close button is owned by the tab bar and dies with the tab.
    tabs_->removeTab(index);
}

void EditorPane::replaceTabEditor(int index, QWidget *editor)
{
    Document *document = documentAt(index);
    if (document == nullptr || editor == nullptr) {
        return;
    }
    document->editor = editor;
    stack_->addWidget(editor);
    wireEditor(editor);
    refreshTab(index);
}

void EditorPane::wireEditor(QWidget *editor)
{
    if (editor == nullptr) {
        return;
    }
    editor->installEventFilter(this);
    // T80/RM-5b: the shell's command registry is the only authority for the
    // sequences it binds; remove the editor's own copies before a key press.
    clearOwnedShortcuts(editor, ownedShortcuts_);
    // KTextEditor populates its standard actions lazily (its KActionCollection
    // has no QObject parent, so it cannot be found from the widget tree
    // afterwards), so re-apply once the current event-loop turn is over.
    const QPointer<QWidget> guarded(editor);
    const QStringList owned = ownedShortcuts_;
    QTimer::singleShot(0, this, [guarded, owned]() {
        if (guarded != nullptr) {
            clearOwnedShortcuts(guarded.data(), owned);
        }
    });
    if (provider_ == nullptr) {
        return;
    }
    // T94: the provider pushes breadcrumb changes for THIS tab's document; the
    // pane repaints the bar (empty while the provider has no path).
    provider_->setStatusListener(editor, [this]() { updateBreadcrumb(); });
    // T12: the dirty marker follows the document's own modified flag, so the tab
    // label, the window title and the status bar all clear from ONE event.
    provider_->setModifiedListener(editor, [this, editor](bool modified) {
        Q_UNUSED(modified);
        for (int i = 0; i < documents_.size(); ++i) {
            if (documents_.at(i) != nullptr && documents_.at(i)->editor == editor) {
                refreshTab(i);
                break;
            }
        }
        notifyStateChanged();
    });
}

void EditorPane::unwireEditor(EditorProvider *provider, QWidget *editor)
{
    if (editor == nullptr) {
        return;
    }
    editor->removeEventFilter(this);
    if (provider != nullptr) {
        provider->setStatusListener(editor, nullptr);
        provider->setModifiedListener(editor, nullptr);
    }
}

void EditorPane::syncCurrentEditor()
{
    Document *document = documentAt(tabs_->currentIndex());
    editor_ = document != nullptr ? document->editor : nullptr;
    if (document != nullptr && stack_->currentWidget() != document->editor) {
        stack_->setCurrentWidget(document->editor);
    }
}

void EditorPane::applyPendingPosition(QWidget *editor)
{
    if (editor == nullptr || pendingPositions_.isEmpty()) {
        return;
    }
    const auto entry = pendingPositions_.constFind(editor);
    if (entry == pendingPositions_.constEnd()) {
        return;
    }
    if (!editor->isVisible()) {
        // The page is not the visible tab yet: keep the entry and let the Show
        // event (next activation) apply it.
        return;
    }
    const SessionDocument state = entry.value();
    pendingPositions_.remove(editor);
    applyViewState(provider_, editor, state);
}

QString EditorPane::documentLabel(const Document &document) const
{
    if (document.path.isEmpty()) {
        return QStringLiteral("untitled");
    }
    const QString name = QFileInfo(document.path).fileName();
    return name.isEmpty() ? document.path : name;
}

void EditorPane::refreshTab(int index)
{
    const Document *document = documentAt(index);
    if (document == nullptr) {
        return;
    }
    const bool dirty = provider_ != nullptr && document->editor != nullptr
                       && provider_->isModified(document->editor);
    // AC-8.1: the dirty marker is part of the TAB LABEL ("● name"), so it is
    // visible without hovering and never depends on the tab's colours. AC-8.2:
    // a save clears the document flag, the listener fires and the label is
    // rebuilt without the marker.
    const QString label =
        dirty ? QStringLiteral("● %1").arg(documentLabel(*document)) : documentLabel(*document);
    if (tabs_->tabText(index) != label) {
        tabs_->setTabText(index, label);
    }
    // FIX 1: the tooltip shows the path with the product's ONE display rule
    // (project-relative when the document is inside the project root, absolute
    // otherwise). The tab TEXT stays the file name: it is a name, not a path,
    // and the open key stays absolute.
    tabs_->setTabToolTip(index, document->path.isEmpty()
                                    ? QStringLiteral("untitled")
                                    : vedit::paths::displayPath(displayRoot_, document->path));
}

void EditorPane::refreshAllTabs()
{
    for (int index = 0; index < documents_.size(); ++index) {
        refreshTab(index);
    }
}

void EditorPane::applyTabStyle()
{
    if (tabs_ == nullptr || applyingTabStyle_) {
        return;
    }
    // setStyleSheet() sends a StyleChange event; changeEvent() calls back in
    // here. The flag breaks that cycle, the cached string keeps a repaint that
    // changes nothing from touching the style system at all.
    applyingTabStyle_ = true;
    // AC-9.2 + UI-Mosaic T3: the active tab is unmistakable and both states come
    // from the PRODUCT theme tokens instead of palette blends (contract §2.3):
    // 8px pill radius, the selected pill is surface + accent text + 1px border,
    // the unselected pill stays transparent, hover uses surfaceAlt.
    const ProductTheme &theme = currentProductTheme();
    const QString sheet = QStringLiteral(
                             "QTabBar { background: transparent; }"
                             "QTabBar::tab { background: transparent; color: %1;"
                             " padding: 4px 10px; margin: 3px 4px 0px 0px;"
                             " border: 1px solid transparent; border-radius: 8px; }"
                             "QTabBar::tab:selected { background: %2; color: %3;"
                             " border: 1px solid %4; font-weight: 600; }"
                             "QTabBar::tab:hover:!selected { background: %5; }"
                             "QToolButton { border: none; background: transparent; color: %1; }")
                             .arg(theme.textMuted.name(), theme.surface.name(),
                                  theme.accent.name(), theme.border.name(),
                                  theme.surfaceAlt.name());
    if (sheet != tabStyleSheet_) {
        tabStyleSheet_ = sheet;
        tabs_->setStyleSheet(sheet);
    }
    if (activeBadge_ != nullptr) {
        // The accent colour is also the selected pill's text colour, so the badge
        // ties to it and stays legible on every product theme.
        const QString badgeSheet =
            QStringLiteral("color: %1; font-weight: 600;").arg(theme.accent.name());
        if (activeBadge_->styleSheet() != badgeSheet) {
            activeBadge_->setStyleSheet(badgeSheet);
        }
    }
    applyingTabStyle_ = false;
}

void EditorPane::notifyStateChanged()
{
    if (onStateChanged_ != nullptr) {
        onStateChanged_();
    }
}

void EditorPane::updateActiveBadge()
{
    if (activeBadge_ == nullptr) {
        return;
    }
    activeBadge_->setVisible(active_);
}

QString EditorPane::breadcrumbText() const
{
#if defined(VEDIT_APP_HAVE_BREADCRUMB)
    if (breadcrumbBar_ != nullptr) {
        QStringList parts;
        for (int index = 0; index < breadcrumbBar_->segmentCount(); ++index) {
            parts.append(breadcrumbBar_->segmentText(index));
        }
        return parts.join(QStringLiteral(" > "));
    }
#endif
    return breadcrumb_ != nullptr ? breadcrumb_->text() : QString();
}

void EditorPane::updateBreadcrumb()
{
#if defined(VEDIT_APP_HAVE_BREADCRUMB)
    // The semantic path comes from the editor lane through the view's
    // "veditBreadcrumbEntries" property (fold_breadcrumb.h): the shell never
    // includes a KTextEditor header. The bar hides itself while the path (or the
    // file path) is empty, exactly like the QLabel it replaced.
    if (breadcrumbBar_ != nullptr) {
        const QString file =
            provider_ != nullptr && editor_ != nullptr ? provider_->currentFilePath(editor_)
                                                       : QString();
        std::vector<vedit::breadcrumb::Entry> entries;
        if (editor_ != nullptr) {
            const QVariantList published =
                editor_->property("veditBreadcrumbEntries").toList();
            entries.reserve(static_cast<std::size_t>(published.size()));
            for (const QVariant &value : published) {
                const QVariantMap item = value.toMap();
                vedit::breadcrumb::Entry entry;
                entry.kind = item.value(QStringLiteral("kind")).toString().toStdString();
                entry.name = item.value(QStringLiteral("name")).toString().toStdString();
                entry.line = item.value(QStringLiteral("line")).toInt();
                entries.push_back(std::move(entry));
            }
        }
        breadcrumbBar_->setPath(file, entries);
        return;
    }
#endif
    if (breadcrumb_ == nullptr) {
        return;
    }
    const QString text = provider_ != nullptr ? provider_->statusText(editor_) : QString();
    breadcrumb_->setText(text);
    breadcrumb_->setVisible(!text.isEmpty());
}

void EditorPane::requestFocus()
{
    if (editor_ != nullptr) {
        editor_->setFocus(Qt::OtherFocusReason);
    }
}

void EditorPane::setProvider(EditorProvider *provider)
{
    if (provider_ == provider) {
        return;
    }
    EditorProvider *previous = provider_;
    provider_ = provider;
    // The active-editor pointer must never name a widget that is about to be
    // replaced: the swap below deletes and recreates every editor, and loading a
    // document into the NEW provider fires its modified listener, which reaches
    // the shell's Undo/Redo sync — that reads editorWidget(). Clearing it makes
    // the transient state read as "no active view" instead of dereferencing a
    // widget that has just been deleted; syncCurrentEditor() restores it below.
    editor_ = nullptr;
    // T19: every editor widget is recreated below, so an archived position bound
    // to one of the old widgets can never be applied (and must not outlive it).
    pendingPositions_.clear();
    // Every tab keeps its place and its path; only the widget behind it is
    // recreated by the new provider. A document that was already open is
    // re-read from disk (the shell installs the real provider before the user
    // can open a file, so this is the demo placeholder being replaced).
    for (int index = 0; index < documents_.size(); ++index) {
        Document *document = documents_.at(index);
        if (document == nullptr) {
            continue;
        }
        QWidget *stale = document->editor;
        if (stale != nullptr) {
            unwireEditor(previous, stale);
            const int stackIndex = stack_->indexOf(stale);
            if (stackIndex >= 0) {
                stack_->removeWidget(stale);
            }
            stale->setParent(nullptr);
            delete stale;
            document->editor = nullptr;
        }
        QWidget *fresh = createEditorWidget();
        document->editor = fresh;
        stack_->addWidget(fresh);
        wireEditor(fresh);
        if (!document->path.isEmpty() && provider_ != nullptr) {
            QString error;
            if (!provider_->openFile(fresh, document->path, &error) && !error.isEmpty()) {
                // The tab stays visible with its path: a later save reports the
                // real reason instead of the pane silently dropping the file.
                vedit::app::crash::event("provider swap open failed: %s", qPrintable(error));
            }
        }
        refreshTab(index);
    }
    syncCurrentEditor();
    updateBreadcrumb();
    notifyStateChanged();
}

EditorProvider *EditorPane::provider() const
{
    return provider_;
}

int EditorPane::enforceSingleCtrlSAuthority()
{
    int removed = 0;
    for (const Document *document : documents_) {
        if (document != nullptr) {
            removed += clearOwnedShortcuts(document->editor, ownedShortcuts_);
        }
    }
    return removed;
}

void EditorPane::setOwnedShortcuts(const QStringList &portableSequences)
{
    ownedShortcuts_ = portableSequences;
    ownedShortcuts_.removeDuplicates();
    ownedShortcuts_.sort();
    enforceSingleCtrlSAuthority();
}

QStringList EditorPane::ownedShortcuts() const
{
    return ownedShortcuts_;
}

void EditorPane::newDocument()
{
    if (provider_ == nullptr) {
        return;
    }
    // Reuse the untouched empty tab: the first New must not leave a second blank
    // tab behind (the same rule openFile() applies for its first document).
    if (const Document *current = documentAt(tabs_->currentIndex())) {
        if (current->path.isEmpty() && !provider_->isModified(current->editor)) {
            setActiveTab(tabs_->currentIndex());
            requestFocus();
            return;
        }
    }
    addTabForDocument(createEditorWidget());
    tabs_->setCurrentIndex(documents_.size() - 1);
    syncCurrentEditor();
    updateBreadcrumb();
    notifyStateChanged();
    requestFocus();
}

bool EditorPane::triggerEditorAction(const QStringList &objectNames,
                                     const QString &fallbackMethod)
{
    QWidget *editor = editorWidget();
    if (editor == nullptr) {
        return false;
    }
    if (QAction *action = findEditorAction(editor, objectNames)) {
        if (!action->isEnabled()) {
            return false;
        }
        action->trigger();
        return true;
    }
    // A provider without a named action (the plain-text placeholder): invoke its
    // own slot, exactly the operation its context menu exposes.
    if (!fallbackMethod.isEmpty()) {
        const QByteArray signature = fallbackMethod.toUtf8() + "()";
        if (editor->metaObject()->indexOfMethod(signature.constData()) >= 0) {
            return QMetaObject::invokeMethod(editor, fallbackMethod.toUtf8().constData(),
                                             Qt::DirectConnection);
        }
    }
    return false;
}

bool EditorPane::editorActionEnabled(const QStringList &objectNames) const
{
    QWidget *editor = editorWidget();
    if (editor == nullptr) {
        return false;
    }
    if (QAction *action = findEditorAction(editor, objectNames)) {
        return action->isEnabled();
    }
    // No named action: the shell still wires the editor's own method, so the
    // command is greyed only while there is no active view at all.
    return true;
}

bool EditorPane::openFile(const QString &absolutePath, QString *error)
{    if (provider_ == nullptr) {
        if (error != nullptr) {
            *error = QCoreApplication::translate("VEdit", "open: the pane has no editor provider");
        }
        return false;
    }
    const QFileInfo info(absolutePath);
    const QString absolute = info.absoluteFilePath();
    // AC-9.x: opening a file that is already open in this pane focuses its tab
    // instead of creating a duplicate.
    const int existing = tabIndexForPath(absolute);
    if (existing >= 0) {
        setActiveTab(existing);
        // FIX 2: "the document is open now" is the event the project lane
        // consumes (a `.f` opened again must still be imported if it was not).
        // Its handler is idempotent, so focusing an already-open tab may fire
        // the signal without any user-visible duplicate.
        if (onFileOpened_ != nullptr) {
            onFileOpened_(absolute);
        }
        return true;
    }
    int target = -1;
    bool created = false;
    if (const Document *current = documentAt(tabs_->currentIndex())) {
        // Reuse the untouched "untitled" tab: the first File > Open must not
        // leave an empty tab next to the file it opened.
        if (current->path.isEmpty() && !provider_->isModified(current->editor)) {
            target = tabs_->currentIndex();
        }
    }
    if (target < 0) {
        addTabForDocument(createEditorWidget());
        target = documents_.size() - 1;
        created = true;
    }
    Document *document = documentAt(target);
    if (document == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("open: the pane could not create a tab");
        }
        return false;
    }
    if (!provider_->openFile(document->editor, absolute, error)) {
        if (created) {
            removeTab(target);
            if (documents_.isEmpty()) {
                addTabForDocument(createEditorWidget());
                tabs_->setCurrentIndex(0);
            }
            syncCurrentEditor();
            notifyStateChanged();
        }
        return false;
    }
    document->path = absolute;
    refreshTab(target);
    setActiveTab(target);
    syncCurrentEditor();
    updateBreadcrumb();
    notifyStateChanged();
    // T117 crash report context: the file a crash happened on.
    vedit::app::crash::setCurrentFile(absolute);
    vedit::app::crash::event("open %s", qPrintable(absolute));
    // FIX 2: the ONE notification every open surface reaches (see the header).
    // Emitted last, so a receiver already reads the new active tab and path.
    if (onFileOpened_ != nullptr) {
        onFileOpened_(absolute);
    }
    return true;
}

QString EditorPane::currentFilePath() const
{
    const Document *document = documentAt(tabs_ != nullptr ? tabs_->currentIndex() : -1);
    if (document != nullptr && !document->path.isEmpty()) {
        return document->path;
    }
    return provider_ != nullptr ? provider_->currentFilePath(editor_) : QString();
}

bool EditorPane::saveFile(QString *error)
{
    return saveTab(activeTabIndex(), error);
}

bool EditorPane::saveTab(int index, QString *error)
{
    Document *document = documentAt(index);
    if (document == nullptr || provider_ == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("save: the pane holds no such document");
        }
        return false;
    }
    if (!provider_->saveFile(document->editor, error)) {
        return false;
    }
    refreshTab(index);
    notifyStateChanged();
    // R3: the bytes are on disk and the tab is refreshed - only now may the
    // save-triggered project lint hear about it (it must never delay the save).
    if (onDocumentSaved_ != nullptr) {
        onDocumentSaved_(document->path, false);
    }
    return true;
}

bool EditorPane::saveAsFile(const QString &absolutePath, QString *error)
{
    Document *document = documentAt(activeTabIndex());
    if (document == nullptr || document->editor == nullptr || provider_ == nullptr) {
        if (error != nullptr) {
            *error = QCoreApplication::translate("VEdit", "Save As: the pane has no active document");
        }
        return false;
    }
    const QFileInfo target(absolutePath);
    if (!target.isAbsolute() || target.fileName().isEmpty()) {
        if (error != nullptr) {
            *error = QCoreApplication::translate("VEdit", "Save As: %1 is not an absolute file path")
                         .arg(absolutePath);
        }
        return false;
    }

    // The path a document is saved to is the one property both providers resolve
    // through (editor_provider.h's currentFilePath, which the edit-core provider
    // consults before the KTextEditor document URL). Pointing it at the target and
    // then using the provider's own saveFile() keeps ONE write path: the demo
    // provider writes its QPlainTextEdit with QSaveFile, the edit-core provider
    // writes the KTextEditor buffer with the kernel's atomic save, and both clear
    // the modified flag themselves (AC-1.4: the tab marker must clear).
    const QString previousPath = provider_->currentFilePath(document->editor);
    document->editor->setProperty("vedit.openFilePath", target.absoluteFilePath());
    QString saveError;
    if (!provider_->saveFile(document->editor, &saveError)) {
        // A refused save must not leave the tab claiming a file that was never
        // written: restore the path the document had (empty for an untitled tab).
        document->editor->setProperty("vedit.openFilePath",
                                      previousPath.isEmpty() ? QVariant() : QVariant(previousPath));
        if (error != nullptr) {
            *error = saveError.isEmpty()
                         ? QCoreApplication::translate("VEdit", "Save As: %1 could not be written")
                               .arg(target.absoluteFilePath())
                         : saveError;
        }
        return false;
    }

    document->path = target.absoluteFilePath();
    refreshTab(activeTabIndex());
    notifyStateChanged();
    // R3: Save As is a successful write too (and the tab just adopted a path).
    if (onDocumentSaved_ != nullptr) {
        onDocumentSaved_(document->path, true);
    }
    return true;
}

int EditorPane::tabCount() const
{
    return documents_.size();
}

int EditorPane::activeTabIndex() const
{
    return tabs_ != nullptr ? tabs_->currentIndex() : -1;
}

void EditorPane::setActiveTab(int index)
{
    if (index < 0 || index >= documents_.size()) {
        return;
    }
    if (tabs_->currentIndex() != index) {
        tabs_->setCurrentIndex(index);
    }
    syncCurrentEditor();
    updateBreadcrumb();
    notifyStateChanged();
}

QString EditorPane::tabFilePath(int index) const
{
    const Document *document = documentAt(index);
    return document != nullptr ? document->path : QString();
}

QWidget *EditorPane::tabEditorWidget(int index) const
{
    const Document *document = documentAt(index);
    return document != nullptr ? document->editor : nullptr;
}

QStringList EditorPane::openFilePaths() const
{
    QStringList paths;
    for (const Document *document : documents_) {
        if (document != nullptr && !document->path.isEmpty()) {
            paths.append(document->path);
        }
    }
    return paths;
}

QString EditorPane::tabText(int index) const
{
    if (tabs_ == nullptr || index < 0 || index >= tabs_->count()) {
        return QString();
    }
    return tabs_->tabText(index);
}

bool EditorPane::isTabModified(int index) const
{
    const Document *document = documentAt(index);
    return document != nullptr && provider_ != nullptr && document->editor != nullptr
           && provider_->isModified(document->editor);
}

int EditorPane::tabIndexForPath(const QString &absolutePath) const
{
    if (absolutePath.isEmpty()) {
        return -1;
    }
    const QString absolute = QFileInfo(absolutePath).absoluteFilePath();
    for (int index = 0; index < documents_.size(); ++index) {
        if (documents_.at(index) != nullptr && documents_.at(index)->path == absolute) {
            return index;
        }
    }
    return -1;
}

bool EditorPane::closeTab(int index)
{
    Document *document = documentAt(index);
    if (document == nullptr) {
        return false;
    }
    if (isTabModified(index)) {
        // AC-8.3: a dirty document is never discarded by a bare close. Without
        // an installed prompt the close is refused outright.
        if (!onTabCloseRequested_) {
            return false;
        }
        const SaveDecision decision = onTabCloseRequested_(this, index);
        if (decision == SaveDecision::Cancel) {
            return false;
        }
        if (decision == SaveDecision::Save) {
            QString error;
            if (!saveTab(index, &error)) {
                return false;
            }
        }
    }
    removeTab(index);
    if (documents_.isEmpty()) {
        // A pane always shows one tab; the last one closing leaves a fresh
        // empty document instead of an empty strip.
        addTabForDocument(createEditorWidget());
        tabs_->setCurrentIndex(0);
    } else {
        // AC-9.4: the neighbouring tab becomes active.
        tabs_->setCurrentIndex(std::clamp(index, 0, tabCount() - 1));
    }
    syncCurrentEditor();
    updateBreadcrumb();
    notifyStateChanged();
    return true;
}

QTabBar *EditorPane::tabBar() const
{
    return tabs_;
}

void EditorPane::setOnTabCloseRequested(std::function<SaveDecision(EditorPane *, int)> callback)
{
    onTabCloseRequested_ = std::move(callback);
}

void EditorPane::setOnStateChanged(std::function<void()> callback)
{
    onStateChanged_ = std::move(callback);
}

void EditorPane::setOnFileOpened(std::function<void(const QString &)> callback)
{
    onFileOpened_ = std::move(callback);
}

void EditorPane::setOnDocumentSaved(std::function<void(const QString &, bool)> callback)
{
    onDocumentSaved_ = std::move(callback);
}

void EditorPane::setDisplayRoot(const QString &root)
{
    if (displayRoot_ == root) {
        return;
    }
    displayRoot_ = root;
    // A root switch changes what every open tab renders in its tooltip.
    refreshAllTabs();
}

QString EditorPane::paneId() const
{
    return paneId_;
}

QWidget *EditorPane::editorWidget() const
{
    return editor_;
}

bool EditorPane::containsEditorWidget(const QWidget *widget) const
{
    if (widget == nullptr) {
        return false;
    }
    // Every tab is checked, not only the active one: a focus event can name a
    // hidden tab's view while a tab switch is in flight.
    for (const Document *document : documents_) {
        QWidget *editor = document != nullptr ? document->editor : nullptr;
        if (editor != nullptr && (editor == widget || editor->isAncestorOf(widget))) {
            return true;
        }
    }
    return false;
}

void EditorPane::setActive(bool active)
{
    if (active_ == active) {
        return;
    }
    active_ = active;
    updateActiveBadge();
}

bool EditorPane::isActive() const
{
    return active_;
}

void EditorPane::setOnFocusRequested(std::function<void(EditorPane *)> callback)
{
    onFocusRequested_ = std::move(callback);
}

void EditorPane::setOnCloseRequested(std::function<void(EditorPane *)> callback)
{
    onCloseRequested_ = std::move(callback);
}

bool EditorPane::isModified() const
{
    for (int index = 0; index < documents_.size(); ++index) {
        if (isTabModified(index)) {
            return true;
        }
    }
    return false;
}

bool EditorPane::isActiveTabModified() const
{
    return isTabModified(activeTabIndex());
}

EditorPane::Document *EditorPane::documentAt(int index)
{
    if (index < 0 || index >= documents_.size()) {
        return nullptr;
    }
    return documents_.at(index);
}

const EditorPane::Document *EditorPane::documentAt(int index) const
{
    if (index < 0 || index >= documents_.size()) {
        return nullptr;
    }
    return documents_.at(index);
}

void EditorPane::mousePressEvent(QMouseEvent *event)
{
    if (onFocusRequested_ != nullptr) {
        onFocusRequested_(this);
    }
    QWidget::mousePressEvent(event);
}

bool EditorPane::eventFilter(QObject *watched, QEvent *event)
{
    bool watchedIsDocument = false;
    for (const Document *document : documents_) {
        if (document != nullptr && document->editor == watched) {
            watchedIsDocument = true;
            break;
        }
    }
    if (watchedIsDocument && event != nullptr
        && (event->type() == QEvent::Show || event->type() == QEvent::FocusIn)) {
        // T80/RM-5b: providers may populate their standard actions lazily,
        // after createEditor() returned. Re-applying the invariant when the
        // widget is shown or focused guarantees it holds before a key press.
        clearOwnedShortcuts(qobject_cast<QWidget *>(watched), ownedShortcuts_);
    }
    if (watchedIsDocument && event != nullptr && event->type() == QEvent::Show) {
        // T19: this tab just became the visible one; now its viewport has a real
        // geometry, so the archived caret/scroll can be applied without clamping.
        applyPendingPosition(qobject_cast<QWidget *>(watched));
    }
    if (event != nullptr
        && (event->type() == QEvent::FocusIn || event->type() == QEvent::MouseButtonPress)
        && onFocusRequested_ != nullptr) {
        onFocusRequested_(this);
    }
    return false;
}

void EditorPane::changeEvent(QEvent *event)
{
    if (event != nullptr
        && (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange)) {
        // The shell applies a theme by changing the application palette; the
        // tab strip has to follow it or the active tab stops standing out.
        applyTabStyle();
    }
    QWidget::changeEvent(event);
}

// ---------------------------------------------------------------------------
// EditorArea
// ---------------------------------------------------------------------------

EditorArea::EditorArea(QWidget *parent)
    : QWidget(parent)
{
    // UI-Mosaic T3: the editor area is a card surface. The chrome shell wraps it
    // in the QFrame that carries this objectName (cards.qss selects it), and the
    // name is set here too so the container is identifiable from inside the lane.
    setObjectName(QStringLiteral("veditCard"));
    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(0);

    // T90/F-2: the T80 invariant is re-applied at pane creation, show, focus and
    // the next event-loop turn. A runtime re-application of editor/standard
    // shortcuts (KDE system settings applying KStandardShortcut, a provider
    // repopulating its action collection) can still put a second Ctrl+S back.
    // Two triggers cover that without polling the shortcut map: a window/application
    // activation (the typical moment a settings-driven re-apply becomes visible)
    // and a low-frequency timer as the safety net. Both call the SAME enforcement
    // entry point, and the selfcheck asserts "enumerate >1 -> cleared".
    shortcutGuard_ = new QTimer(this);
    shortcutGuard_->setObjectName(QStringLiteral("ctrlSAuthorityGuard"));
    shortcutGuard_->setInterval(2000);
    connect(shortcutGuard_, &QTimer::timeout, this,
            [this]() { enforceSingleCtrlSAuthority(); });
    shortcutGuard_->start();
    if (QCoreApplication::instance() != nullptr) {
        QCoreApplication::instance()->installEventFilter(this);
    }

    // UI-Mosaic V1: 编辑器卡片和面板卡片用同一条柔和投影（同一组参数）。QFrame#veditCard
    // 由外壳在构造完本控件之后创建并把本控件重挂进去，所以投影放到下一轮
    // 事件循环再装（此时父链已定）。父控件不是卡片（例如只单独构造编辑区的
    // 测试）就什么都不做。
    QTimer::singleShot(0, this, [this]() {
        auto *card = qobject_cast<QFrame *>(parentWidget());
        if (card == nullptr || card->objectName() != QStringLiteral("veditCard")
            || card->graphicsEffect() != nullptr) {
            return;
        }
        // 参数与 panel_registry.cpp 的 makeCardShadow() 保持一致：同一套卡片
        // 只有一条阴影语言（V5：blur 12 浅色 / 14 深色，offset 0,1，浅色 α22 / 深色 α220）。
        const bool light = QApplication::palette().color(QPalette::Window).lightness() > 128;
        auto *shadow = new QGraphicsDropShadowEffect(card);
        shadow->setBlurRadius(light ? 12 : 14);
        shadow->setOffset(0, 1);
        shadow->setColor(QColor(0, 0, 0, light ? 22 : 220));
        card->setGraphicsEffect(shadow);
    });
}

EditorArea::~EditorArea()
{
    if (shortcutGuard_ != nullptr) {
        shortcutGuard_->stop();
    }
    if (QCoreApplication::instance() != nullptr) {
        QCoreApplication::instance()->removeEventFilter(this);
    }
    delete content_;
    content_ = nullptr;
    root_.reset();
}

bool EditorArea::eventFilter(QObject *watched, QEvent *event)
{
    if (event == nullptr) {
        return false;
    }
    if (event->type() == QEvent::ApplicationActivate
        || event->type() == QEvent::WindowActivate) {
        enforceSingleCtrlSAuthority();
    } else if (event->type() == QEvent::MouseButtonPress
               || event->type() == QEvent::FocusIn) {
        // UX batch 2: a press or focus-in anywhere inside a pane's editor makes
        // that pane the ACTIVE one, so commands, the status bar and the
        // definition/rename/toolbar paths that read activePane() follow the pane
        // the user is working in. The filter has to run at the APPLICATION level:
        // the real mouse target of the product editor is KatePart's inner
        // KateViewInternal widget, a CHILD of the KTextEditor::View, and a filter
        // installed on the view never sees those events (evidence:
        // evidence/ux-batch-2/review-b4/01-focus-delivery.txt).
        activatePaneContaining(qobject_cast<QWidget *>(watched));
    }
    return false;
}

void EditorArea::activatePaneContaining(const QWidget *widget)
{
    if (widget == nullptr || rebuilding_) {
        // A re-layout reparents the panes; the focus events it produces are not
        // the user choosing a pane.
        return;
    }
    if (active_ != nullptr && active_->containsEditorWidget(widget)) {
        return;
    }
    const QList<EditorPane *> current = panes();
    for (EditorPane *pane : current) {
        if (pane != nullptr && pane->containsEditorWidget(widget)) {
            setActivePane(pane);
            return;
        }
    }
}

int EditorArea::enforceSingleCtrlSAuthority()
{
    int removed = 0;
    const QList<EditorPane *> current = panes();
    for (EditorPane *pane : current) {
        if (pane != nullptr) {
            removed += pane->enforceSingleCtrlSAuthority();
        }
    }
    return removed;
}

void EditorArea::setOwnedShortcuts(const QStringList &portableSequences)
{
    ownedShortcuts_ = portableSequences;
    ownedShortcuts_.removeDuplicates();
    ownedShortcuts_.sort();
    const QList<EditorPane *> current = panes();
    for (EditorPane *pane : current) {
        if (pane != nullptr) {
            pane->setOwnedShortcuts(ownedShortcuts_);
        }
    }
}

QStringList EditorArea::ownedShortcuts() const
{
    return ownedShortcuts_;
}

QStringList EditorArea::editorShortcutOwners() const
{
    // Every sequence a live editor's own action carries, with a readable owner
    // label. KTextEditor registers its standard actions on the inner
    // KateViewInternal widget (and some on the view), so the whole subtree is
    // walked exactly like the authority code above.
    QStringList owners;
    QSet<QString> seen;
    const QList<EditorPane *> current = panes();
    for (EditorPane *pane : current) {
        if (pane == nullptr) {
            continue;
        }
        for (int index = 0; index < pane->tabCount(); ++index) {
            QWidget *editor = pane->tabEditorWidget(index);
            if (editor == nullptr) {
                continue;
            }
            const auto record = [&owners, &seen](QAction *action) {
                if (action == nullptr || action->shortcuts().isEmpty()) {
                    return;
                }
                QString label = action->text().remove(QLatin1Char('&')).trimmed();
                if (label.isEmpty()) {
                    label = action->objectName();
                }
                if (!action->objectName().isEmpty()) {
                    label += QStringLiteral(" (") + action->objectName() + QLatin1Char(')');
                }
                for (const QKeySequence &sequence : action->shortcuts()) {
                    const QString portable = sequence.toString(QKeySequence::PortableText);
                    const QString key = portable + QLatin1Char('\t') + label;
                    if (portable.isEmpty() || seen.contains(key)) {
                        continue;
                    }
                    seen.insert(key);
                    owners.append(key);
                }
            };
            for (QAction *action : editor->actions()) {
                record(action);
            }
            for (QWidget *widget : editor->findChildren<QWidget *>()) {
                for (QAction *action : widget->actions()) {
                    record(action);
                }
            }
            for (QAction *action : editor->findChildren<QAction *>()) {
                record(action);
            }
        }
    }
    owners.sort();
    return owners;
}

void EditorArea::setProvider(EditorProvider *provider)
{
    if (provider_ == provider) {
        return;
    }
    provider_ = provider;
    // The shell builds its first pane before main() installs the real provider,
    // so every existing pane gets a fresh editor widget per tab; the layout tree
    // (splits, ratios, active pane) is untouched.
    const QList<EditorPane *> current = panes();
    for (EditorPane *pane : current) {
        pane->setProvider(provider_);
    }
}

EditorProvider *EditorArea::provider() const
{
    return provider_;
}

EditorPane *EditorArea::createPane(const QString &paneId)
{
    QString id = paneId;
    if (id.isEmpty()) {
        id = QStringLiteral("pane-%1").arg(nextPaneNumber_);
    }
    bool ok = false;
    const int numeric = id.section(QLatin1Char('-'), -1).toInt(&ok);
    if (ok) {
        nextPaneNumber_ = std::max(nextPaneNumber_, numeric + 1);
    }
    auto *pane = new EditorPane(id, provider_, nullptr);
    // RM-5b: a pane created after the shell registered its commands (a split,
    // a restored layout) must start with the same shortcut authority.
    pane->setOwnedShortcuts(ownedShortcuts_);
    pane->setOnFocusRequested([this](EditorPane *requested) { setActivePane(requested); });
    pane->setOnCloseRequested([this](EditorPane *requested) {
        setActivePane(requested);
        closeActivePane();
    });
    // T12: the shell's per-tab close prompt and its title/status-bar refresh are
    // installed on every pane, including the panes restored later.
    pane->setOnTabCloseRequested(onTabCloseRequested_);
    pane->setOnStateChanged([this]() {
        if (onStateChanged_ != nullptr) {
            onStateChanged_();
        }
    });
    // FIX 1/FIX 2: the display root and the ONE open notification are installed
    // on every pane, including panes created after the shell started.
    pane->setDisplayRoot(displayRoot_);
    if (onFileOpened_ != nullptr) {
        pane->setOnFileOpened(onFileOpened_);
    }
    if (onDocumentSaved_ != nullptr) {
        pane->setOnDocumentSaved(onDocumentSaved_);
    }
    return pane;
}

EditorArea::Node *EditorArea::newLeaf(EditorPane *pane)
{
    auto node = std::make_unique<Node>();
    node->kind = Node::Kind::Leaf;
    node->pane = pane;
    return node.release();
}

EditorArea::Node *EditorArea::findLeaf(Node *node, const EditorPane *pane) const
{
    if (node == nullptr) {
        return nullptr;
    }
    if (node->kind == Node::Kind::Leaf) {
        return node->pane == pane ? node : nullptr;
    }
    if (Node *found = findLeaf(node->first.get(), pane)) {
        return found;
    }
    return findLeaf(node->second.get(), pane);
}

EditorArea::Node *EditorArea::findParent(Node *node, const Node *child) const
{
    if (node == nullptr || node->kind != Node::Kind::Split) {
        return nullptr;
    }
    if (node->first.get() == child || node->second.get() == child) {
        return node;
    }
    if (Node *found = findParent(node->first.get(), child)) {
        return found;
    }
    return findParent(node->second.get(), child);
}

std::unique_ptr<EditorArea::Node> *EditorArea::ownerSlot(Node *child)
{
    if (root_.get() == child) {
        return &root_;
    }
    Node *parent = findParent(root_.get(), child);
    if (parent == nullptr) {
        return nullptr;
    }
    if (parent->first.get() == child) {
        return &parent->first;
    }
    return &parent->second;
}

void EditorArea::collectPanes(const Node *node, QList<EditorPane *> *out) const
{
    if (node == nullptr) {
        return;
    }
    if (node->kind == Node::Kind::Leaf) {
        if (node->pane != nullptr) {
            out->append(node->pane);
        }
        return;
    }
    collectPanes(node->first.get(), out);
    collectPanes(node->second.get(), out);
}

QList<EditorPane *> EditorArea::panes() const
{
    QList<EditorPane *> result;
    collectPanes(root_.get(), &result);
    return result;
}

int EditorArea::paneCount() const
{
    return panes().size();
}

EditorPane *EditorArea::ensurePane()
{
    if (root_ == nullptr) {
        auto *pane = createPane();
        root_.reset(newLeaf(pane));
        setActivePane(pane);
        rebuild();
        return pane;
    }
    if (active_ == nullptr) {
        const auto all = panes();
        if (!all.isEmpty()) {
            setActivePane(all.first());
        }
    }
    return active_;
}

EditorPane *EditorArea::activePane() const
{
    return active_;
}

void EditorArea::setActivePane(EditorPane *pane)
{
    if (pane == nullptr || pane == active_) {
        return;
    }
    if (active_ != nullptr) {
        active_->setActive(false);
    }
    active_ = pane;
    active_->setActive(true);
    if (onActivePaneChanged_ != nullptr) {
        onActivePaneChanged_(active_);
    }
}

QWidget *EditorArea::buildWidget(Node *node)
{
    if (node == nullptr) {
        return nullptr;
    }
    if (node->kind == Node::Kind::Leaf) {
        // The pane is wrapped so the layout container is never the pane widget
        // itself: rebuild()/clearTree() reparent panes out and delete the
        // containers, which must not destroy a pane that the model still owns.
        auto *wrapper = new QWidget();
        wrapper->setObjectName(QStringLiteral("pane-wrapper"));
        auto *wrapperLayout = new QVBoxLayout(wrapper);
        wrapperLayout->setContentsMargins(0, 0, 0, 0);
        wrapperLayout->setSpacing(0);
        wrapperLayout->addWidget(node->pane);
        return wrapper;
    }
    auto *splitter = new QSplitter(node->orientation);
    splitter->setObjectName(QStringLiteral("splitter-") +
                            QString::fromLatin1(node->orientation == Qt::Horizontal ? "h" : "v"));
    splitter->setChildrenCollapsible(true);
    splitter->addWidget(buildWidget(node->first.get()));
    splitter->addWidget(buildWidget(node->second.get()));
    const int total = 1000;
    const int first = std::clamp(static_cast<int>(node->ratio * total), 1, total - 1);
    splitter->setSizes({first, total - first});

    Node *captured = node;
    QObject::connect(splitter, &QSplitter::splitterMoved, splitter, [this, captured, splitter]() {
        if (rebuilding_) {
            return;
        }
        const QList<int> sizes = splitter->sizes();
        if (sizes.size() == 2) {
            const int sum = sizes.at(0) + sizes.at(1);
            if (sum > 0) {
                captured->ratio = static_cast<double>(sizes.at(0)) / static_cast<double>(sum);
            }
        }
        if (onLayoutChanged_ != nullptr) {
            onLayoutChanged_();
        }
    });
    return splitter;
}

void EditorArea::rebuild()
{
    if (rebuilding_) {
        return;
    }
    rebuilding_ = true;
    // Detach panes from the old widget tree before it is destroyed: QSplitter
    // owns its child widgets, and the panes must survive a re-layout.
    const auto all = panes();
    for (EditorPane *pane : all) {
        pane->setParent(nullptr);
    }
    delete content_;
    content_ = nullptr;
    if (root_ != nullptr) {
        content_ = buildWidget(root_.get());
    }
    if (content_ != nullptr) {
        layout_->addWidget(content_);
    }
    rebuilding_ = false;
    if (onLayoutChanged_ != nullptr) {
        onLayoutChanged_();
    }
}

EditorPane *EditorArea::splitActive(Qt::Orientation orientation)
{
    if (active_ == nullptr) {
        ensurePane();
    }
    Node *leaf = findLeaf(root_.get(), active_);
    if (leaf == nullptr) {
        return nullptr;
    }
    std::unique_ptr<Node> *slot = ownerSlot(leaf);
    if (slot == nullptr) {
        return nullptr;
    }
    auto *newPane = createPane();
    auto split = std::make_unique<Node>();
    split->kind = Node::Kind::Split;
    split->orientation = orientation;
    split->ratio = 0.5;
    split->first = std::move(*slot);
    split->second.reset(newLeaf(newPane));
    *slot = std::move(split);
    setActivePane(newPane);
    rebuild();
    return newPane;
}

bool EditorArea::closeActivePane()
{
    return closePane(active_);
}

bool EditorArea::closePane(EditorPane *closing)
{
    if (closing == nullptr || !panes().contains(closing)) {
        return false;
    }
    if (onClosePaneRequested_ != nullptr && !onClosePaneRequested_(closing)) {
        return false;
    }
    Node *leaf = findLeaf(root_.get(), closing);
    if (leaf == nullptr) {
        return false;
    }
    if (root_.get() == leaf) {
        // Last pane: replace its content with a fresh pane, never leave the
        // area without one (the shell must stay usable, empty state included).
        EditorPane *replacement = createPane();
        deletePane(closing);
        active_ = nullptr;
        leaf->pane = replacement;
        setActivePane(replacement);
        if (replacement->editorWidget() != nullptr) {
            replacement->editorWidget()->setFocus(Qt::OtherFocusReason);
        }
        rebuild();
        return true;
    }
    Node *parent = findParent(root_.get(), leaf);
    if (parent == nullptr) {
        return false;
    }
    const bool leafIsFirst = parent->first.get() == leaf;
    std::unique_ptr<Node> sibling = std::move(leafIsFirst ? parent->second : parent->first);
    std::unique_ptr<Node> *parentSlot = ownerSlot(parent);
    if (parentSlot == nullptr) {
        return false;
    }
    active_ = nullptr;
    deletePane(closing);
    *parentSlot = std::move(sibling);
    const auto remaining = panes();
    if (!remaining.isEmpty()) {
        setActivePane(remaining.first());
        if (remaining.first()->editorWidget() != nullptr) {
            remaining.first()->editorWidget()->setFocus(Qt::OtherFocusReason);
        }
    }
    rebuild();
    return true;
}

void EditorArea::focusNextPane()
{
    const auto all = panes();
    if (all.size() < 2) {
        return;
    }
    const int index = all.indexOf(active_);
    const int next = (index + 1) % all.size();
    setActivePane(all.at(next));
    if (active_->editorWidget() != nullptr) {
        active_->editorWidget()->setFocus(Qt::OtherFocusReason);
    }
}

void EditorArea::resetLayout()
{
    clearTree();
    auto *pane = createPane();
    root_.reset(newLeaf(pane));
    setActivePane(pane);
    rebuild();
}

void EditorArea::newDocument()
{
    EditorPane *pane = ensurePane();
    if (pane == nullptr) {
        return;
    }
    setActivePane(pane);
    pane->newDocument();
}

bool EditorArea::triggerActiveEditorAction(const QStringList &objectNames,
                                           const QString &fallbackMethod)
{
    EditorPane *pane = activePane();
    return pane != nullptr && pane->triggerEditorAction(objectNames, fallbackMethod);
}

bool EditorArea::activeEditorActionEnabled(const QStringList &objectNames) const
{
    const EditorPane *pane = activePane();
    return pane != nullptr && pane->editorActionEnabled(objectNames);
}

void EditorArea::deletePane(EditorPane *pane)
{
    if (pane == nullptr) {
        return;
    }
    // T12: a pane owns several editor widgets now; let the live provider drop
    // their subscriptions before the widgets (and the pane) are destroyed.
    pane->releaseListeners();
    pane->setParent(nullptr);
    delete pane;
}

void EditorArea::clearTree()
{
    const auto all = panes();
    for (EditorPane *pane : all) {
        pane->releaseListeners();
        pane->setParent(nullptr);
        delete pane;
    }
    active_ = nullptr;
    delete content_;
    content_ = nullptr;
    root_.reset();
}

QJsonObject EditorArea::nodeToJson(const Node *node) const
{
    QJsonObject json;
    if (node == nullptr) {
        return json;
    }
    if (node->kind == Node::Kind::Leaf) {
        // Archive schema v2: the leaf IS the pane's session document (tabs, paths,
        // caret/scroll, active tab). SessionPane owns the JSON shape, so the
        // archive schema stays in one place (src/app/session_store.h).
        SessionPane pane;
        pane.paneId = node->pane != nullptr ? node->pane->paneId() : QString();
        pane.provider = provider_ != nullptr ? provider_->id() : QString();
        if (node->pane != nullptr) {
            int activeTab = -1;
            pane.documents = node->pane->documentStates(&activeTab);
            pane.activeTab = activeTab;
        }
        return pane.toJson();
    }
    json.insert(QStringLiteral("type"), QStringLiteral("split"));
    json.insert(QStringLiteral("orientation"),
                QString::fromLatin1(node->orientation == Qt::Horizontal ? "horizontal" : "vertical"));
    json.insert(QStringLiteral("ratio"), node->ratio);
    json.insert(QStringLiteral("first"), nodeToJson(node->first.get()));
    json.insert(QStringLiteral("second"), nodeToJson(node->second.get()));
    return json;
}

EditorArea::Node *EditorArea::nodeFromJson(const QJsonObject &json, QList<EditorPane *> *created,
                                           QString *error)
{
    const QString type = json.value(QStringLiteral("type")).toString();
    auto node = std::make_unique<Node>();
    if (type == QLatin1String("leaf")) {
        const QString paneId = json.value(QStringLiteral("pane")).toString();
        if (paneId.isEmpty()) {
            if (error != nullptr) {
                *error = QCoreApplication::translate("VEdit", "leaf node without pane id");
            }
            return nullptr;
        }
        const QString providerId = json.value(QStringLiteral("provider")).toString();
        if (!providerId.isEmpty() && provider_ != nullptr && providerId != provider_->id()) {
            diagnostics_.append(QStringLiteral("pane %1 was saved by provider '%2', restored with '%3'")
                                    .arg(paneId, providerId, provider_->id()));
        }
        node->kind = Node::Kind::Leaf;
        node->pane = createPane(paneId);
        if (created != nullptr) {
            created->append(node->pane);
        }
        // T19: re-open the pane's archived tabs, in the archived order, and put
        // the caret/scroll back where the user left it. A caret is only replayed
        // into the provider that wrote it (line numbering is provider-specific),
        // and a vanised file is a diagnostic, never a failed restore.
        const SessionPane saved = SessionPane::fromJson(json);
        if (!saved.documents.isEmpty()) {
            const bool sameProvider = saved.provider.isEmpty() || provider_ == nullptr
                                      || saved.provider == provider_->id();
            QStringList restoreDiagnostics;
            const int restored = node->pane->restoreDocuments(saved.documents, saved.activeTab,
                                                              sameProvider, &restoreDiagnostics);
            diagnostics_.append(restoreDiagnostics);
            if (restored == 0) {
                diagnostics_.append(
                    QStringLiteral("session: no document of pane %1 could be reopened (%2 archived)")
                        .arg(paneId)
                        .arg(saved.documents.size()));
            }
        }
        return node.release();
    }
    if (type == QLatin1String("split")) {
        const QString orientation = json.value(QStringLiteral("orientation")).toString();
        if (orientation != QLatin1String("horizontal") && orientation != QLatin1String("vertical")) {
            if (error != nullptr) {
                *error = QCoreApplication::translate("VEdit", "split node without a valid orientation");
            }
            return nullptr;
        }
        const QJsonValue firstValue = json.value(QStringLiteral("first"));
        const QJsonValue secondValue = json.value(QStringLiteral("second"));
        if (!firstValue.isObject() || !secondValue.isObject()) {
            if (error != nullptr) {
                *error = QCoreApplication::translate("VEdit", "split node needs two object children");
            }
            return nullptr;
        }
        node->kind = Node::Kind::Split;
        node->orientation = orientation == QLatin1String("horizontal") ? Qt::Horizontal : Qt::Vertical;
        const double ratio = json.value(QStringLiteral("ratio")).toDouble(0.5);
        node->ratio = std::clamp(ratio, 0.05, 0.95);
        node->first.reset(nodeFromJson(firstValue.toObject(), created, error));
        if (node->first == nullptr) {
            return nullptr;
        }
        node->second.reset(nodeFromJson(secondValue.toObject(), created, error));
        if (node->second == nullptr) {
            node->first.reset();
            return nullptr;
        }
        return node.release();
    }
    if (error != nullptr) {
        *error = QCoreApplication::translate("VEdit", "unknown layout node type '%1'").arg(type);
    }
    return nullptr;
}

QJsonObject EditorArea::saveLayout() const
{
    QJsonObject json;
    // Schema v2 (T19): every leaf also carries its tabs and the caret/scroll of
    // each, which is what makes a restart continue where the user left off. The
    // tree itself (panes, orientation, ratio, active pane) is unchanged since v1,
    // so a v1 reader still sees the layout it always saw.
    json.insert(QStringLiteral("version"), SessionState::kCurrentVersion);
    json.insert(QStringLiteral("nextPane"), nextPaneNumber_);
    json.insert(QStringLiteral("active"), active_ != nullptr ? active_->paneId() : QString());
    json.insert(QStringLiteral("root"), nodeToJson(root_.get()));
    return json;
}

bool EditorArea::restoreLayout(const QJsonObject &layout, QString *error)
{
    const QJsonValue rootValue = layout.value(QStringLiteral("root"));
    if (!rootValue.isObject()) {
        if (error != nullptr) {
            *error = QCoreApplication::translate("VEdit", "layout has no root object");
        }
        return false;
    }
    diagnostics_.clear();
    const int archiveVersion = layout.value(QStringLiteral("version")).toInt(1);
    if (archiveVersion > SessionState::kCurrentVersion) {
        diagnostics_.append(QStringLiteral("session layout version %1 is newer than %2: only the "
                                           "understood fields were restored")
                                .arg(archiveVersion)
                                .arg(SessionState::kCurrentVersion));
    }
    QString parseError;
    QList<EditorPane *> created;
    Node *parsed = nodeFromJson(rootValue.toObject(), &created, &parseError);
    if (parsed == nullptr) {
        for (EditorPane *pane : created) {
            deletePane(pane);
        }
        if (error != nullptr) {
            *error = parseError.isEmpty() ? QCoreApplication::translate("VEdit", "layout parse failed") : parseError;
        }
        return false;
    }
    clearTree();
    root_.reset(parsed);
    nextPaneNumber_ = std::max(nextPaneNumber_, layout.value(QStringLiteral("nextPane")).toInt(1));
    const QString activeId = layout.value(QStringLiteral("active")).toString();
    const auto all = panes();
    EditorPane *target = all.isEmpty() ? nullptr : all.first();
    for (EditorPane *pane : all) {
        if (pane->paneId() == activeId) {
            target = pane;
            break;
        }
    }
    active_ = nullptr;
    if (target != nullptr) {
        setActivePane(target);
    }
    rebuild();
    return true;
}

QStringList EditorArea::diagnostics() const
{
    return diagnostics_;
}

void EditorArea::setOnLayoutChanged(std::function<void()> callback)
{
    onLayoutChanged_ = std::move(callback);
}

void EditorArea::setOnActivePaneChanged(std::function<void(EditorPane *)> callback)
{
    onActivePaneChanged_ = std::move(callback);
}

void EditorArea::setOnClosePaneRequested(std::function<bool(EditorPane *)> callback)
{
    onClosePaneRequested_ = std::move(callback);
}

void EditorArea::setOnTabCloseRequested(std::function<SaveDecision(EditorPane *, int)> callback)
{
    onTabCloseRequested_ = std::move(callback);
    const auto all = panes();
    for (EditorPane *pane : all) {
        if (pane != nullptr) {
            pane->setOnTabCloseRequested(onTabCloseRequested_);
        }
    }
}

void EditorArea::setOnStateChanged(std::function<void()> callback)
{
    onStateChanged_ = std::move(callback);
    const auto all = panes();
    for (EditorPane *pane : all) {
        if (pane == nullptr) {
            continue;
        }
        pane->setOnStateChanged([this]() {
            if (onStateChanged_ != nullptr) {
                onStateChanged_();
            }
        });
    }
}

void EditorArea::setOnFileOpened(std::function<void(const QString &)> callback)
{
    onFileOpened_ = std::move(callback);
    const auto all = panes();
    for (EditorPane *pane : all) {
        if (pane != nullptr) {
            pane->setOnFileOpened(onFileOpened_);
        }
    }
}

void EditorArea::setOnDocumentSaved(std::function<void(const QString &, bool)> callback)
{
    onDocumentSaved_ = std::move(callback);
    const auto all = panes();
    for (EditorPane *pane : all) {
        if (pane != nullptr) {
            pane->setOnDocumentSaved(onDocumentSaved_);
        }
    }
}

void EditorArea::setDisplayRoot(const QString &root)
{
    displayRoot_ = root;
    const auto all = panes();
    for (EditorPane *pane : all) {
        if (pane != nullptr) {
            pane->setDisplayRoot(displayRoot_);
        }
    }
}

} // namespace vedit::app
