#include "editor_advanced.h"
#include <QCoreApplication>

#include "lsp_actions.h"
#include "multi_cursor.h"
#include "signature_help.h"

#include <KTextEditor/Cursor>
#include <KTextEditor/Document>
#include <KTextEditor/View>

#include <QAction>
#include <QChildEvent>
#include <QEvent>
#include <QKeyEvent>
#include <QMetaObject>
#include <QPair>
#include <QPoint>
#include <QWidget>

#include <cstdio>

namespace vedit::editor {
namespace {

constexpr auto kObjectName = "vedit-editor-advanced";

/// One machine line per applied/refused feature (the probe reads these).
void logAdvancedLine(const QString &line)
{
    // The shared logger keeps the VEDIT_QUIET rule in one place.
    logEditorActionLine(line);
}

const QList<AdvancedFeature> &featureOrder()
{
    static const QList<AdvancedFeature> order{
        AdvancedFeature::LspCompletion,  AdvancedFeature::KeywordCompletion,
        AdvancedFeature::SignatureHelp,  AdvancedFeature::CodeAction,
        AdvancedFeature::StickyScroll,   AdvancedFeature::InlayHints,
        AdvancedFeature::Snippets,       AdvancedFeature::IndentGuides,
        AdvancedFeature::Whitespace,     AdvancedFeature::ColumnRuler};
    return order;
}

} // namespace

QString advancedFeatureName(AdvancedFeature feature)
{
    switch (feature) {
    case AdvancedFeature::LspCompletion:
        return QStringLiteral("lsp-completion");
    case AdvancedFeature::KeywordCompletion:
        return QStringLiteral("keyword-completion");
    case AdvancedFeature::SignatureHelp:
        return QStringLiteral("signature-help");
    case AdvancedFeature::CodeAction:
        return QStringLiteral("code-action");
    case AdvancedFeature::StickyScroll:
        return QStringLiteral("sticky-scroll");
    case AdvancedFeature::InlayHints:
        return QStringLiteral("inlay-hints");
    case AdvancedFeature::Snippets:
        return QStringLiteral("snippets");
    case AdvancedFeature::IndentGuides:
        return QStringLiteral("indent-guides");
    case AdvancedFeature::Whitespace:
        return QStringLiteral("whitespace");
    case AdvancedFeature::ColumnRuler:
        return QStringLiteral("column-ruler");
    }
    return QString();
}

QString advancedFeatureLabel(AdvancedFeature feature)
{
    switch (feature) {
    case AdvancedFeature::LspCompletion:
        return QCoreApplication::translate("VEdit", "LSP Completion");
    case AdvancedFeature::KeywordCompletion:
        return QCoreApplication::translate("VEdit", "Keyword Completion (KTextEditor)");
    case AdvancedFeature::SignatureHelp:
        return QCoreApplication::translate("VEdit", "Signature Help (editor-synthesised)");
    case AdvancedFeature::CodeAction:
        return QCoreApplication::translate("VEdit", "Code Actions");
    case AdvancedFeature::StickyScroll:
        return QCoreApplication::translate("VEdit", "Sticky Scroll");
    case AdvancedFeature::InlayHints:
        return QCoreApplication::translate("VEdit", "Inlay Hints");
    case AdvancedFeature::Snippets:
        return QCoreApplication::translate("VEdit", "Snippets");
    case AdvancedFeature::IndentGuides:
        return QCoreApplication::translate("VEdit", "Indent Guides");
    case AdvancedFeature::Whitespace:
        return QCoreApplication::translate("VEdit", "Show Whitespace");
    case AdvancedFeature::ColumnRuler:
        return QCoreApplication::translate("VEdit", "Column Ruler");
    }
    return QString();
}

QString advancedFeatureGroup(AdvancedFeature feature)
{
    switch (feature) {
    case AdvancedFeature::LspCompletion:
    case AdvancedFeature::KeywordCompletion:
    case AdvancedFeature::SignatureHelp:
    case AdvancedFeature::CodeAction:
        return QStringLiteral("1 lsp-completion-signature-code-action");
    case AdvancedFeature::StickyScroll:
        return QStringLiteral("2 sticky-scroll");
    case AdvancedFeature::InlayHints:
        return QStringLiteral("3 inlay-hints");
    case AdvancedFeature::Snippets:
        return QStringLiteral("4 snippets");
    case AdvancedFeature::IndentGuides:
    case AdvancedFeature::Whitespace:
    case AdvancedFeature::ColumnRuler:
        return QStringLiteral("5 indent-guides-whitespace-column-ruler");
    }
    return QString();
}

EditorAdvanced::EditorAdvanced(KTextEditor::View *view)
    : QObject(view)
    , view_(view)
    , applied_(featureOrder().size(), 0)
{
    setObjectName(QString::fromLatin1(kObjectName));
}

EditorAdvanced *EditorAdvanced::install(KTextEditor::View *view)
{
    if (view == nullptr) {
        return nullptr;
    }
    if (EditorAdvanced *existing = instanceFor(view)) {
        return existing;
    }
    auto *advanced = new EditorAdvanced(view);
    installEditorEventFilter(view, advanced);
    // Apply the one feature that is an editor *option* rather than a command, so
    // it is on for every pane: keyword completion is what KTextEditor can do
    // without an LSP completion request.
    if (advanced->availability(AdvancedFeature::KeywordCompletion).enabled) {
        view->setConfigValue(QStringLiteral("keyword-completion"), true);
    }
    return advanced;
}

EditorAdvanced *EditorAdvanced::instanceFor(const KTextEditor::View *view)
{
    return view != nullptr ? view->findChild<EditorAdvanced *>(QString::fromLatin1(kObjectName))
                           : nullptr;
}

int EditorAdvanced::indexOf(AdvancedFeature feature)
{
    return static_cast<int>(feature);
}

QList<AdvancedFeature> EditorAdvanced::features() const
{
    return featureOrder();
}

QStringList EditorAdvanced::configKeys() const
{
    return view_ != nullptr ? view_->configKeys() : QStringList();
}

FeatureState EditorAdvanced::availability(AdvancedFeature feature) const
{
    FeatureState state;
    switch (feature) {
    case AdvancedFeature::KeywordCompletion: {
        const bool supported = configKeys().contains(QStringLiteral("keyword-completion"));
        state.source = QStringLiteral("ktexteditor");
        state.enabled = supported;
        if (!supported) {
            state.reason = QStringLiteral(
                "this KTextEditor build does not advertise the 'keyword-completion' view option");
        }
        return state;
    }
    case AdvancedFeature::SignatureHelp: {
        state.source = QStringLiteral("editor");
        state.enabled = SignatureHelp::instanceFor(view_) != nullptr;
        if (!state.enabled) {
            state.reason = QStringLiteral("the signatureHelp synthesiser is not installed on this view");
        }
        return state;
    }
    case AdvancedFeature::Snippets:
        state.source = QStringLiteral("editor");
        state.enabled = view_ != nullptr && view_->document() != nullptr;
        if (!state.enabled) {
            state.reason = QStringLiteral("no document open in this editor");
        }
        return state;
    case AdvancedFeature::LspCompletion:
        state.source = QStringLiteral("abi");
        state.reason = QStringLiteral(
            "no completion request in the C ABI (the LSP session serves definition / references / "
            "symbols / rename / formatting / diagnostics / hover / signatureHelp) and KTextEditor "
            "6.24 exposes no public completion entry point; keyword completion is the supported "
            "substitute");
        break;
    case AdvancedFeature::CodeAction:
        state.source = QStringLiteral("abi");
        state.reason = QStringLiteral(
            "no textDocument/codeAction request in the C ABI, so the menu item would have to fake "
            "its result");
        break;
    case AdvancedFeature::StickyScroll:
        state.source = QStringLiteral("ktexteditor");
        state.reason = QStringLiteral(
            "KTextEditor 6.24 has no sticky-scroll API (no such symbol in the installed headers); "
            "the pane's breadcrumb bar is what shows the enclosing scope");
        break;
    case AdvancedFeature::InlayHints:
        state.source = QStringLiteral("abi");
        state.reason = QStringLiteral(
            "no textDocument/inlayHint request in the C ABI; InlineNoteProvider exists in the "
            "kernel but there is no inlay data source to feed it");
        break;
    case AdvancedFeature::IndentGuides:
    case AdvancedFeature::Whitespace:
    case AdvancedFeature::ColumnRuler:
        state.source = QStringLiteral("ktexteditor");
        state.reason = QStringLiteral(
            "%1 is a KatePart renderer option and KTextEditor 6.24 does not expose it through "
            "View::configKeys() (measured keys: font, theme, line-numbers, folding-bar, "
            "keyword-completion, ...), so the product cannot toggle it without private KatePart "
            "APIs")
                      .arg(advancedFeatureLabel(feature));
        break;
    }
    return state;
}

bool EditorAdvanced::insertSnippet(const QString &snippetId)
{
    KTextEditor::Document *document = view_ != nullptr ? view_->document() : nullptr;
    if (document == nullptr) {
        lastRefusal_ = QStringLiteral("no document open in this editor");
        logAdvancedLine(QStringLiteral("ADVANCED-REFUSED feature=snippets snippet=%1 reason='%2'")
                            .arg(snippetId, lastRefusal_));
        return false;
    }
    QString text;
    if (snippetId == QLatin1String("module")) {
        text = QStringLiteral("module ${name} (\n  input logic clk\n);\n  \nendmodule\n");
    } else if (snippetId == QLatin1String("always_ff")) {
        text = QStringLiteral("always_ff @(posedge clk) begin\n  \nend\n");
    } else if (snippetId == QLatin1String("case")) {
        text = QStringLiteral("case (sel)\n  0: ;\n  default: ;\nendcase\n");
    } else {
        lastRefusal_ = QStringLiteral("unknown snippet '%1'").arg(snippetId);
        logAdvancedLine(QStringLiteral("ADVANCED-REFUSED feature=snippets snippet=%1 reason='%2'")
                            .arg(snippetId, lastRefusal_));
        return false;
    }
    document->insertText(view_->cursorPosition(), text);
    ++applied_[indexOf(AdvancedFeature::Snippets)];
    logAdvancedLine(QStringLiteral("ADVANCED-APPLIED feature=snippets snippet=%1 bytes=%2")
                        .arg(snippetId)
                        .arg(text.toUtf8().size()));
    return true;
}

bool EditorAdvanced::apply(AdvancedFeature feature)
{
    const FeatureState state = availability(feature);
    if (!state.enabled) {
        lastRefusal_ = state.reason;
        logAdvancedLine(QStringLiteral("ADVANCED-REFUSED feature=%1 group='%2' source=%3 reason='%4'")
                            .arg(advancedFeatureName(feature), advancedFeatureGroup(feature),
                                 state.source, state.reason));
        return false;
    }
    switch (feature) {
    case AdvancedFeature::KeywordCompletion: {
        const bool next = !view_->configValue(QStringLiteral("keyword-completion")).toBool();
        view_->setConfigValue(QStringLiteral("keyword-completion"), next);
        ++applied_[indexOf(feature)];
        logAdvancedLine(QStringLiteral("ADVANCED-APPLIED feature=keyword-completion value=%1 "
                                       "readback=%2")
                            .arg(next ? 1 : 0)
                            .arg(view_->configValue(QStringLiteral("keyword-completion")).toBool()
                                         ? 1
                                         : 0));
        return true;
    }
    case AdvancedFeature::SignatureHelp: {
        SignatureHelp *help = SignatureHelp::instanceFor(view_);
        if (help == nullptr) {
            return false;
        }
        help->refresh();
        ++applied_[indexOf(feature)];
        logAdvancedLine(QStringLiteral("ADVANCED-APPLIED feature=signature-help popup=%1 reason='%2'")
                            .arg(help->popupVisible() ? 1 : 0)
                            .arg(help->statusText()));
        return true;
    }
    case AdvancedFeature::Snippets:
        return insertSnippet(QStringLiteral("always_ff"));
    default:
        break;
    }
    lastRefusal_ = QStringLiteral("no effect is implemented for this feature");
    logAdvancedLine(QStringLiteral("ADVANCED-REFUSED feature=%1 reason='%2'")
                        .arg(advancedFeatureName(feature), lastRefusal_));
    return false;
}

QMenu *EditorAdvanced::buildMenu(QWidget *parent)
{
    if (view_ == nullptr) {
        return nullptr;
    }
    auto *menu = new QMenu(parent != nullptr ? parent : view_);
    menu->setObjectName(QStringLiteral("vedit-advanced-menu"));
    menu->setToolTipsVisible(true);
    QString currentGroup;
    for (AdvancedFeature feature : featureOrder()) {
        const QString group = advancedFeatureGroup(feature);
        if (group != currentGroup) {
            currentGroup = group;
            if (!menu->actions().isEmpty()) {
                menu->addSeparator();
            }
            if (QAction *header = menu->addSection(group)) {
                Q_UNUSED(header);
            }
        }
        const FeatureState state = availability(feature);
        auto *item = new QAction(advancedFeatureLabel(feature), menu);
        item->setObjectName(QStringLiteral("vedit.advanced.") + advancedFeatureName(feature));
        item->setEnabled(state.enabled);
        const QString tip = state.enabled
                                ? QCoreApplication::translate("VEdit", "%1 — available (%2)")
                                      .arg(advancedFeatureLabel(feature), state.source)
                                : state.reason;
        item->setToolTip(tip);
        item->setStatusTip(tip);
        if (feature == AdvancedFeature::Snippets) {
            QMenu *snippets = menu->addMenu(advancedFeatureLabel(feature));
            snippets->setObjectName(QStringLiteral("vedit.advanced.snippets"));
            snippets->setToolTipsVisible(true);
            const QList<QPair<QString, QString>> entries{
                {QStringLiteral("module"), QCoreApplication::translate("VEdit", "Module skeleton")},
                {QStringLiteral("always_ff"), QCoreApplication::translate("VEdit", "always_ff block")},
                {QStringLiteral("case"), QCoreApplication::translate("VEdit", "case statement")}};
            for (const auto &entry : entries) {
                QAction *snippet = snippets->addAction(entry.second);
                snippet->setObjectName(QStringLiteral("vedit.advanced.snippet.")
                                      + entry.first);
                QObject::connect(snippet, &QAction::triggered, this,
                                 [this, id = entry.first]() { insertSnippet(id); });
            }
            continue;
        }
        QObject::connect(item, &QAction::triggered, this,
                         [this, feature]() { apply(feature); });
        menu->addAction(item);
    }
    lastMenu_ = menu;
    return menu;
}

QMenu *EditorAdvanced::showMenu(const QPoint &globalPos)
{
    QMenu *menu = buildMenu();
    if (menu == nullptr) {
        return nullptr;
    }
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->popup(globalPos);
    return menu;
}

int EditorAdvanced::applyCount(AdvancedFeature feature) const
{
    return applied_.value(indexOf(feature));
}

bool EditorAdvanced::eventFilter(QObject *watched, QEvent *event)
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
    const auto isMenuKey = [](QKeyEvent *key) -> bool {
        return key->key() == Qt::Key_A && key->modifiers().testFlag(Qt::ControlModifier)
               && key->modifiers().testFlag(Qt::AltModifier);
    };
    switch (event->type()) {
    case QEvent::ShortcutOverride: {
        auto *key = static_cast<QKeyEvent *>(event);
        if (isMenuKey(key)) {
            event->accept();
            return true;
        }
        break;
    }
    case QEvent::KeyPress: {
        auto *key = static_cast<QKeyEvent *>(event);
        if (isMenuKey(key)) {
            const QPoint point = view_->cursorToCoordinate(view_->cursorPosition());
            showMenu(view_->mapToGlobal(point + QPoint(8, 18)));
            return true;
        }
        break;
    }
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

} // namespace vedit::editor
