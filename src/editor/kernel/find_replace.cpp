#include "find_replace.h"

#include "multi_cursor.h"
#include "../../workspace/search/search_history_button.h"

#include <KTextEditor/Document>
#include <KTextEditor/View>

#include <QAction>
#include <QChar>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QEvent>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QShortcut>
#include <QSignalBlocker>
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

// The overlay keeps this distance from the viewport edges and is never moved
// (or resized) outside it (AC-8.1).
constexpr int kOverlayMargin = 10;

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

    // The find row is a grid, not a fixed row: at narrow widths the controls
    // reflow onto extra rows so every option and the close button stay
    // reachable (AC-8.2). `layoutFindRow()` owns the placement.
    findRow_ = new QWidget(this);
    findGrid_ = new QGridLayout(findRow_);
    findGrid_->setContentsMargins(0, 0, 0, 0);
    findGrid_->setSpacing(4);
    findGrid_->setColumnStretch(0, 1);
    findInput_ = new QWidget(findRow_);
    auto *inputLayout = new QHBoxLayout(findInput_);
    inputLayout->setContentsMargins(0, 0, 0, 0);
    inputLayout->setSpacing(4);
    findEdit_ = new QLineEdit(findInput_);
    findEdit_->setObjectName(QStringLiteral("vedit-find-pattern"));
    findEdit_->setPlaceholderText(tr("Find"));
    findEdit_->setClearButtonEnabled(true);
    findEdit_->setMinimumWidth(200);
    inputLayout->addWidget(findEdit_, 1);
    inputLayout->addWidget(vedit::search::createHistoryButton(findEdit_, findInput_));

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

    previousButton_ = new QToolButton(this);
    previousButton_->setObjectName(QStringLiteral("vedit-find-prev"));
    previousButton_->setText(tr("Prev"));
    previousButton_->setToolTip(tr("Previous match (Shift+F3)"));
    nextButton_ = new QToolButton(this);
    nextButton_->setObjectName(QStringLiteral("vedit-find-next"));
    nextButton_->setText(tr("Next"));
    nextButton_->setToolTip(tr("Next match (F3)"));

    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("vedit-find-status"));
    status_->setMinimumWidth(90);

    closeButton_ = new QToolButton(this);
    closeButton_->setObjectName(QStringLiteral("vedit-find-close"));
    closeButton_->setText(QStringLiteral("x"));
    closeButton_->setToolTip(tr("Close (Esc)"));
    closeButton_->setAutoRaise(true);

    // Replace disclosure (batch 20261110d): the replace row is only reachable
    // through Ctrl+H today; this chevron lets a user reveal it from the bar
    // itself. Collapsed by default on every open (`openBar()` resets it), and
    // the state drives `replaceRowVisible()` so the two never disagree.
    disclosureButton_ = new QToolButton(this);
    disclosureButton_->setObjectName(QStringLiteral("vedit-find-disclosure"));
    disclosureButton_->setCheckable(true);
    disclosureButton_->setAutoRaise(true);
    disclosureButton_->setChecked(false);
    disclosureButton_->setArrowType(Qt::RightArrow);
    disclosureButton_->setToolTip(tr("Show replace options"));
    outer->addWidget(findRow_);

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
    connect(findEdit_, &QLineEdit::returnPressed, this, [this]() { findFromField(false); });
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
    connect(previousButton_, &QToolButton::clicked, this, [this]() { findPrevious(); });
    connect(nextButton_, &QToolButton::clicked, this, [this]() { findNext(); });
    connect(replaceOneButton, &QToolButton::clicked, this, [this]() { replaceCurrent(); });
    connect(replaceAllButton, &QToolButton::clicked, this, [this]() { replaceAllMatches(); });
    connect(closeButton_, &QToolButton::clicked, this, [this]() { closeBar(); });
    connect(disclosureButton_, &QToolButton::toggled, this, [this](bool expanded) {
        if (disclosureButton_ != nullptr) {
            disclosureButton_->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
            disclosureButton_->setToolTip(expanded ? tr("Hide replace options")
                                                   : tr("Show replace options"));
        }
        if (replaceRow_ != nullptr) {
            replaceRow_->setVisible(expanded);
        }
        // The two forms differ by a row: re-measure, then keep the bar inside
        // the viewport at its new height.
        if (isVisible()) {
            adjustSize();
            positionOverlay();
        }
    });

    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, [this]() { closeBar(); });

    layoutFindRow(false);
    // The wide form's PREFERRED width is the reflow threshold (batch 20261110d):
    // `minimumSizeHint().width()` only reported the field's 200 px minimum, so
    // the bar stayed in the one-row form long after the controls no longer fit
    // (the reported "~700 px preferred, measured ~minimum" defect). Measured
    // from the widgets, never a guessed pixel count (AC-8.2).
    wideMinimumWidth_ = findRow_->sizeHint().width();
    wideLayoutPreferredWidth_ = wideMinimumWidth_;
    wideLayoutMinimumWidth_ = findRow_->minimumSizeHint().width();
    // The frame background is the drag handle (AC-8.1), so the bar advertises
    // the grab cursor; controls keep an ordinary pointer so a hand never
    // promises a drag over a button.
    setCursor(Qt::OpenHandCursor);
    for (QToolButton *button : findChildren<QToolButton *>()) {
        button->setCursor(Qt::ArrowCursor);
    }
    if (scopeCombo_ != nullptr) {
        scopeCombo_->setCursor(Qt::ArrowCursor);
    }
    for (QLineEdit *field : findChildren<QLineEdit *>()) {
        field->setCursor(Qt::IBeamCursor);
    }
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
    openBar();
}

void FindReplaceBar::openReplace()
{
    openBar();
}

// Batch 20261110d: BOTH entry points open the bar with the replace row
// COLLAPSED. Ctrl+H keeps its shortcut and still opens the bar, but the replace
// row is revealed by the disclosure chevron instead of appearing silently — the
// default state is folded, and `openReplace()` does not override it.
void FindReplaceBar::openBar()
{
    if (view_ == nullptr || view_->document() == nullptr) {
        return;
    }
    // The dock's Current-File scope searches the document this bar belongs to.
    const QString path = documentPath(view_);
    if (!path.isEmpty()) {
        vedit::search::SearchHub::instance().setCurrentFilePath(path);
    }
    // Default folded on every open; the chevron and the row are set together so
    // `replaceRowVisible()` can never disagree with the button.
    {
        const QSignalBlocker blocker(disclosureButton_);
        disclosureButton_->setChecked(false);
        disclosureButton_->setArrowType(Qt::RightArrow);
        disclosureButton_->setToolTip(tr("Show replace options"));
    }
    replaceRow_->setVisible(false);
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
    // Esc and the close button hand the keyboard back to the editor (AC-8.4);
    // the view forwards focus to KatePart's inner widget.
    if (view_ != nullptr) {
        view_->setFocus(Qt::OtherFocusReason);
    }
}

void FindReplaceBar::positionOverlay()
{
    if (view_ == nullptr) {
        return;
    }
    // The overlay may never need more width than the viewport leaves it, and
    // the form it chooses depends on that width (AC-8.1/AC-8.2).
    const int available = std::max(240, view_->width() - 2 * kOverlayMargin);
    setMaximumWidth(available);
    layoutFindRow(wideMinimumWidth_ > 0 && available < wideMinimumWidth_);
    adjustSize();
    move(clampToViewport(customPosition_ ? customPositionValue_ : defaultPosition()));
}

QPoint FindReplaceBar::clampToViewport(const QPoint &position) const
{
    if (view_ == nullptr) {
        return position;
    }
    const int maxX = std::max(0, view_->width() - width());
    const int maxY = std::max(0, view_->height() - height());
    return QPoint(std::clamp(position.x(), 0, maxX), std::clamp(position.y(), 0, maxY));
}

QPoint FindReplaceBar::defaultPosition() const
{
    if (view_ == nullptr) {
        return QPoint(kOverlayMargin, kOverlayMargin);
    }
    return QPoint(std::max(kOverlayMargin, view_->width() - width() - kOverlayMargin),
                  kOverlayMargin);
}

bool FindReplaceBar::isCustomPosition() const
{
    return customPosition_;
}

bool FindReplaceBar::compactLayout() const
{
    return compact_;
}

void FindReplaceBar::restoreDefaultPosition()
{
    customPosition_ = false;
    if (isVisible()) {
        positionOverlay();
    }
}

void FindReplaceBar::layoutFindRow(bool compact)
{
    if (findGrid_ == nullptr || findEdit_ == nullptr || caseButton_ == nullptr ||
        wholeButton_ == nullptr || regexButton_ == nullptr || selectionButton_ == nullptr ||
        scopeCombo_ == nullptr || previousButton_ == nullptr || nextButton_ == nullptr ||
        status_ == nullptr || closeButton_ == nullptr) {
        return;
    }
    if (findRowLaidOut_ && compact_ == compact) {
        return;
    }
    findRowLaidOut_ = true;
    compact_ = compact;
    // Moving a widget between grid cells is `addWidget` again: Qt relocates an
    // existing layout item, it does not duplicate it.
    if (compact) {
        // Narrow form (AC-8.2): the field keeps the first row with the close
        // button, the options reflow below, and Prev/Next/status share the
        // third row. Every control stays on screen at 360 px.
        findEdit_->setMinimumWidth(120);
        findGrid_->addWidget(findInput_, 0, 0, 1, 4);
        findGrid_->addWidget(closeButton_, 0, 4);
        findGrid_->addWidget(caseButton_, 1, 0);
        findGrid_->addWidget(wholeButton_, 1, 1);
        findGrid_->addWidget(regexButton_, 1, 2);
        findGrid_->addWidget(scopeCombo_, 1, 3, 1, 2);
        findGrid_->addWidget(selectionButton_, 1, 5);
        findGrid_->addWidget(previousButton_, 2, 0);
        findGrid_->addWidget(nextButton_, 2, 1);
        findGrid_->addWidget(status_, 2, 2, 1, 3);
        findGrid_->addWidget(disclosureButton_, 0, 5);
        for (int column = 1; column < 6; ++column) {
            findGrid_->setColumnStretch(column, 0);
        }
        findGrid_->setColumnStretch(0, 1);
    } else {
        findEdit_->setMinimumWidth(200);
        findGrid_->addWidget(findInput_, 0, 0);
        findGrid_->addWidget(caseButton_, 0, 1);
        findGrid_->addWidget(wholeButton_, 0, 2);
        findGrid_->addWidget(regexButton_, 0, 3);
        findGrid_->addWidget(scopeCombo_, 0, 4);
        findGrid_->addWidget(selectionButton_, 0, 5);
        findGrid_->addWidget(previousButton_, 0, 6);
        findGrid_->addWidget(nextButton_, 0, 7);
        findGrid_->addWidget(status_, 0, 8);
        findGrid_->addWidget(closeButton_, 0, 9);
        findGrid_->addWidget(disclosureButton_, 0, 10);
        for (int column = 1; column <= 10; ++column) {
            findGrid_->setColumnStretch(column, 0);
        }
        findGrid_->setColumnStretch(0, 1);
    }

    // Pin the find row's height after EVERY form switch (batch 20261110d): the
    // grid moves controls between rows and the bar had no minimum height, so a
    // switch could let the rows overlap while the geometry settled. Measured
    // again on each switch, so the wide form releases the compact form's extra
    // height instead of keeping it forever.
    findGrid_->activate();
    const int rowHeight = findRow_->sizeHint().height();
    if (rowHeight > 0) {
        findRow_->setMinimumHeight(rowHeight);
    }
}

void FindReplaceBar::findFromField(bool backwards)
{
    // In project scope the Enter key runs the project search through the dock
    // (the bar does not search the tree itself).
    if (projectScope()) {
        requestProjectSearch();
    } else if (backwards) {
        findPrevious();
    } else {
        findNext();
    }
    // Return searches; it does not hand the keyboard to the document (AC-8.3).
    // The field is only re-focused when something took it, so ordinary typing
    // is never disturbed.
    if (findEdit_ != nullptr && !findEdit_->hasFocus()) {
        findEdit_->setFocus(Qt::OtherFocusReason);
    }
}

void FindReplaceBar::replaceFromField(bool backwards)
{
    // Return in the replace field replaces the CURRENT match and continues
    // walking forward (`replaceCurrent()` lands on the next match once the
    // replaced one is gone); Shift+Return replaces it and steps back one match.
    // Both arms are claimed by the event filter, so the key never reaches the
    // document (a Return there would insert a newline — the F1 defect).
    replaceCurrent();
    if (backwards) {
        findPrevious();
    }
    if (replaceEdit_ != nullptr && !replaceEdit_->hasFocus()) {
        replaceEdit_->setFocus(Qt::OtherFocusReason);
    }
}

void FindReplaceBar::mousePressEvent(QMouseEvent *event)
{
    // A press that reaches the frame landed on its background: every control is
    // a child widget and handles its own press, so the background is exactly
    // the drag handle (AC-8.1).
    if (event->button() != Qt::LeftButton || view_ == nullptr) {
        QFrame::mousePressEvent(event);
        return;
    }
    dragging_ = true;
    dragMoved_ = false;
    dragStartGlobal_ = event->globalPosition().toPoint();
    dragStartPosition_ = pos();
    raise();
    event->accept();
}

void FindReplaceBar::mouseMoveEvent(QMouseEvent *event)
{
    if (!dragging_) {
        QFrame::mouseMoveEvent(event);
        return;
    }
    const QPoint delta = event->globalPosition().toPoint() - dragStartGlobal_;
    if (!delta.isNull()) {
        dragMoved_ = true;
    }
    move(clampToViewport(dragStartPosition_ + delta));
    event->accept();
}

void FindReplaceBar::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !dragging_) {
        QFrame::mouseReleaseEvent(event);
        return;
    }
    dragging_ = false;
    if (dragMoved_) {
        // The position belongs to this view for the rest of its session: the
        // bar reopens where the user left it until they reset it.
        customPosition_ = true;
        customPositionValue_ = pos();
    }
    event->accept();
}

void FindReplaceBar::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    QAction *reset = menu.addAction(tr("Reset position"));
    reset->setEnabled(customPosition_);
    connect(reset, &QAction::triggered, this, [this]() { restoreDefaultPosition(); });
    menu.exec(event->globalPos());
    event->accept();
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
    if ((watched == findEdit_ || watched == replaceEdit_) &&
        (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)) {
        auto *key = static_cast<QKeyEvent *>(event);
        const bool returnKey = key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter;
        const Qt::KeyboardModifiers modifiers =
            key->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier |
                                Qt::MetaModifier);
        if (returnKey && (modifiers == Qt::NoModifier || modifiers == Qt::ShiftModifier)) {
            // BOTH fields are claimed, not just the find field. QLineEdit ignores
            // the key AFTER emitting returnPressed, and an ignored key propagates
            // up the parent chain into KatePart, where EditorEnhancements'
            // Return handling inserts a newline at the document cursor. That is
            // how a Return in the REPLACE field used to corrupt the document
            // (`alpha alpha` -> `X \nalpha`): no claim existed there at all.
            // Find field: Return = find-next, Shift+Return = find-previous.
            // Replace field: Return = replace the current match and keep
            // walking forward, Shift+Return = replace it and step back one
            // match (see replaceFromField()).
            if (event->type() == QEvent::KeyPress) {
                if (watched == replaceEdit_) {
                    replaceFromField(modifiers == Qt::ShiftModifier);
                } else {
                    findFromField(modifiers == Qt::ShiftModifier);
                }
            }
            event->accept();
            return true;
        }
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
    // Batch 20261110d: a RUN search enters the shared bounded history. F3/Enter
    // both land here; a repeat is deduped by the hub, not appended again.
    vedit::search::SearchHub::instance().noteQuery(findEdit_->text());
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
    vedit::search::SearchHub::instance().noteQuery(findEdit_->text());
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
    vedit::search::SearchHub::instance().noteQuery(findEdit_->text());
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
    vedit::search::SearchHub::instance().noteQuery(findEdit_->text());
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
                     ? tr("Whole project query sent to the Search panel")
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
