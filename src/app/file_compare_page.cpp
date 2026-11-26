#include "file_compare.h"

#include "editor_area.h"
#include "editor_provider.h"

#include <KActionCollection>
#include <KSyntaxHighlighting/Theme>
#include <KTextEditor/Attribute>
#include <KTextEditor/Document>
#include <KTextEditor/MovingRange>
#include <KTextEditor/View>

#include <QAction>
#include <QCheckBox>
#include <QCoreApplication>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QShortcut>
#include <QSplitter>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QTemporaryDir>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace vedit::app {
namespace {

constexpr auto maxBytes = ComparisonEditorInput::maxBytes;
constexpr auto maxLines = ComparisonEditorInput::maxLines;
const QByteArray bom = QByteArray::fromHex("efbbbf");

QString textFor(const char *text)
{
    return QCoreApplication::translate("FileComparison", text);
}

std::optional<QByteArray> readLocal(const QString &path)
{
    QFile file(path);
    if (!QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const QByteArray bytes = file.read(maxBytes + 1);
    return file.error() == QFileDevice::NoError ? std::optional(bytes) : std::nullopt;
}

bool writeInput(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.flush();
}

bool exceedsLimit(const QByteArray &bytes)
{
    return bytes.size() > maxBytes || bytes.count('\n') >= maxLines;
}

struct Lines {
    QByteArray text;
    QList<qsizetype> offsets;

    void reset(QByteArray bytes)
    {
        text = std::move(bytes);
        offsets = {0};
        for (qsizetype pos = text.indexOf('\n'); pos >= 0; pos = text.indexOf('\n', pos + 1)) {
            offsets.append(pos + 1);
        }
        if (offsets.constLast() != text.size()) {
            offsets.append(text.size());
        }
    }

    qsizetype size() const { return offsets.size() - 1; }
    QByteArray slice(int first, int count) const
    {
        return text.mid(offsets.at(first), offsets.at(first + count) - offsets.at(first));
    }
};

struct Hunk {
    std::array<int, 2> first;
    std::array<int, 2> count;
};

struct ChangedSpan {
    qsizetype start;
    std::array<qsizetype, 2> end;
};

ChangedSpan changedSpan(QStringView left, QStringView right)
{
    qsizetype first = 0;
    const auto sharedLength = std::min(left.size(), right.size());
    while (first < sharedLength && left[first] == right[first]) {
        ++first;
    }
    if (first > 0 && left[first - 1].isHighSurrogate()) {
        --first;
    }
    auto leftEnd = left.size();
    auto rightEnd = right.size();
    while (leftEnd > first && rightEnd > first && left[leftEnd - 1] == right[rightEnd - 1]) {
        --leftEnd;
        --rightEnd;
    }
    if (leftEnd < left.size() && left[leftEnd].isLowSurrogate()) {
        ++leftEnd;
        ++rightEnd;
    }
    return {first, {leftEnd, rightEnd}};
}

struct InlineDifference {
    std::array<int, 2> line;
    ChangedSpan columns;
};

std::optional<QList<Hunk>> parseHunks(int code, const QByteArray &result, const std::array<Lines, 2> &lines)
{
    if (code != 0 && code != 1) {
        return std::nullopt;
    }
    QList<Hunk> hunks;
    std::array<int, 2> end{};
    for (const auto &line : result.split('\n')) {
        if (line.isEmpty()) {
            continue;
        }
        const auto numbers = line.split(' ');
        if (numbers.size() != 4) {
            return std::nullopt;
        }
        Hunk hunk{};
        for (int side = 0; side < 2; ++side) {
            bool startOk = false;
            bool countOk = false;
            const int start = numbers.at(2 * side).toInt(&startOk);
            const int count = numbers.at(2 * side + 1).toInt(&countOk);
            if (!startOk || !countOk || start < 1 || count < 0 || start - 1 < end[side]
                || start - 1 > lines[side].size() || count > lines[side].size() - (start - 1)) {
                return std::nullopt;
            }
            hunk.first[side] = start - 1;
            hunk.count[side] = count;
        }
        if (hunk.first[0] - end[0] != hunk.first[1] - end[1] || (hunk.count[0] == 0 && hunk.count[1] == 0)) {
            return std::nullopt;
        }
        end = {hunk.first[0] + hunk.count[0], hunk.first[1] + hunk.count[1]};
        hunks.append(hunk);
    }
    if ((code == 0) != hunks.isEmpty() || lines[0].size() - end[0] != lines[1].size() - end[1]) {
        return std::nullopt;
    }
    return hunks;
}

struct Side {
    CompareInput input;
    KTextEditor::View *view = nullptr;
    QLabel *label = nullptr;
    QPushButton *save = nullptr;
    QPushButton *apply = nullptr;
    std::vector<std::unique_ptr<KTextEditor::MovingRange>> decorations;

    KTextEditor::Document *document() const { return view ? view->document() : nullptr; }
    bool dirty() const { return document() && document()->isModified(); }
    bool writable() const
    {
        if (input.localPath.isEmpty() || (document() && !document()->isReadWrite())) {
            return false;
        }
        const QFileInfo info(input.localPath);
        constexpr auto writeBits = QFileDevice::WriteOwner | QFileDevice::WriteGroup | QFileDevice::WriteOther;
        return (info.permissions() & writeBits) && info.isWritable() && QFileInfo(info.absolutePath()).isWritable();
    }
};

KTextEditor::Cursor lineStart(KTextEditor::Document *document, int line)
{
    return line < document->lines() ? KTextEditor::Cursor(line, 0) : document->documentEnd();
}

QAction *findAction(KTextEditor::View *view, const QStringList &names)
{
    if (view) {
        for (const auto &name : names) {
            if (auto *action = view->actionCollection()->action(name)) {
                return action;
            }
        }
    }
    return nullptr;
}

const QStringList undoActions{QStringLiteral("edit_undo"), QStringLiteral("undo")};

class Comparison final : public QWidget {
public:
    Comparison(EditorArea &area, QString id, CompareInput left, CompareInput right);
    ~Comparison() override;
    AuxiliaryTabCallbacks callbacks();

private:
    QString prepare(int side, QWidget *parent);
    void connectView(int side);
    QWidget *openEditorForPath(const QString &path) const;
    void documentChanged();
    void recalculate();
    void diffFinished(int code);
    void selectHunk(int index, bool moveCursors);
    void syncScroll(int source, int line);
    void collectInlineDifferences();
    void paintDifferences();
    void refreshControls();
    void applyHunk(int target);
    bool saveSide(int side, QString *error);
    bool saveAll(QString *error);
    bool isModified() const;

    EditorArea &area_;
    const QString id_;
    EditorProvider *provider_;
    std::array<Side, 2> sides_;
    QTemporaryDir temporary_;
    QProcess diff_;
    QTimer update_;
    std::array<Lines, 2> lines_;
    QList<Hunk> hunks_;
    QList<InlineDifference> inlineDifferences_;
    QLabel *status_ = nullptr;
    QPushButton *previous_ = nullptr;
    QPushButton *next_ = nullptr;
    QPushButton *undo_ = nullptr;
    QCheckBox *sync_ = nullptr;
    int selected_ = -1;
    int activeSide_ = 0;
    int editedSide_ = 0;
    quint64 generation_ = 0;
    quint64 runningGeneration_ = 0;
    bool computing_ = false;
    bool ready_ = false;
    bool moving_ = false;
    bool initialized_ = false;
};

Comparison::Comparison(EditorArea &area, QString id, CompareInput left, CompareInput right)
    : area_(area), id_(std::move(id)), provider_(area.provider()), diff_(this)
{
    setObjectName(QStringLiteral("file-comparison"));
    sides_[0].input = std::move(left);
    sides_[1].input = std::move(right);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 4);
    layout->setSpacing(4);
    auto *actions = new QHBoxLayout;
    const auto button = [this](QHBoxLayout *row, const QString &text, const char *name) {
        auto *control = new QPushButton(text, this);
        control->setObjectName(QString::fromLatin1(name));
        row->addWidget(control);
        return control;
    };
    previous_ = button(actions, textFor("Previous difference"), "compare-previous");
    next_ = button(actions, textFor("Next difference"), "compare-next");
    sides_[0].apply = button(actions, QStringLiteral("←"), "compare-apply-left");
    sides_[1].apply = button(actions, QStringLiteral("→"), "compare-apply-right");
    for (const auto &side : sides_) {
        side.apply->setFixedWidth(38);
    }
    undo_ = button(actions, textFor("Undo"), "compare-undo");
    sync_ = new QCheckBox(textFor("Sync scroll"), this);
    sync_->setObjectName(QStringLiteral("compare-sync-scroll"));
    sync_->setChecked(true);
    actions->addWidget(sync_);
    actions->addStretch();
    layout->addLayout(actions);

    auto *split = new QSplitter(Qt::Horizontal, this);
    split->setChildrenCollapsible(false);
    QString error;
    for (int side = 0; side < 2; ++side) {
        auto &state = sides_[side];
        auto *column = new QWidget(split);
        auto *stack = new QVBoxLayout(column);
        stack->setContentsMargins(0, 0, 0, 0);
        stack->setSpacing(4);
        auto *header = new QHBoxLayout;
        state.label = new QLabel(column);
        state.label->setObjectName(side == 0 ? QStringLiteral("compare-label-left") : QStringLiteral("compare-label-right"));
        state.label->setTextFormat(Qt::PlainText);
        state.label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        header->addWidget(state.label, 1);
        const QString saveTitle = side == 0 ? textFor("Save left") : textFor("Save right");
        state.save = button(header, saveTitle, side == 0 ? "compare-save-left" : "compare-save-right");
        state.apply->setToolTip(side == 0 ? textFor("Apply selected difference to the left")
                                          : textFor("Apply selected difference to the right"));
        state.apply->setAccessibleName(state.apply->toolTip());
        connect(state.apply, &QPushButton::clicked, this, [this, side] { applyHunk(side); });
        connect(state.save, &QPushButton::clicked, this, [this, side] {
            QString message;
            if (!saveSide(side, &message)) {
                status_->setText(message);
            }
        });
        stack->addLayout(header);
        const QString failure = prepare(side, column);
        if (!failure.isEmpty() && error.isEmpty()) {
            error = failure;
        }
        if (state.view) {
            stack->addWidget(state.view, 1);
            connectView(side);
        } else {
            stack->addStretch();
        }
        split->setStretchFactor(side, 1);
    }
    layout->addWidget(split, 1);
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("compare-status"));
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    initialized_ = error.isEmpty();
    setFocusProxy(sides_[0].view ? sides_[0].view : sides_[1].view);
    refreshControls();
    if (!initialized_) {
        for (auto &side : sides_) {
            if (side.view) {
                side.view->setEnabled(false);
            }
        }
        status_->setText(error);
        return;
    }
    connect(previous_, &QPushButton::clicked, this, [this] { selectHunk(selected_ - 1, true); });
    connect(next_, &QPushButton::clicked, this, [this] { selectHunk(selected_ + 1, true); });
    const auto bindNavigation = [this](QPushButton *control, QKeySequence sequence) {
        auto *shortcut = new QShortcut(sequence, this);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        control->setToolTip(QStringLiteral("%1 (%2)").arg(control->text(), sequence.toString(QKeySequence::NativeText)));
        connect(shortcut, &QShortcut::activated, control, &QPushButton::click);
    };
    bindNavigation(previous_, QKeySequence(Qt::ALT | Qt::Key_PageUp));
    bindNavigation(next_, QKeySequence(Qt::ALT | Qt::Key_PageDown));
    connect(undo_, &QPushButton::clicked, this, [this] {
        if (auto *action = findAction(sides_[editedSide_].view, undoActions); action && action->isEnabled()) {
            action->trigger();
        }
    });
    connect(sync_, &QCheckBox::toggled, this, [this] {
        syncScroll(activeSide_, sides_[activeSide_].view->firstDisplayedLine());
    });
    update_.setSingleShot(true);
    update_.setInterval(120);
    connect(&update_, &QTimer::timeout, this, &Comparison::recalculate);
    connect(&diff_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus exit) {
        diffFinished(exit == QProcess::NormalExit ? code : -1);
    });
    connect(&diff_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError failure) {
        if (failure == QProcess::FailedToStart) {
            diffFinished(-1);
        }
    });
    recalculate();
}

Comparison::~Comparison()
{
    for (auto &side : sides_) {
        side.decorations.clear();
        if (auto *document = side.document()) {
            disconnect(document, nullptr, this, nullptr);
            disconnect(side.view, nullptr, this, nullptr);
            if (auto *undo = findAction(side.view, undoActions)) {
                disconnect(undo, nullptr, this, nullptr);
            }
        }
    }
    disconnect(&diff_, nullptr, this, nullptr);
    if (diff_.state() != QProcess::NotRunning) {
        diff_.kill();
        diff_.waitForFinished();
    }
}

void Comparison::connectView(int side)
{
    auto &state = sides_[side];
    const QStringList editorOnlyActions{QStringLiteral("file_save"), QStringLiteral("file_save_as"),
        QStringLiteral("file_save_as_with_encoding"), QStringLiteral("file_save_copy_as"),
        QStringLiteral("file_reload"), QStringLiteral("tools_toggle_write_lock")};
    for (const auto &name : editorOnlyActions) {
        if (auto *action = state.view->actionCollection()->action(name)) {
            action->setShortcuts({});
            action->setVisible(false);
            disconnect(action, nullptr, state.document(), nullptr);
            disconnect(action, nullptr, state.view, nullptr);
        }
    }
    connect(state.view, &KTextEditor::View::focusIn, this, [this, side] {
        activeSide_ = side;
        setFocusProxy(sides_[side].view);
        area_.refreshAuxiliaryTab(id_);
    });
    connect(state.view, &KTextEditor::View::verticalScrollPositionChanged, this,
            [this, side](KTextEditor::View *view) { syncScroll(side, view->firstDisplayedLine()); });
    connect(state.view, &KTextEditor::View::cursorPositionChanged, this,
            [this, side](KTextEditor::View *, KTextEditor::Cursor cursor) {
        if (!ready_ || moving_ || side != activeSide_) {
            return;
        }
        const auto it = std::upper_bound(hunks_.cbegin(), hunks_.cend(), cursor.line(),
            [side](int line, const Hunk &hunk) { return line < hunk.first[side]; });
        if (it != hunks_.cbegin() && cursor.line() < (it - 1)->first[side] + (it - 1)->count[side]) {
            selectHunk(static_cast<int>(it - hunks_.cbegin() - 1), false);
        }
    });
    connect(state.view, &KTextEditor::View::configChanged, this, [this] { paintDifferences(); });
    connect(state.document(), &KTextEditor::Document::aboutToInvalidateMovingInterfaceContent,
            this, [this] { documentChanged(); });
    connect(state.document(), &KTextEditor::Document::textChanged, this, [this, side] {
        editedSide_ = side;
        documentChanged();
    });
    const auto refresh = [this] {
        refreshControls();
        area_.refreshAuxiliaryTab(id_);
    };
    connect(state.document(), &KTextEditor::Document::modifiedChanged, this, refresh);
    if (auto *undo = findAction(state.view, undoActions)) {
        connect(undo, &QAction::changed, this, refresh);
    }
}

QWidget *Comparison::openEditorForPath(const QString &path) const
{
    auto panes = area_.panes();
    if (auto *active = area_.activePane(); panes.removeOne(active)) {
        panes.prepend(active);
    }
    for (auto *pane : panes) {
        for (int tab = 0; tab < pane->tabCount(); ++tab) {
            const auto tabPath = pane->tabFilePath(tab);
            if (!tabPath.isEmpty() && QFileInfo(tabPath).canonicalFilePath() == path) {
                return pane->tabEditorWidget(tab);
            }
        }
    }
    return nullptr;
}

QString Comparison::prepare(int index, QWidget *parent)
{
    auto &side = sides_[index];
    const auto &input = side.input;
    const auto bytes = input.localPath.isEmpty() ? std::optional(input.snapshot) : readLocal(input.localPath);
    if (!bytes) {
        return textFor("Cannot read %1.").arg(input.label);
    }
    if (bytes->size() > maxBytes) {
        return textFor("%1 exceeds the 8 MiB text comparison limit.").arg(input.label);
    }
    QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
    QByteArray text = bytes->startsWith(bom) ? bytes->mid(bom.size()) : *bytes;
    text.replace("\r\n", "\n");
    const QString decoded = decoder.decode(text);
    Q_UNUSED(decoded);
    if (decoder.hasError() || bytes->contains('\0')) {
        return textFor("%1 is binary or uses an unsupported encoding. Comparison requires UTF-8 text.").arg(input.label);
    }
    if (exceedsLimit(text)) {
        return textFor("%1 exceeds the text comparison limit (8 MiB or 100,000 lines).").arg(input.label);
    }
    QWidget *source = nullptr;
    if (provider_ && !input.localPath.isEmpty()) {
        source = input.editorView;
        if (!source || QFileInfo(provider_->currentFilePath(source)).canonicalFilePath() != input.localPath) {
            source = openEditorForPath(input.localPath);
        }
    }
    const auto *sourceView = qobject_cast<KTextEditor::View *>(source);
    const bool readOnly = !side.writable() || (sourceView && !sourceView->document()->isReadWrite());
    if (readOnly && sourceView) {
        text = sourceView->document()->text().toUtf8();
        if (exceedsLimit(text) || text.contains('\0')) {
            return textFor("%1 exceeds the text comparison limit (8 MiB or 100,000 lines).").arg(input.label);
        }
    }
    const QString logical = input.logicalPath.isEmpty() ? input.localPath : input.logicalPath;
    const QString backing = readOnly ? temporary_.filePath(QStringLiteral("snapshot-%1").arg(index)) : input.localPath;
    if (readOnly && (!temporary_.isValid() || !writeInput(backing, text))) {
        return textFor("Cannot prepare comparison input.");
    }
    QString error;
    side.view = provider_ ? qobject_cast<KTextEditor::View *>(provider_->createComparisonEditor(
        source, parent, {input.localPath, backing, logical, readOnly}, &error)) : nullptr;
    if (!side.view) {
        return error.isEmpty() ? textFor("Comparison requires a KTextEditor view.") : error;
    }
    side.view->setObjectName(index == 0 ? QStringLiteral("compare-left-editor") : QStringLiteral("compare-right-editor"));
    side.view->setAccessibleName(index == 0 ? textFor("Left comparison editor") : textFor("Right comparison editor"));
    if (!readOnly && !side.document()->property(comparisonDiskDigestProperty).isValid()) {
        side.document()->setProperty(comparisonDiskDigestProperty, comparisonDiskDigest(*bytes));
    }
    return {};
}

void Comparison::documentChanged()
{
    if (!initialized_) {
        return;
    }
    ++generation_;
    ready_ = false;
    computing_ = true;
    paintDifferences();
    refreshControls();
    area_.refreshAuxiliaryTab(id_);
    update_.start();
}

void Comparison::recalculate()
{
    if (diff_.state() != QProcess::NotRunning) {
        return;
    }
    QStringList args;
    const QString format = QStringLiteral("%df %dn %dF %dN\n");
    for (const QString &kind : {QStringLiteral("old"), QStringLiteral("new"), QStringLiteral("changed")}) {
        args << QStringLiteral("--%1-group-format=").arg(kind) + format;
    }
    args << QStringLiteral("--unchanged-group-format=") << QStringLiteral("--");
    for (int side = 0; side < 2; ++side) {
        const QByteArray text = sides_[side].document()->text().toUtf8();
        const bool tooLarge = exceedsLimit(text);
        const QString path = temporary_.filePath(QStringLiteral("diff-%1").arg(side));
        if (tooLarge || !temporary_.isValid() || !writeInput(path, text)) {
            computing_ = false;
            refreshControls();
            status_->setText(tooLarge ? textFor("%1 exceeds the text comparison limit (8 MiB or 100,000 lines).").arg(sides_[side].input.label)
                                      : textFor("Cannot prepare comparison input."));
            return;
        }
        lines_[side].reset(text);
        args << path;
    }
    runningGeneration_ = generation_;
    computing_ = true;
    refreshControls();
    const QString tools = QCoreApplication::applicationDirPath() + QStringLiteral("/usr/lib/vedit/file-compare");
    QString executable = QStandardPaths::findExecutable(QStringLiteral("diff"), {tools});
    if (executable.isEmpty()) {
        executable = QStandardPaths::findExecutable(QStringLiteral("diff"));
    }
    if (executable.isEmpty()) {
        diffFinished(-1);
        return;
    }
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.remove(QStringLiteral("LD_LIBRARY_PATH"));
    diff_.setProcessEnvironment(environment);
    diff_.start(executable, args);
}

void Comparison::diffFinished(int code)
{
    const QByteArray out = diff_.readAllStandardOutput();
    const QString error = QString::fromLocal8Bit(diff_.readAllStandardError()).trimmed();
    if (runningGeneration_ != generation_) {
        if (!update_.isActive()) {
            update_.start();
        }
        return;
    }
    computing_ = false;
    auto result = parseHunks(code, out, lines_);
    ready_ = result.has_value();
    hunks_ = std::move(result).value_or(QList<Hunk>{});
    collectInlineDifferences();
    const bool firstResult = generation_ == 0 && !hunks_.isEmpty();
    selected_ = hunks_.isEmpty() ? -1 : std::clamp(selected_, 0, static_cast<int>(hunks_.size()) - 1);
    paintDifferences();
    if (firstResult) {
        selectHunk(selected_, true);
    } else {
        refreshControls();
    }
    if (!ready_) {
        status_->setText(error.isEmpty() ? textFor("Comparison could not run. GNU diff is required.")
                                         : textFor("Comparison could not run: %1").arg(error));
    }
}

void Comparison::selectHunk(int index, bool moveCursors)
{
    if (!ready_ || index < 0 || index >= hunks_.size()) {
        return;
    }
    if (index != selected_) {
        selected_ = index;
        paintDifferences();
    }
    if (moveCursors) {
        const QScopedValueRollback moving(moving_, true);
        const auto &hunk = hunks_.at(index);
        for (int side = 0; side < 2; ++side) {
            auto *view = sides_[side].view;
            auto *document = view->document();
            view->setCursorPosition(lineStart(document, hunk.first[side]));
            view->setScrollPosition(lineStart(document, std::max(0, hunk.first[side] - 2)));
        }
    }
    refreshControls();
}

void Comparison::syncScroll(int source, int line)
{
    if (!ready_ || moving_ || !sync_->isChecked()) {
        return;
    }
    const int target = 1 - source;
    int offset = 0;
    for (const auto &hunk : hunks_) {
        if (line < hunk.first[source]) {
            break;
        }
        if (line < hunk.first[source] + hunk.count[source]) {
            offset = hunk.first[target] - hunk.first[source];
            line = hunk.first[source] + std::min(line - hunk.first[source], std::max(0, hunk.count[target] - 1));
            break;
        }
        offset = hunk.first[target] + hunk.count[target] - hunk.first[source] - hunk.count[source];
    }
    const QScopedValueRollback moving(moving_, true);
    auto *other = sides_[target].view;
    const int mapped = std::clamp(line + offset, 0, other->document()->lines() - 1);
    if (other->firstDisplayedLine() != mapped) {
        other->setScrollPosition(KTextEditor::Cursor(mapped, 0));
    }
}

void Comparison::collectInlineDifferences()
{
    // Pair changed lines by position within their hunk. Large input keeps the
    // full-line cue without paying for an unbounded number of extra ranges.
    constexpr int maxInlineColumns = 4096;
    constexpr int maxInlineDifferences = 10000;
    inlineDifferences_.clear();
    const auto *left = sides_[0].document();
    const auto *right = sides_[1].document();
    for (const auto &hunk : hunks_) {
        const int pairs = std::min(hunk.count[0], hunk.count[1]);
        for (int offset = 0; offset < pairs; ++offset) {
            if (inlineDifferences_.size() >= maxInlineDifferences) {
                return;
            }
            const int leftLine = hunk.first[0] + offset;
            const int rightLine = hunk.first[1] + offset;
            if (left->lineLength(leftLine) > maxInlineColumns || right->lineLength(rightLine) > maxInlineColumns) {
                continue;
            }
            const auto leftText = left->line(leftLine);
            const auto rightText = right->line(rightLine);
            if (leftText != rightText) {
                inlineDifferences_.append({{leftLine, rightLine}, changedSpan(leftText, rightText)});
            }
        }
    }
}

void Comparison::paintDifferences()
{
    for (int index = 0; index < 2; ++index) {
        auto &side = sides_[index];
        side.decorations.clear();
        if (!side.view) {
            continue;
        }
        if (!ready_) {
            side.view->setProperty("compare-diff-range-count", 0);
            side.view->setProperty("compare-inline-ranges", QVariantList{});
            side.view->setProperty("compare-inline-background-only", false);
            continue;
        }
        const QColor base = QColor::fromRgba(side.view->theme().editorColor(KSyntaxHighlighting::Theme::BackgroundColor));
        const QColor accent = index == 0 ? QColor(208, 65, 68) : QColor(37, 155, 100);
        const auto attribute = [&](int strength, bool fillWhitespace) {
            const auto mix = [=](int background, int ink) { return (background * (100 - strength) + ink * strength) / 100; };
            auto result = KTextEditor::Attribute::Ptr(new KTextEditor::Attribute);
            result->setBackground(QColor(mix(base.red(), accent.red()), mix(base.green(), accent.green()), mix(base.blue(), accent.blue())));
            result->setBackgroundFillWhitespace(fillWhitespace);
            return result;
        };
        const auto plain = attribute(22, true);
        const auto selected = attribute(36, true);
        const auto stronger = attribute(52, false);
        auto *document = side.document();
        const auto decorate = [&side, document](const KTextEditor::Range &bounds,
                                               const KTextEditor::Attribute::Ptr &style, qreal depth) {
            auto *range = document->newMovingRange(bounds);
            range->setView(side.view);
            range->setAttributeOnlyForViews(true);
            range->setRange(bounds, style, depth);
            side.decorations.emplace_back(range);
            return range;
        };
        for (int i = 0; i < hunks_.size(); ++i) {
            const auto &hunk = hunks_.at(i);
            if (hunk.count[index] == 0) {
                continue;
            }
            const auto start = lineStart(document, hunk.first[index]);
            const auto end = lineStart(document, hunk.first[index] + hunk.count[index]);
            if (start == end) {
                continue;
            }
            decorate({start, end}, i == selected_ ? selected : plain, 0);
        }
        side.view->setProperty("compare-diff-range-count", static_cast<int>(side.decorations.size()));
        QVariantList ranges;
        bool backgroundOnly = true;
        for (const auto &difference : inlineDifferences_) {
            const auto start = static_cast<int>(difference.columns.start);
            const auto end = static_cast<int>(difference.columns.end[index]);
            if (start == end) {
                continue;
            }
            const int line = difference.line[index];
            const auto *range = decorate(KTextEditor::Range(line, start, line, end), stronger, -1);
            const auto bounds = range->toRange();
            const auto style = range->attribute();
            ranges.append(QVariant::fromValue(QVariantList{bounds.start().line(), bounds.start().column(), bounds.end().column()}));
            backgroundOnly &= style->hasProperty(QTextFormat::BackgroundBrush) && !style->hasProperty(QTextFormat::ForegroundBrush);
        }
        side.view->setProperty("compare-inline-background-only", !ranges.isEmpty() && backgroundOnly);
        side.view->setProperty("compare-inline-ranges", ranges);
    }
}

void Comparison::refreshControls()
{
    if (!status_) {
        return;
    }
    const bool selected = ready_ && selected_ >= 0;
    for (auto &side : sides_) {
        QString label = side.input.label;
        if (QFileInfo(label).isAbsolute() && !side.input.localPath.isEmpty()
            && QFileInfo(label).canonicalFilePath() == side.input.localPath) {
            label = QFileInfo(label).fileName();
        }
        if (side.input.missing) {
            label += textFor("  ·  Absent at this revision");
        } else if (!side.writable()) {
            label += textFor("  ·  Read-only");
        }
        if (side.dirty()) {
            label += QStringLiteral("  *");
        }
        side.label->setText(label);
        side.label->setToolTip(label == side.input.label ? label : side.input.label + u'\n' + label);
        side.apply->setEnabled(selected && side.writable());
        side.save->setVisible(!side.input.localPath.isEmpty());
        side.save->setEnabled(initialized_ && side.dirty() && side.writable());
    }
    previous_->setEnabled(selected && selected_ > 0);
    next_->setEnabled(ready_ && selected_ + 1 < hunks_.size());
    const auto *undo = findAction(sides_[editedSide_].view, undoActions);
    undo_->setEnabled(initialized_ && undo && undo->isEnabled());
    sync_->setEnabled(initialized_);
    if (computing_) {
        status_->setText(textFor("Comparing…"));
    } else if (ready_) {
        QString status = hunks_.isEmpty() ? textFor("No differences")
            : textFor("Difference %1 of %2").arg(selected_ + 1).arg(hunks_.size());
        if (selected) {
            const auto &hunk = hunks_.at(selected_);
            if (hunk.count[0] == 0) {
                status += textFor("  ·  Right only");
            } else if (hunk.count[1] == 0) {
                status += textFor("  ·  Left only");
            }
        }
        status_->setText(status);
    }
}

void Comparison::applyHunk(int target)
{
    if (!ready_ || selected_ < 0 || !sides_[target].writable()) {
        return;
    }
    const auto &hunk = hunks_.at(selected_);
    const int source = 1 - target;
    const auto &original = lines_[target];
    const qsizetype start = original.offsets.at(hunk.first[target]);
    const qsizetype length = original.offsets.at(hunk.first[target] + hunk.count[target]) - start;
    const QByteArray replacement = lines_[source].slice(hunk.first[source], hunk.count[source]);
    QByteArray merged = original.text;
    merged.replace(start, length, replacement);
    if (exceedsLimit(merged)) {
        status_->setText(textFor("Applying this difference would exceed the text comparison limit."));
        return;
    }
    const QString prefix = QString::fromUtf8(QByteArrayView(original.text).first(start));
    const QString removed = QString::fromUtf8(QByteArrayView(original.text).sliced(start, length));
    const QString inserted = QString::fromUtf8(replacement);
    // Keep common text, including line boundaries, out of the native edit.
    const auto columns = changedSpan(removed, inserted);
    if (!provider_->replaceComparisonText(sides_[target].view, prefix.size() + columns.start, columns.end[0] - columns.start,
                                          inserted.mid(columns.start, columns.end[1] - columns.start))) {
        status_->setText(textFor("This difference could not be applied."));
    }
}

bool Comparison::saveSide(int target, QString *error)
{
    auto &side = sides_[target];
    if (!side.dirty()) {
        return true;
    }
    if (!initialized_ || !side.writable()) {
        *error = textFor("%1 is read-only.").arg(side.input.label);
        return false;
    }
    if (!provider_->saveFile(side.view, error)) {
        return false;
    }
    refreshControls();
    status_->setText(textFor("Saved %1").arg(side.input.label));
    return true;
}

bool Comparison::saveAll(QString *error)
{
    QString detail;
    if (!saveSide(activeSide_, &detail) || !saveSide(1 - activeSide_, &detail)) {
        status_->setText(detail);
        if (error) {
            *error = detail;
        }
        return false;
    }
    return true;
}

bool Comparison::isModified() const
{
    return initialized_ && (sides_[0].dirty() || sides_[1].dirty());
}

AuxiliaryTabCallbacks Comparison::callbacks()
{
    AuxiliaryTabCallbacks result;
    result.isModified = [this] { return isModified(); };
    result.save = [this](QString *error) { return saveAll(error); };
    result.actionEnabled = [this](const QStringList &names) {
        const auto *action = findAction(sides_[activeSide_].view, names);
        return action && action->isEnabled();
    };
    result.triggerAction = [this](const QStringList &names, const char *) {
        auto *action = findAction(sides_[activeSide_].view, names);
        if (!action || !action->isEnabled()) {
            return false;
        }
        action->trigger();
        return true;
    };
    result.needsClosePrompt = [this] {
        return initialized_ && std::any_of(sides_.begin(), sides_.end(), [](const Side &side) {
            return side.dirty() && side.document()->views().size() == 1;
        });
    };
    return result;
}

} // namespace

void openFileComparisonPage(EditorArea &area, const QString &id, const QString &title, CompareInput left, CompareInput right)
{
    auto *comparison = new Comparison(area, id, std::move(left), std::move(right));
    area.openAuxiliaryTab(id, title, comparison, comparison->callbacks());
}

} // namespace vedit::app
