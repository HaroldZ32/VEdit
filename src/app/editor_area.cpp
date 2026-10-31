#include "editor_area.h"

#include "editor_provider.h"

#include <QEvent>
#include <QFont>
#include <QJsonValue>
#include <QLabel>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QString>
#include <QVBoxLayout>

#include <algorithm>

namespace vedit::app {

struct EditorArea::Node {
    enum class Kind { Leaf, Split };

    Kind kind = Kind::Leaf;
    EditorPane *pane = nullptr;
    Qt::Orientation orientation = Qt::Horizontal;
    double ratio = 0.5;
    std::unique_ptr<Node> first;
    std::unique_ptr<Node> second;
};

// ---------------------------------------------------------------------------
// EditorPane
// ---------------------------------------------------------------------------

EditorPane::EditorPane(QString paneId, EditorProvider *provider, QWidget *parent)
    : QWidget(parent)
    , paneId_(std::move(paneId))
    , provider_(provider)
{
    setObjectName(QStringLiteral("editor-pane"));
    setFocusPolicy(Qt::StrongFocus);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    header_ = new QLabel(this);
    header_->setObjectName(QStringLiteral("editor-pane-header"));
    header_->setContentsMargins(6, 2, 6, 2);
    layout->addWidget(header_);

    editor_ = provider_ != nullptr ? provider_->createEditor(this) : nullptr;
    if (editor_ == nullptr) {
        auto *fallback = new QPlainTextEdit(this);
        fallback->setPlainText(QStringLiteral("No editor provider connected."));
        fallback->setReadOnly(true);
        editor_ = fallback;
    }
    layout->addWidget(editor_, 1);
    editor_->installEventFilter(this);
    updateHeader();
}

QString EditorPane::paneId() const
{
    return paneId_;
}

QWidget *EditorPane::editorWidget() const
{
    return editor_;
}

void EditorPane::setActive(bool active)
{
    if (active_ == active) {
        return;
    }
    active_ = active;
    updateHeader();
}

bool EditorPane::isActive() const
{
    return active_;
}

void EditorPane::setOnFocusRequested(std::function<void(EditorPane *)> callback)
{
    onFocusRequested_ = std::move(callback);
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
    Q_UNUSED(watched);
    if ((event->type() == QEvent::FocusIn || event->type() == QEvent::MouseButtonPress) &&
        onFocusRequested_ != nullptr) {
        onFocusRequested_(this);
    }
    return false;
}

void EditorPane::updateHeader()
{
    if (header_ == nullptr) {
        return;
    }
    const QString providerName = provider_ != nullptr ? provider_->displayName() : QStringLiteral("no provider");
    header_->setText(QStringLiteral("%1  ·  %2%3")
                         .arg(paneId_, providerName, active_ ? QStringLiteral("    ◀ active") : QString()));
    QFont font = header_->font();
    font.setBold(active_);
    header_->setFont(font);
}

// ---------------------------------------------------------------------------
// EditorArea
// ---------------------------------------------------------------------------

EditorArea::EditorArea(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("editor-area"));
    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(0);
}

EditorArea::~EditorArea()
{
    delete content_;
    content_ = nullptr;
    root_.reset();
}

void EditorArea::setProvider(EditorProvider *provider)
{
    provider_ = provider;
    // Existing panes keep the provider they were built with; new panes use the
    // new provider. The editor lane registers its provider before the first pane.
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
    pane->setOnFocusRequested([this](EditorPane *requested) { setActivePane(requested); });
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
    if (active_ == nullptr) {
        return false;
    }
    Node *leaf = findLeaf(root_.get(), active_);
    if (leaf == nullptr) {
        return false;
    }
    EditorPane *closing = active_;
    if (root_.get() == leaf) {
        // Last pane: replace its content with a fresh pane, never leave the
        // area without one (the shell must stay usable, empty state included).
        EditorPane *replacement = createPane();
        deletePane(closing);
        active_ = nullptr;
        leaf->pane = replacement;
        setActivePane(replacement);
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

void EditorArea::deletePane(EditorPane *pane)
{
    if (pane == nullptr) {
        return;
    }
    pane->setParent(nullptr);
    delete pane;
}

void EditorArea::clearTree()
{
    const auto all = panes();
    for (EditorPane *pane : all) {
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
        json.insert(QStringLiteral("type"), QStringLiteral("leaf"));
        json.insert(QStringLiteral("pane"), node->pane != nullptr ? node->pane->paneId() : QString());
        json.insert(QStringLiteral("provider"),
                    provider_ != nullptr ? provider_->id() : QString());
        return json;
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
                *error = QStringLiteral("leaf node without pane id");
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
        return node.release();
    }
    if (type == QLatin1String("split")) {
        const QString orientation = json.value(QStringLiteral("orientation")).toString();
        if (orientation != QLatin1String("horizontal") && orientation != QLatin1String("vertical")) {
            if (error != nullptr) {
                *error = QStringLiteral("split node without a valid orientation");
            }
            return nullptr;
        }
        const QJsonValue firstValue = json.value(QStringLiteral("first"));
        const QJsonValue secondValue = json.value(QStringLiteral("second"));
        if (!firstValue.isObject() || !secondValue.isObject()) {
            if (error != nullptr) {
                *error = QStringLiteral("split node needs two object children");
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
        *error = QStringLiteral("unknown layout node type '%1'").arg(type);
    }
    return nullptr;
}

QJsonObject EditorArea::saveLayout() const
{
    QJsonObject json;
    json.insert(QStringLiteral("version"), 1);
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
            *error = QStringLiteral("layout has no root object");
        }
        return false;
    }
    diagnostics_.clear();
    QString parseError;
    QList<EditorPane *> created;
    Node *parsed = nodeFromJson(rootValue.toObject(), &created, &parseError);
    if (parsed == nullptr) {
        for (EditorPane *pane : created) {
            deletePane(pane);
        }
        if (error != nullptr) {
            *error = parseError.isEmpty() ? QStringLiteral("layout parse failed") : parseError;
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

} // namespace vedit::app
