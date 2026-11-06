#include "find_replace.h"

#include "multi_cursor.h"

#include <KTextEditor/Document>
#include <KTextEditor/View>

#include <QAction>
#include <QChar>
#include <QComboBox>
#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QShortcut>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <QCoreApplication>

namespace vedit::editor {
namespace {

// A hard ceiling so a pathological pattern (e.g. an empty regex match at every
// position) cannot freeze the GUI; the status line says how to narrow it.
constexpr int kMaxMatches = 100000;

KTextEditor::Cursor advanceOneCharacter(KTextEditor::Document *document,
                                        const KTextEditor::Cursor &position)
{
    const int line = position.line();
    if (position.column() < document->lineLength(line)) {
        return KTextEditor::Cursor(line, position.column() + 1);
    }
    if (line + 1 < document->lines()) {
        return KTextEditor::Cursor(line + 1, 0);
    }
    return KTextEditor::Cursor(-1, -1);
}

bool atOrAfter(const KTextEditor::Cursor &left, const KTextEditor::Cursor &right)
{
    if (left.line() != right.line()) {
        return left.line() > right.line();
    }
    return left.column() >= right.column();
}

bool isWordCharacter(QChar character)
{
    return character.isLetterOrNumber() || character == QLatin1Char('_');
}

// Whole-word filtering on top of the kernel search. Done here (instead of only
// passing KTextEditor::WholeWords) because the kernel honours that flag for
// plain text only, and AC-4.3 wants "whole word" to combine with regex too.
bool hasWholeWordBoundaries(KTextEditor::Document *document, const KTextEditor::Range &match)
{
    const KTextEditor::Cursor start = match.start();
    const KTextEditor::Cursor end = match.end();
    if (start.column() > 0) {
        const QString line = document->line(start.line());
        if (start.column() - 1 < line.size() && isWordCharacter(line.at(start.column() - 1))) {
            return false;
        }
    }
    if (end.line() >= 0 && end.line() < document->lines()) {
        const QString line = document->line(end.line());
        if (end.column() < line.size() && isWordCharacter(line.at(end.column()))) {
            return false;
        }
    }
    return true;
}

// End the interval at the last real character: (lines(), 0) makes the
// KTextEditor search emit "line 1 is not within interval" noise.
KTextEditor::Range wholeDocumentRange(KTextEditor::Document *document)
{
    const int lastLine = document->lines() - 1;
    if (lastLine < 0) {
        return KTextEditor::Range::invalid();
    }
    return KTextEditor::Range(KTextEditor::Cursor(0, 0),
                              KTextEditor::Cursor(lastLine, document->lineLength(lastLine)));
}

QToolButton *makeToggle(QWidget *parent, const QString &text, const QString &objectName,
                        const QString &tooltip)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(objectName);
    button->setText(text);
    button->setToolTip(tooltip);
    button->setCheckable(true);
    button->setAutoRaise(true);
    return button;
}

// The path of the document this bar is installed on. The editor provider records
// it in this property when it opens a file (src/editor/kernel/ktexteditor_provider.cpp);
// the document URL is the fallback. The dock's Current-File scope searches it.
QString documentPath(KTextEditor::View *view)
{
    if (view == nullptr) {
        return QString();
    }
    QString path = view->property("vedit.openFilePath").toString();
    if (path.isEmpty() && view->document() != nullptr && view->document()->url().isLocalFile()) {
        path = view->document()->url().toLocalFile();
    }
    if (path.isEmpty()) {
        return QString();
    }
    return QFileInfo(path).absoluteFilePath();
}

} // namespace

FindResult findMatches(KTextEditor::Document *document, const QString &needle,
                       const FindOptions &options, const KTextEditor::Range *scope,
                       QList<KTextEditor::Range> *ranges)
{
    FindResult result;
    if (ranges != nullptr) {
        ranges->clear();
    }
    if (document == nullptr) {
        result.error = QCoreApplication::translate("VEdit", "find: no document");
        return result;
    }
    if (needle.isEmpty()) {
        result.error = QCoreApplication::translate("VEdit", "find: the pattern is empty");
        return result;
    }
    if (options.regex) {
        const QRegularExpression expression(needle);
        if (!expression.isValid()) {
            result.error = QStringLiteral("find: invalid regular expression: %1")
                               .arg(expression.errorString());
            return result;
        }
    }

    KTextEditor::Range searchRange = wholeDocumentRange(document);
    if (scope != nullptr && scope->isValid() && scope->start() != scope->end()) {
        searchRange = *scope;
    }
    if (!searchRange.isValid() || searchRange.start() == searchRange.end()) {
        result.ok = true;
        return result;
    }

    KTextEditor::SearchOptions searchOptions = KTextEditor::Default;
    if (!options.caseSensitive) {
        searchOptions |= KTextEditor::CaseInsensitive;
    }
    if (options.regex) {
        searchOptions |= KTextEditor::Regex;
    } else if (options.wholeWords) {
        searchOptions |= KTextEditor::WholeWords;
    }

    KTextEditor::Cursor start = searchRange.start();
    const KTextEditor::Cursor end = searchRange.end();
    int count = 0;
    while (true) {
        if (!start.isValid() || atOrAfter(start, end)) {
            break;
        }
        const QList<KTextEditor::Range> found =
            document->searchText(KTextEditor::Range(start, end), needle, searchOptions);
        if (found.isEmpty() || !found.first().isValid()) {
            break;
        }
        const KTextEditor::Range match = found.first();
        const bool accepted = !options.wholeWords || hasWholeWordBoundaries(document, match);
        if (accepted) {
            if (ranges != nullptr) {
                ranges->append(match);
            }
            ++count;
            if (count >= kMaxMatches) {
                result.error = QStringLiteral(
                                   "find: more than %1 matches; narrow the pattern or use a "
                                   "selection")
                                   .arg(kMaxMatches);
                return result;
            }
            KTextEditor::Cursor next = match.end();
            if (match.start() == match.end()) {
                next = advanceOneCharacter(document, match.end());
            }
            if (!next.isValid() || atOrAfter(start, next)) {
                break;
            }
            start = next;
        } else {
            // A rejected whole-word match can hide an accepted one that starts
            // inside it, so continue one character after its START, not its end.
            const KTextEditor::Cursor next = advanceOneCharacter(document, match.start());
            if (!next.isValid() || atOrAfter(start, next)) {
                break;
            }
            start = next;
        }
    }
    result.ok = true;
    result.matches = count;
    return result;
}

FindResult findMatches(KTextEditor::Document *document, const QString &needle,
                       bool caseSensitive, QList<KTextEditor::Range> *ranges)
{
    FindOptions options;
    options.caseSensitive = caseSensitive;
    return findMatches(document, needle, options, nullptr, ranges);
}

ReplaceResult replaceAll(KTextEditor::Document *document, const QString &needle,
                         const QString &replacement, const FindOptions &options,
                         const KTextEditor::Range *scope)
{
    ReplaceResult result;
    QList<KTextEditor::Range> ranges;
    const FindResult found = findMatches(document, needle, options, scope, &ranges);
    if (!found.ok) {
        result.error = found.error;
        return result;
    }
    result.found = found.matches;
    if (ranges.isEmpty()) {
        result.ok = true;
        return result;
    }

    // One transaction = one undo step for the whole replace-all. The matches are
    // in forward order, so replacing from the last to the first keeps every
    // earlier range valid (later replacements cannot shift it).
    {
        KTextEditor::Document::EditingTransaction transaction(document);
        for (int index = static_cast<int>(ranges.size()) - 1; index >= 0; --index) {
            if (document->replaceText(ranges.at(index), replacement)) {
                ++result.replaced;
            }
        }
    }
    if (result.replaced != result.found) {
        result.error = QCoreApplication::translate("VEdit", "replace-all replaced %1 of %2 matches")
                           .arg(result.replaced)
                           .arg(result.found);
        return result;
    }
    result.ok = true;
    return result;
}

ReplaceResult replaceAll(KTextEditor::Document *document, const QString &needle,
                         const QString &replacement, bool caseSensitive)
{
    FindOptions options;
    options.caseSensitive = caseSensitive;
    return replaceAll(document, needle, replacement, options, nullptr);
}

ReplaceResult replaceOne(KTextEditor::Document *document, const KTextEditor::Range &match,
                         const QString &replacement)
{
    ReplaceResult result;
    if (document == nullptr) {
        result.error = QStringLiteral("replace: no document");
        return result;
    }
    if (!match.isValid()) {
        result.error = QStringLiteral("replace: the match range is invalid");
        return result;
    }
    {
        KTextEditor::Document::EditingTransaction transaction(document);
        if (!document->replaceText(match, replacement)) {
            result.found = 1;
            result.error = QStringLiteral("replace: the document refused the replacement");
            return result;
        }
    }
    result.found = 1;
    result.replaced = 1;
    result.ok = true;
    return result;
}

// --- the in-view bar -------------------------------------------------------

FindReplaceBar::FindReplaceBar(KTextEditor::View *view)
    : QFrame(view)
    , view_(view)
{
    setObjectName(QStringLiteral("vedit-find-bar"));
    setFrameShape(QFrame::StyledPanel);
    setFrameShadow(QFrame::Raised);
    setAutoFillBackground(true);
}

FindReplaceBar::~FindReplaceBar()
{
    if (hubSubscription_ >= 0) {
        vedit::search::SearchHub::instance().unsubscribe(hubSubscription_);
        hubSubscription_ = -1;
    }
}

FindReplaceBar *FindReplaceBar::install(KTextEditor::View *view)
{
    if (view == nullptr) {
        return nullptr;
    }
    if (FindReplaceBar *existing = instanceFor(view)) {
        return existing;
    }
    auto *bar = new FindReplaceBar(view);
    bar->buildUi();
    bar->installKeyHandling();
    bar->refreshDocumentConnection();
    // Two-way sync with the search dock (ux-batch-2 B2): a query/option change
    // on either surface is adopted by the other, without re-publishing.
    bar->hubSubscription_ = vedit::search::SearchHub::instance().subscribe(
        [bar](const vedit::search::SearchHandoff &handoff, const void *source) {
            if (source == bar) {
                return;
            }
            bar->adoptFromHub(handoff);
        });
    QObject::connect(view, &KTextEditor::View::selectionChanged, bar,
                     [bar](KTextEditor::View *) {
                         if (!bar->isVisible() || bar->selectionButton_ == nullptr ||
                             !bar->selectionButton_->isChecked()) {
                             return;
                         }
                         const KTextEditor::Range selection = bar->view_->selectionRange();
                         if (selection == bar->lastApplied_) {
                             return; // this selection was applied by the bar itself
                         }
                         if (!selection.isValid() || selection.start() == selection.end()) {
                             return;
                         }
                         bar->scope_ = selection;
                         bar->hasScope_ = true;
                         bar->dirty_ = true;
                         bar->recompute(false);
                     });
    bar->hide();
    return bar;
}

FindReplaceBar *FindReplaceBar::instanceFor(QWidget *editorWidget)
{
    if (editorWidget == nullptr) {
        return nullptr;
    }
    return editorWidget->findChild<FindReplaceBar *>(QStringLiteral("vedit-find-bar"));
}

void FindReplaceBar::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 6, 8, 6);
    outer->setSpacing(4);

    auto *findRow = new QHBoxLayout();
    findRow->setSpacing(4);
    findEdit_ = new QLineEdit(this);
    findEdit_->setObjectName(QStringLiteral("vedit-find-pattern"));
    findEdit_->setPlaceholderText(tr("Find"));
    findEdit_->setClearButtonEnabled(true);
    findEdit_->setMinimumWidth(200);
    findRow->addWidget(findEdit_, 1);

    caseButton_ = makeToggle(this, QStringLiteral("Aa"), QStringLiteral("vedit-find-case"),
                             tr("Case sensitive"));
    caseButton_->setChecked(true);
    wholeButton_ = makeToggle(this, QStringLiteral("W"), QStringLiteral("vedit-find-whole"),
                              tr("Whole words"));
    regexButton_ = makeToggle(this, QStringLiteral(".*"), QStringLiteral("vedit-find-regex"),
                              tr("Regular expression"));
    // The shared scope option (ux-batch-2 B2): the same selector the search dock
    // has. Whole project hands the query + options to the dock; this bar never
    // walks the project from inside one document.
    scopeCombo_ = new QComboBox(this);
    scopeCombo_->setObjectName(QStringLiteral("vedit-find-scope"));
    scopeCombo_->addItem(tr("Current File"),
                         static_cast<int>(vedit::search::SearchScope::CurrentFile));
    scopeCombo_->addItem(tr("Whole Project"),
                         static_cast<int>(vedit::search::SearchScope::WholeProject));
    scopeCombo_->setToolTip(tr("Search this document, or hand the query to the Search panel"));
    selectionButton_ =
        makeToggle(this, QStringLiteral("Sel"), QStringLiteral("vedit-find-scope-selection"),
                   tr("Search and replace only inside the selection"));
    findRow->addWidget(caseButton_);
    findRow->addWidget(wholeButton_);
    findRow->addWidget(regexButton_);
    findRow->addWidget(scopeCombo_);
    findRow->addWidget(selectionButton_);

    auto *previous = new QToolButton(this);
    previous->setObjectName(QStringLiteral("vedit-find-prev"));
    previous->setText(tr("Prev"));
    previous->setToolTip(tr("Previous match (Shift+F3)"));
    auto *next = new QToolButton(this);
    next->setObjectName(QStringLiteral("vedit-find-next"));
    next->setText(tr("Next"));
    next->setToolTip(tr("Next match (F3)"));
    findRow->addWidget(previous);
    findRow->addWidget(next);

    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("vedit-find-status"));
    status_->setMinimumWidth(90);
    findRow->addWidget(status_);

    auto *close = new QToolButton(this);
    close->setObjectName(QStringLiteral("vedit-find-close"));
    close->setText(QStringLiteral("x"));
    close->setToolTip(tr("Close (Esc)"));
    close->setAutoRaise(true);
    findRow->addWidget(close);
    outer->addLayout(findRow);

    replaceRow_ = new QWidget(this);
    auto *replaceLayout = new QHBoxLayout(replaceRow_);
    replaceLayout->setContentsMargins(0, 0, 0, 0);
    replaceLayout->setSpacing(4);
    replaceEdit_ = new QLineEdit(replaceRow_);
    replaceEdit_->setObjectName(QStringLiteral("vedit-find-replacement"));
    replaceEdit_->setPlaceholderText(tr("Replace with"));
    replaceLayout->addWidget(replaceEdit_, 1);
    auto *replaceOneButton = new QToolButton(replaceRow_);
    replaceOneButton->setObjectName(QStringLiteral("vedit-find-replace"));
    replaceOneButton->setText(tr("Replace"));
    replaceOneButton->setToolTip(tr("Replace the current match"));
    auto *replaceAllButton = new QToolButton(replaceRow_);
    replaceAllButton->setObjectName(QStringLiteral("vedit-find-replace-all"));
    replaceAllButton->setText(tr("Replace All"));
    replaceAllButton->setToolTip(tr("Replace every match in one undo step"));
    replaceLayout->addWidget(replaceOneButton);
    replaceLayout->addWidget(replaceAllButton);
    outer->addWidget(replaceRow_);
    replaceRow_->setVisible(false);

    connect(findEdit_, &QLineEdit::textChanged, this, [this]() {
        dirty_ = true;
        if (isVisible()) {
            recompute(false);
        }
        publishToHub();
    });
    connect(findEdit_, &QLineEdit::returnPressed, this, [this]() {
        // In project scope the Enter key runs the project search through the
        // dock (the bar does not search the tree itself).
        if (projectScope()) {
            requestProjectSearch();
        } else {
            findNext();
        }
    });
    connect(replaceEdit_, &QLineEdit::returnPressed, this, [this]() { replaceCurrent(); });
    for (QToolButton *toggle : {caseButton_, wholeButton_, regexButton_}) {
        connect(toggle, &QToolButton::toggled, this, [this](bool) {
            dirty_ = true;
            if (isVisible()) {
                recompute(false);
            }
            publishToHub();
        });
    }
    connect(scopeCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        if (syncingHub_) {
            return;
        }
        const bool project = projectScope();
        // The selection scope only makes sense for the in-file search.
        if (selectionButton_ != nullptr) {
            selectionButton_->setEnabled(!project);
            if (project && selectionButton_->isChecked()) {
                selectionButton_->setChecked(false);
            }
        }
        dirty_ = true;
        if (project) {
            ranges_.clear();
            current_ = -1;
            if (isVisible()) {
                updateStatus();
            }
            requestProjectSearch();
        } else if (isVisible()) {
            recompute(false);
        }
        publishToHub();
    });
    connect(selectionButton_, &QToolButton::toggled, this,
            [this](bool checked) { applySelectionToggle(checked); });
    connect(previous, &QToolButton::clicked, this, [this]() { findPrevious(); });
    connect(next, &QToolButton::clicked, this, [this]() { findNext(); });
    connect(replaceOneButton, &QToolButton::clicked, this, [this]() { replaceCurrent(); });
    connect(replaceAllButton, &QToolButton::clicked, this, [this]() { replaceAllMatches(); });
    connect(close, &QToolButton::clicked, this, [this]() { closeBar(); });

    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, [this]() { closeBar(); });

    updateStatus();
}

void FindReplaceBar::installKeyHandling()
{
    if (view_ == nullptr) {
        return;
    }
    clearKernelFindShortcuts();
    // The bar owns Ctrl+F / Ctrl+H / F3 / Shift+F3. Per the shared kernel
    // contract (multi_cursor.h) the filter goes on the widget TREE below the
    // view — real focus is KatePart's inner widget — and claims ShortcutOverride
    // only for those four sequences. The tree install covers the view, the bar
    // and the bar's own children; ChildAdded in eventFilter covers late children.
    installEditorEventFilter(view_, this);
}

bool FindReplaceBar::handleShortcutKey(QKeyEvent *event, bool overridePhase)
{
    const Qt::KeyboardModifiers modifiers =
        event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier |
                              Qt::MetaModifier);
    const int key = event->key();
    enum class Command { None, Find, Replace, Next, Previous };
    Command command = Command::None;
    if (key == Qt::Key_F && modifiers == Qt::ControlModifier) {
        command = Command::Find;
    } else if (key == Qt::Key_H && modifiers == Qt::ControlModifier) {
        command = Command::Replace;
    } else if (key == Qt::Key_F3) {
        if (modifiers == Qt::NoModifier) {
            command = Command::Next;
        } else if (modifiers == Qt::ShiftModifier) {
            command = Command::Previous;
        }
    }
    if (command == Command::None) {
        return false;
    }
    if (overridePhase) {
        // Claim the ShortcutOverride: the key then arrives here as a KeyPress
        // and the kernel's own action never sees it.
        event->accept();
        return true;
    }
    switch (command) {
    case Command::Find:
        openFind();
        break;
    case Command::Replace:
        openReplace();
        break;
    case Command::Next:
        findNext();
        break;
    case Command::Previous:
        findPrevious();
        break;
    case Command::None:
        break;
    }
    event->accept();
    return true;
}

void FindReplaceBar::clearKernelFindShortcuts()
{
    if (view_ == nullptr) {
        return;
    }
    // KTextEditor's embedded view ships its own find actions on the same four
    // sequences; two shortcuts with one sequence make Qt drop BOTH as ambiguous
    // ("QAction::event: Ambiguous shortcut overload"). The product has exactly
    // one find UI (this bar), so the kernel actions lose their sequences here.
    const QList<QKeySequence> owned{QKeySequence::Find, QKeySequence(QStringLiteral("Ctrl+H")),
                                    QKeySequence(QStringLiteral("F3")),
                                    QKeySequence(QStringLiteral("Shift+F3"))};
    const QList<QAction *> actions = view_->findChildren<QAction *>();
    for (QAction *action : actions) {
        if (action == nullptr || action->shortcut().isEmpty()) {
            continue;
        }
        if (owned.contains(action->shortcut())) {
            action->setShortcut(QKeySequence());
        }
    }
}

void FindReplaceBar::openFind()
{
    openBar(false);
}

void FindReplaceBar::openReplace()
{
    openBar(true);
}

void FindReplaceBar::openBar(bool withReplace)
{
    if (view_ == nullptr || view_->document() == nullptr) {
        return;
    }
    // The dock's Current-File scope searches the document this bar belongs to.
    const QString path = documentPath(view_);
    if (!path.isEmpty()) {
        vedit::search::SearchHub::instance().setCurrentFilePath(path);
    }
    replaceRow_->setVisible(withReplace);
    captureScopeAtOpen();
    refreshDocumentConnection();
    // The kernel may create its find actions lazily; clear their sequences again
    // so the first Ctrl+F the user presses reaches this bar, not an ambiguity.
    clearKernelFindShortcuts();
    if (findEdit_->text().isEmpty()) {
        const KTextEditor::Range selection = view_->selectionRange();
        if (selection.isValid() && selection.start() != selection.end() &&
            selection.start().line() == selection.end().line()) {
            const QString text = view_->document()->text(selection);
            if (!text.isEmpty() && text.size() <= 200) {
                findEdit_->setText(text);
            }
        }
    }
    dirty_ = true;
    show();
    raise();
    positionOverlay();
    recompute(false);
    findEdit_->setFocus(Qt::ShortcutFocusReason);
    findEdit_->selectAll();
}

void FindReplaceBar::closeBar()
{
    hide();
    if (view_ != nullptr) {
        view_->setFocus();
    }
}

void FindReplaceBar::positionOverlay()
{
    if (view_ == nullptr) {
        return;
    }
    adjustSize();
    const int margin = 10;
    const int x = std::max(margin, view_->width() - width() - margin);
    move(x, margin);
}

bool FindReplaceBar::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::ChildAdded && isEditorOwnedWidget(view_, watched)) {
        auto *child = static_cast<QChildEvent *>(event)->child();
        if (child != nullptr && child->isWidgetType()) {
            child->installEventFilter(this);
            for (QWidget *descendant : child->findChildren<QWidget *>()) {
                descendant->installEventFilter(this);
            }
        }
    }
    if (!isEditorOwnedWidget(view_, watched)) {
        return QFrame::eventFilter(watched, event);
    }
    if (watched == view_ && (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
        if (isVisible()) {
            positionOverlay();
        }
        return QFrame::eventFilter(watched, event);
    }
    if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) {
        if (handleShortcutKey(static_cast<QKeyEvent *>(event),
                              event->type() == QEvent::ShortcutOverride)) {
            return true;
        }
    }
    return QFrame::eventFilter(watched, event);
}

void FindReplaceBar::refreshDocumentConnection()
{
    KTextEditor::Document *document = view_ != nullptr ? view_->document() : nullptr;
    if (document == connectedDocument_) {
        return;
    }
    if (textConnection_) {
        QObject::disconnect(textConnection_);
    }
    connectedDocument_ = document;
    if (document != nullptr) {
        textConnection_ = connect(document, &KTextEditor::Document::textChanged, this,
                                  [this](KTextEditor::Document *) {
                                      dirty_ = true;
                                      if (!suppressRefresh_ && isVisible()) {
                                          recompute(false);
                                      }
                                  });
    }
}

void FindReplaceBar::captureScopeAtOpen()
{
    // Scoping is an EXPLICIT choice: opening Ctrl+F with a selection must not
    // silently restrict the search to it (otherwise "select a word, search for
    // another word" returns nothing). The selection is only adopted when the
    // Sel toggle was already on; the toggle is cleared when there is nothing
    // valid to scope to.
    const bool wanted = selectionButton_ != nullptr && selectionButton_->isChecked();
    const KTextEditor::Range selection =
        view_ != nullptr ? view_->selectionRange() : KTextEditor::Range::invalid();
    const bool usable = wanted && selection.isValid() && selection.start() != selection.end();
    scope_ = usable ? selection : KTextEditor::Range::invalid();
    hasScope_ = usable;
    if (selectionButton_ != nullptr && wanted && !usable) {
        syncingButtons_ = true;
        selectionButton_->setChecked(false);
        syncingButtons_ = false;
    }
}

void FindReplaceBar::applySelectionToggle(bool checked)
{
    if (syncingButtons_) {
        return;
    }
    bool usable = false;
    if (checked && view_ != nullptr) {
        const KTextEditor::Range selection = view_->selectionRange();
        usable = selection.isValid() && selection.start() != selection.end() &&
                 !(selection == lastApplied_);
    }
    if (checked && !usable) {
        syncingButtons_ = true;
        selectionButton_->setChecked(false);
        syncingButtons_ = false;
        hasScope_ = false;
        scope_ = KTextEditor::Range::invalid();
        dirty_ = true;
        if (isVisible()) {
            recompute(false);
        }
        transient_ = tr("Select text to scope the search");
        updateStatus();
        return;
    }
    if (checked) {
        scope_ = view_->selectionRange();
        hasScope_ = true;
    } else {
        scope_ = KTextEditor::Range::invalid();
        hasScope_ = false;
    }
    dirty_ = true;
    if (isVisible()) {
        recompute(false);
    }
}

void FindReplaceBar::recompute(bool selectCurrent)
{
    ranges_.clear();
    current_ = -1;
    error_.clear();
    transient_.clear();
    dirty_ = false;
    KTextEditor::Document *document = view_ != nullptr ? view_->document() : nullptr;
    if (document == nullptr || findEdit_ == nullptr) {
        updateStatus();
        return;
    }
    if (projectScope()) {
        // The project search belongs to the dock; the bar must not pretend to
        // search the whole project from inside one document (B2 item 3).
        updateStatus();
        return;
    }
    const QString pattern = findEdit_->text();
    if (pattern.isEmpty()) {
        updateStatus();
        return;
    }
    const FindResult found = findMatches(document, pattern, options(), selectionScope(), &ranges_);
    if (!found.ok) {
        ranges_.clear();
        error_ = found.error;
        updateStatus();
        return;
    }
    if (ranges_.isEmpty()) {
        updateStatus();
        return;
    }
    current_ = indexAtOrAfterCursor();
    if (current_ < 0) {
        current_ = 0;
    }
    if (selectCurrent) {
        goTo(current_);
    } else {
        updateStatus();
    }
}

void FindReplaceBar::goTo(int index)
{
    if (index < 0 || index >= static_cast<int>(ranges_.size()) || view_ == nullptr) {
        current_ = -1;
        updateStatus();
        return;
    }
    current_ = index;
    const KTextEditor::Range range = ranges_.at(index);
    lastApplied_ = range;
    view_->setCursorPosition(range.start());
    view_->setSelection(range);
    updateStatus();
}

void FindReplaceBar::findNext()
{
    if (view_ == nullptr || view_->document() == nullptr) {
        return;
    }
    if (projectScope()) {
        requestProjectSearch();
        return;
    }
    if (findEdit_->text().isEmpty()) {
        openFind();
        return;
    }
    if (dirty_) {
        recompute(false);
    }
    if (ranges_.isEmpty()) {
        updateStatus();
        return;
    }
    int next = -1;
    if (current_ >= 0 && current_ < static_cast<int>(ranges_.size()) &&
        ranges_.at(current_) == lastApplied_) {
        next = current_ + 1;
    } else {
        next = indexAtOrAfterCursor();
    }
    if (next < 0 || next >= static_cast<int>(ranges_.size())) {
        next = 0;
    }
    goTo(next);
}

void FindReplaceBar::findPrevious()
{
    if (view_ == nullptr || view_->document() == nullptr) {
        return;
    }
    if (projectScope()) {
        requestProjectSearch();
        return;
    }
    if (findEdit_->text().isEmpty()) {
        openFind();
        return;
    }
    if (dirty_) {
        recompute(false);
    }
    if (ranges_.isEmpty()) {
        updateStatus();
        return;
    }
    int previousIndex = -1;
    if (current_ >= 0 && current_ < static_cast<int>(ranges_.size()) &&
        ranges_.at(current_) == lastApplied_) {
        previousIndex = current_ - 1;
    } else {
        previousIndex = indexAtOrBeforeCursor();
    }
    if (previousIndex < 0) {
        previousIndex = static_cast<int>(ranges_.size()) - 1;
    }
    goTo(previousIndex);
}

void FindReplaceBar::replaceCurrent()
{
    KTextEditor::Document *document = view_ != nullptr ? view_->document() : nullptr;
    if (document == nullptr || findEdit_ == nullptr) {
        return;
    }
    if (projectScope()) {
        // Project replacement is the dock's two-button flow (replace one /
        // replace all with a confirmation), never a silent in-document edit.
        transient_ = tr("Project replace runs from the Search panel");
        updateStatus();
        return;
    }
    if (findEdit_->text().isEmpty()) {
        openFind();
        return;
    }
    if (dirty_) {
        recompute(false);
    }
    if (ranges_.isEmpty()) {
        updateStatus();
        return;
    }
    int index = current_;
    if (index < 0 || index >= static_cast<int>(ranges_.size())) {
        index = indexAtOrAfterCursor();
    }
    if (index < 0) {
        index = 0;
    }
    const KTextEditor::Range target = ranges_.at(index);
    suppressRefresh_ = true;
    const ReplaceResult result = replaceOne(document, target, replaceEdit_->text());
    suppressRefresh_ = false;
    dirty_ = true;
    recompute(false);
    if (!result.ok) {
        error_ = result.error;
        updateStatus();
        return;
    }
    if (!ranges_.isEmpty()) {
        goTo(std::min(index, static_cast<int>(ranges_.size()) - 1));
    }
    transient_ = tr("Replaced 1 match");
    updateStatus();
}

void FindReplaceBar::replaceAllMatches()
{
    KTextEditor::Document *document = view_ != nullptr ? view_->document() : nullptr;
    if (document == nullptr || findEdit_ == nullptr) {
        return;
    }
    if (projectScope()) {
        transient_ = tr("Project replace runs from the Search panel");
        updateStatus();
        return;
    }
    if (findEdit_->text().isEmpty()) {
        openFind();
        return;
    }
    suppressRefresh_ = true;
    const ReplaceResult result = replaceAll(document, findEdit_->text(), replaceEdit_->text(),
                                            options(), selectionScope());
    suppressRefresh_ = false;
    dirty_ = true;
    recompute(false);
    if (!result.ok) {
        error_ = result.error;
        updateStatus();
        return;
    }
    transient_ = tr("Replaced %1 of %2 matches").arg(result.replaced).arg(result.found);
    updateStatus();
}

void FindReplaceBar::updateStatus()
{
    if (status_ == nullptr) {
        return;
    }
    if (!error_.isEmpty()) {
        status_->setText(error_);
        return;
    }
    if (!transient_.isEmpty()) {
        status_->setText(transient_);
        return;
    }
    if (projectScope()) {
        // The honest state: this bar does not search the project; the dock does.
        status_->setText(tr("Whole project → Search panel"));
        return;
    }
    if (findEdit_ == nullptr || findEdit_->text().isEmpty()) {
        status_->setText(tr("Type to search"));
        return;
    }
    QString text;
    if (ranges_.isEmpty()) {
        text = tr("No matches");
    } else if (current_ >= 0 && current_ < static_cast<int>(ranges_.size())) {
        text = tr("%1 / %2").arg(current_ + 1).arg(ranges_.size());
    } else {
        text = tr("%1 matches").arg(ranges_.size());
    }
    if (hasScope_ && scope_.isValid() && scope_.start() != scope_.end()) {
        text += tr(" in selection");
    }
    status_->setText(text);
}

FindOptions FindReplaceBar::options() const
{
    FindOptions result;
    result.caseSensitive = caseButton_ != nullptr && caseButton_->isChecked();
    result.wholeWords = wholeButton_ != nullptr && wholeButton_->isChecked();
    result.regex = regexButton_ != nullptr && regexButton_->isChecked();
    result.scope = scope();
    return result;
}

vedit::search::SearchScope FindReplaceBar::scope() const
{
    if (scopeCombo_ == nullptr) {
        return vedit::search::SearchScope::CurrentFile;
    }
    return static_cast<vedit::search::SearchScope>(scopeCombo_->currentData().toInt());
}

bool FindReplaceBar::projectScope() const
{
    return scope() == vedit::search::SearchScope::WholeProject;
}

vedit::search::QueryOptions FindReplaceBar::queryOptions() const
{
    vedit::search::QueryOptions options;
    options.caseSensitive = caseButton_ != nullptr && caseButton_->isChecked();
    options.wholeWords = wholeButton_ != nullptr && wholeButton_->isChecked();
    options.regex = regexButton_ != nullptr && regexButton_->isChecked();
    options.scope = scope();
    return options;
}

vedit::search::SearchHandoff FindReplaceBar::handoff() const
{
    vedit::search::SearchHandoff result;
    result.text = pattern();
    result.options = queryOptions();
    return result;
}

void FindReplaceBar::setScope(vedit::search::SearchScope scope)
{
    if (scopeCombo_ == nullptr) {
        return;
    }
    const int index = scopeCombo_->findData(static_cast<int>(scope));
    if (index < 0) {
        return;
    }
    // The user path: changing the selector runs its handler (which publishes
    // the hand-off and, for whole project, asks the dock to search). A no-op
    // change keeps the current state.
    scopeCombo_->setCurrentIndex(index);
}

void FindReplaceBar::setScopeWidgets(vedit::search::SearchScope scope)
{
    if (scopeCombo_ == nullptr) {
        return;
    }
    const int index = scopeCombo_->findData(static_cast<int>(scope));
    if (index < 0 || index == scopeCombo_->currentIndex()) {
        return;
    }
    // The programmatic update must not re-enter requestProjectSearch().
    const bool previous = syncingHub_;
    syncingHub_ = true;
    scopeCombo_->setCurrentIndex(index);
    syncingHub_ = previous;
}

void FindReplaceBar::publishToHub()
{
    if (syncingHub_) {
        return;
    }
    // `publishFromFindBar` keeps a separate snapshot so a newly opened search
    // dock can replay the bar's live query without inheriting another dock's
    // last search (b2-repair Medium 8).
    vedit::search::SearchHub::instance().publishFromFindBar(handoff(), this);
}

void FindReplaceBar::adoptFromHub(const vedit::search::SearchHandoff &handoff)
{
    const bool previous = syncingHub_;
    syncingHub_ = true;
    if (findEdit_ != nullptr && findEdit_->text() != handoff.text) {
        findEdit_->setText(handoff.text);
        dirty_ = true;
    }
    if (wholeButton_ != nullptr) {
        wholeButton_->setChecked(handoff.options.wholeWords);
    }
    if (caseButton_ != nullptr) {
        caseButton_->setChecked(handoff.options.caseSensitive);
    }
    if (regexButton_ != nullptr) {
        regexButton_->setChecked(handoff.options.regex);
    }
    setScopeWidgets(handoff.options.scope);
    if (selectionButton_ != nullptr) {
        selectionButton_->setEnabled(!projectScope());
    }
    syncingHub_ = previous;
    if (isVisible()) {
        recompute(false);
    }
}

void FindReplaceBar::requestProjectSearch()
{
    if (!projectScope()) {
        return;
    }
    const vedit::search::SearchHandoff request = handoff();
    if (request.text.trimmed().isEmpty()) {
        transient_ = tr("Type a query to search the project");
        updateStatus();
        return;
    }
    vedit::search::SearchHub::instance().setCurrentFilePath(documentPath(view_));
    emit projectSearchRequested(request);
    const bool delivered =
        vedit::search::SearchHub::instance().requestProjectSearch(request);
    transient_ = delivered
                     ? tr("Whole project: searching in the Search panel")
                     : tr("Whole project: open the Search panel to search the project");
    updateStatus();
}

const KTextEditor::Range *FindReplaceBar::selectionScope() const
{
    if (!hasScope_ || !scope_.isValid() || scope_.start() == scope_.end()) {
        return nullptr;
    }
    return &scope_;
}

int FindReplaceBar::indexAtOrAfterCursor() const
{
    if (view_ == nullptr) {
        return -1;
    }
    const KTextEditor::Cursor cursor = view_->cursorPosition();
    for (int index = 0; index < static_cast<int>(ranges_.size()); ++index) {
        // `atOrAfter(start, cursor)` is start >= cursor: a match that starts
        // exactly at the cursor is the current one, not the next one.
        if (atOrAfter(ranges_.at(index).start(), cursor)) {
            return index;
        }
    }
    return -1;
}

int FindReplaceBar::indexAtOrBeforeCursor() const
{
    if (view_ == nullptr) {
        return -1;
    }
    const KTextEditor::Cursor cursor = view_->cursorPosition();
    for (int index = static_cast<int>(ranges_.size()) - 1; index >= 0; --index) {
        // `atOrAfter(cursor, start)` is cursor >= start: start <= cursor.
        if (atOrAfter(cursor, ranges_.at(index).start())) {
            return index;
        }
    }
    return -1;
}

QString FindReplaceBar::pattern() const
{
    return findEdit_ != nullptr ? findEdit_->text() : QString();
}

QString FindReplaceBar::replacement() const
{
    return replaceEdit_ != nullptr ? replaceEdit_->text() : QString();
}

QString FindReplaceBar::statusText() const
{
    return status_ != nullptr ? status_->text() : QString();
}

int FindReplaceBar::matchCount() const
{
    return static_cast<int>(ranges_.size());
}

int FindReplaceBar::currentMatchIndex() const
{
    return current_;
}

bool FindReplaceBar::replaceRowVisible() const
{
    return replaceRow_ != nullptr && replaceRow_->isVisibleTo(this);
}

bool FindReplaceBar::inSelection() const
{
    return hasScope_ && scope_.isValid() && scope_.start() != scope_.end();
}

bool FindReplaceBar::caseSensitive() const
{
    return caseButton_ != nullptr && caseButton_->isChecked();
}

bool FindReplaceBar::wholeWords() const
{
    return wholeButton_ != nullptr && wholeButton_->isChecked();
}

bool FindReplaceBar::regex() const
{
    return regexButton_ != nullptr && regexButton_->isChecked();
}

KTextEditor::View *FindReplaceBar::view() const
{
    return view_;
}

void FindReplaceBar::setPattern(const QString &pattern)
{
    if (findEdit_ == nullptr) {
        return;
    }
    findEdit_->setText(pattern);
    dirty_ = true;
    if (isVisible()) {
        recompute(false);
    }
}

void FindReplaceBar::setReplacement(const QString &text)
{
    if (replaceEdit_ != nullptr) {
        replaceEdit_->setText(text);
    }
}

void FindReplaceBar::setCaseSensitive(bool enabled)
{
    if (caseButton_ != nullptr) {
        caseButton_->setChecked(enabled);
    }
}

void FindReplaceBar::setWholeWords(bool enabled)
{
    if (wholeButton_ != nullptr) {
        wholeButton_->setChecked(enabled);
    }
}

void FindReplaceBar::setRegex(bool enabled)
{
    if (regexButton_ != nullptr) {
        regexButton_->setChecked(enabled);
    }
}

void FindReplaceBar::applyHandoff(const vedit::search::SearchHandoff &handoff)
{
    adoptFromHub(handoff);
}

void FindReplaceBar::setInSelection(bool enabled)
{
    if (selectionButton_ == nullptr) {
        return;
    }
    if (selectionButton_->isChecked() == enabled) {
        applySelectionToggle(enabled);
        return;
    }
    selectionButton_->setChecked(enabled);
}

} // namespace vedit::editor
