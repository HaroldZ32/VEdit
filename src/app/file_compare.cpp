#include "file_compare.h"

#include "app_shell.h"
#include "editor_area.h"
#include "editor_provider.h"

#include <QAbstractTableModel>
#include <QCoreApplication>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSaveFile>
#include <QShortcut>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QTableView>
#include <QTemporaryDir>
#include <QVBoxLayout>

#include <array>
#include <filesystem>
#include <optional>
#include <utility>

namespace vedit::app {
namespace {

constexpr qsizetype maxBytes = 8 * 1024 * 1024;
constexpr qsizetype maxLines = 100000;
const QByteArray bom = QByteArray::fromHex("efbbbf");

std::optional<QByteArray> readLocal(const QString &path)
{
    QFile file(path);
    if (!QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const QByteArray bytes = file.read(maxBytes + 1);
    return file.error() == QFileDevice::NoError ? std::optional(bytes) : std::nullopt;
}

QByteArray normalized(QByteArray bytes)
{
    if (bytes.startsWith(bom)) {
        bytes.remove(0, bom.size());
    }
    return bytes.replace("\r\n", "\n");
}

QList<QByteArray> splitLines(const QByteArray &bytes)
{
    QList<QByteArray> lines;
    for (qsizetype start = 0; start < bytes.size();) {
        const qsizetype newline = bytes.indexOf('\n', start);
        const qsizetype end = newline < 0 ? bytes.size() : newline + 1;
        lines.append(bytes.mid(start, end - start));
        start = end;
    }
    return lines;
}

struct Side {
    CompareInput input;
    QByteArray disk;
    std::optional<QByteArray> clean;
    QByteArray bytes;
    EditorProvider *provider = nullptr;
    QPointer<QWidget> editor;
    bool wasModified = false;

    bool writable() const
    {
        const QFileInfo info(input.localPath);
        constexpr auto writeBits = QFileDevice::WriteOwner | QFileDevice::WriteGroup | QFileDevice::WriteOther;
        return !input.localPath.isEmpty() && (info.permissions() & writeBits) && info.isWritable()
            && QFileInfo(info.absolutePath()).isWritable();
    }

    bool dirty() const { return clean && *clean != bytes; }
    bool bufferCurrent() const
    {
        return !provider || (editor && provider->comparisonText(editor) ==
                                       QString::fromUtf8(normalized(bytes)));
    }

    bool update(const QByteArray &next)
    {
        if (!clean || !bufferCurrent()) {
            return false;
        }
        const QString oldText = QString::fromUtf8(normalized(bytes));
        const QString newText = QString::fromUtf8(normalized(next));
        if (provider && oldText != newText) {
            qsizetype start = 0;
            qsizetype oldEnd = oldText.size();
            qsizetype newEnd = newText.size();
            while (start < oldEnd && start < newEnd && oldText.at(start) == newText.at(start)) {
                ++start;
            }
            if (start > 0 && oldText.at(start - 1).isHighSurrogate()) {
                --start;
            }
            while (oldEnd > start && newEnd > start && oldText.at(oldEnd - 1) == newText.at(newEnd - 1)) {
                --oldEnd;
                --newEnd;
            }
            if (oldEnd < oldText.size() && oldText.at(oldEnd).isLowSurrogate()) {
                ++oldEnd;
                ++newEnd;
            }
            if (!provider->replaceComparisonText(editor, start, oldEnd - start,
                                                 newText.mid(start, newEnd - start))) {
                return false;
            }
        }
        bytes = next;
        if (provider) {
            provider->setComparisonModified(editor, dirty() || wasModified);
        }
        return true;
    }
};

QString prepare(Side &side, ApplicationShell &shell)
{
    if (!side.input.localPath.isEmpty()) {
        const auto disk = readLocal(side.input.localPath);
        if (!disk) {
            return QCoreApplication::translate("FileComparison", "Cannot read %1.").arg(side.input.label);
        }
        side.disk = *disk;
        side.bytes = *disk;
    } else {
        side.bytes = side.input.snapshot;
    }
    if (side.bytes.size() > maxBytes) {
        return QCoreApplication::translate("FileComparison", "%1 exceeds the 8 MiB text comparison limit.").arg(side.input.label);
    }
    QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
    const QString diskText = decoder.decode(normalized(side.bytes));
    if (decoder.hasError() || side.bytes.contains('\0')) {
        return QCoreApplication::translate("FileComparison", "%1 is binary or uses an unsupported encoding. Comparison requires UTF-8 text.")
            .arg(side.input.label);
    }
    if (!side.input.localPath.isEmpty()) {
        for (EditorPane *pane : shell.editorArea()->panes()) {
            for (int index = 0; index < pane->tabCount(); ++index) {
                const QString path = pane->tabFilePath(index);
                if (path.isEmpty() || QFileInfo(path).canonicalFilePath() != side.input.localPath) {
                    continue;
                }
                side.provider = pane->provider();
                side.editor = pane->tabEditorWidget(index);
                side.wasModified = side.provider->isModified(side.editor);
                const QString text = side.provider->comparisonText(side.editor);
                if (text.contains(QChar::Null)) {
                    return QCoreApplication::translate("FileComparison", "%1 is binary or uses an unsupported encoding. Comparison requires UTF-8 text.").arg(side.input.label);
                }
                if (!side.wasModified && text != diskText) {
                    return QCoreApplication::translate("FileComparison", "%1 differs from the open editor. Reload it before comparing.").arg(side.input.label);
                }
                if (text != diskText) {
                    side.bytes = text.toUtf8();
                    if (side.disk.contains("\r\n")) {
                        side.bytes.replace("\n", "\r\n");
                    }
                    if (side.disk.startsWith(bom)) {
                        side.bytes.prepend(bom);
                    }
                }
                break;
            }
            if (side.editor) {
                break;
            }
        }
    }
    if (side.bytes.size() > maxBytes || side.bytes.count('\n') >= maxLines) {
        return QCoreApplication::translate("FileComparison", "%1 exceeds the text comparison limit (8 MiB or 100,000 lines).").arg(side.input.label);
    }
    side.clean = side.bytes;
    return {};
}

struct Hunk {
    std::array<qsizetype, 2> first;
    std::array<qsizetype, 2> count;
};

struct Row {
    std::array<QString, 2> text;
    std::array<qsizetype, 2> line;
    int hunk;
    std::array<bool, 2> present;
};

class AlignedRows final : public QAbstractTableModel {
public:
    using QAbstractTableModel::QAbstractTableModel;

    int rowCount(const QModelIndex &parent) const override { return parent.isValid() ? 0 : rows.size(); }
    int columnCount(const QModelIndex &parent) const override { return parent.isValid() ? 0 : 3; }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid()) {
            return {};
        }
        const auto &row = rows.at(index.row());
        if (index.column() == 1) {
            if (role != Qt::DisplayRole || row.hunk < 0 || starts.at(row.hunk) != index.row()) {
                return {};
            }
            if (!row.present[0]) {
                return QCoreApplication::translate("FileComparison", "Added");
            }
            if (!row.present[1]) {
                return QCoreApplication::translate("FileComparison", "Removed");
            }
            return QCoreApplication::translate("FileComparison", "Changed");
        }
        const int side = index.column() / 2;
        if (role == Qt::DisplayRole && row.present[side]) {
            return QStringLiteral("%1  %2").arg(row.line[side], 5).arg(row.text[side]);
        }
        if (role == Qt::ToolTipRole && row.present[side]) {
            return row.text[side].toHtmlEscaped().prepend(QStringLiteral("<pre>")).append(QStringLiteral("</pre>"));
        }
        if (role == Qt::FontRole) {
            return QFontDatabase::systemFont(QFontDatabase::FixedFont);
        }
        if (role == Qt::BackgroundRole && row.hunk >= 0) {
            const QColor base = palette.color(QPalette::Base);
            const QColor ink = row.present[side] ? (side == 0 ? QColor(208, 65, 68) : QColor(37, 155, 100))
                                                 : palette.color(QPalette::Mid);
            const auto tint = [base, ink](auto channel) { return ((base.*channel)() * 4 + (ink.*channel)()) / 5; };
            return QColor(tint(&QColor::red), tint(&QColor::green), tint(&QColor::blue));
        }
        return {};
    }

    void reset(const std::array<QList<QByteArray>, 2> &lines, const QList<Hunk> &hunks)
    {
        beginResetModel();
        rows.clear();
        starts.clear();
        const auto display = [](QByteArray line) {
            const bool terminated = line.endsWith('\n');
            if (terminated) {
                line.chop(1);
            }
            QString text = QString::fromUtf8(line).replace(QLatin1Char('\t'), QStringLiteral("    "));
            if (!terminated) {
                text += QCoreApplication::translate("FileComparison", "  [no final newline]");
            }
            return text;
        };
        std::array<qsizetype, 2> pos{};
        const auto append = [&](bool left, bool right, int hunk) {
            Row row{{}, {pos[0] + 1, pos[1] + 1}, hunk, {left, right}};
            for (int side = 0; side < 2; ++side) {
                if (row.present[side]) {
                    row.text[side] = display(lines[side].at(pos[side]++));
                }
            }
            rows.append(std::move(row));
        };
        for (int i = 0; i < hunks.size(); ++i) {
            const auto &hunk = hunks.at(i);
            while (pos[0] < hunk.first[0]) {
                append(true, true, -1);
            }
            starts.append(rows.size());
            for (qsizetype offset = 0; offset < std::max(hunk.count[0], hunk.count[1]); ++offset) {
                append(offset < hunk.count[0], offset < hunk.count[1], i);
            }
        }
        while (pos[0] < lines[0].size() && pos[1] < lines[1].size()) {
            append(true, true, -1);
        }
        endResetModel();
    }

    QList<Row> rows;
    QList<int> starts;
    QPalette palette;
};

class Comparison final : public QDialog {
public:
    Comparison(ApplicationShell &shell, CompareInput left, CompareInput right);
    ~Comparison() override;

private:
    void recalculate();
    void readDiff(int code);
    void selectHunk(int hunk);
    void refreshControls();
    bool canSave(int target) const;
    void apply(int target);
    void undo();
    void save(int side);
    void reject() override;

    std::array<Side, 2> sides_;
    std::array<QList<QByteArray>, 2> lines_;
    std::array<QLabel *, 2> labels_{};
    std::array<QPushButton *, 2> saves_{};
    std::array<QPushButton *, 2> arrows_{};
    QTemporaryDir temporary_;
    QProcess *diff_;
    QTableView *table_;
    AlignedRows *model_;
    QLabel *status_;
    QPushButton *previous_;
    QPushButton *next_;
    QPushButton *undo_;
    struct UndoChange {
        int target;
        qsizetype start;
        qsizetype inserted;
        QByteArray removed;
    };
    QList<Hunk> hunks_;
    QList<UndoChange> undoHistory_;
    int selected_ = -1;
    bool computing_ = false;
    bool ready_ = false;
};

Comparison::Comparison(ApplicationShell &shell, CompareInput left, CompareInput right)
    : QDialog(&shell), diff_(new QProcess(this)), table_(new QTableView(this)),
      model_(new AlignedRows(this)), status_(new QLabel(this)), previous_(new QPushButton(QCoreApplication::translate("FileComparison", "Previous difference"), this)),
      next_(new QPushButton(QCoreApplication::translate("FileComparison", "Next difference"), this)), undo_(new QPushButton(QCoreApplication::translate("FileComparison", "Undo hunk"), this))
{
    setObjectName(QStringLiteral("file-comparison"));
    setWindowTitle(QCoreApplication::translate("FileComparison", "Compare Files"));
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowModality(Qt::WindowModal);
    resize(1120, 650);
    sides_[0].input = std::move(left);
    sides_[1].input = std::move(right);

    auto *layout = new QVBoxLayout(this);
    auto *headers = new QHBoxLayout;
    auto *transfer = new QHBoxLayout;
    for (int side = 0; side < 2; ++side) {
        auto *column = new QVBoxLayout;
        labels_[side] = new QLabel(this);
        labels_[side]->setTextFormat(Qt::PlainText);
        labels_[side]->setTextInteractionFlags(Qt::TextSelectableByMouse);
        labels_[side]->setWordWrap(true);
        saves_[side] = new QPushButton(side == 0 ? QCoreApplication::translate("FileComparison", "Save left") : QCoreApplication::translate("FileComparison", "Save right"), this);
        saves_[side]->setObjectName(side == 0 ? QStringLiteral("compare-save-left") : QStringLiteral("compare-save-right"));
        saves_[side]->setVisible(!sides_[side].input.localPath.isEmpty());
        column->addWidget(labels_[side]);
        column->addWidget(saves_[side], 0, Qt::AlignLeft);
        if (side == 1) {
            headers->addLayout(transfer);
        }
        headers->addLayout(column, 1);
        arrows_[side] = new QPushButton(side == 0 ? QStringLiteral("←") : QStringLiteral("→"), this);
        arrows_[side]->setObjectName(side == 0 ? QStringLiteral("compare-apply-left") : QStringLiteral("compare-apply-right"));
        const QString tip = side == 0 ? QCoreApplication::translate("FileComparison", "Apply selected difference to the left") : QCoreApplication::translate("FileComparison", "Apply selected difference to the right");
        arrows_[side]->setToolTip(tip);
        arrows_[side]->setAccessibleName(tip);
        arrows_[side]->setFixedWidth(42);
        transfer->addWidget(arrows_[side]);
        connect(arrows_[side], &QPushButton::clicked, this, [this, side] { apply(side); });
        connect(saves_[side], &QPushButton::clicked, this, [this, side] { save(side); });
    }
    layout->addLayout(headers);
    model_->palette = palette();
    table_->setObjectName(QStringLiteral("compare-aligned-rows"));
    table_->setModel(model_);
    table_->setSelectionBehavior(QAbstractItemView::SelectItems);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setShowGrid(false);
    table_->setWordWrap(false);
    table_->setTextElideMode(Qt::ElideRight);
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table_->verticalHeader()->setDefaultSectionSize(QFontMetrics(QFontDatabase::systemFont(QFontDatabase::FixedFont)).height() + 6);
    table_->horizontalHeader()->hide();
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table_->setColumnWidth(1, 90);
    layout->addWidget(table_, 1);

    auto *actions = new QHBoxLayout;
    auto *close = new QPushButton(QCoreApplication::translate("FileComparison", "Close"), this);
    previous_->setObjectName(QStringLiteral("compare-previous"));
    next_->setObjectName(QStringLiteral("compare-next"));
    undo_->setObjectName(QStringLiteral("compare-undo"));
    status_->setObjectName(QStringLiteral("compare-status"));
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    actions->addWidget(previous_);
    actions->addWidget(next_);
    actions->addWidget(undo_);
    actions->addWidget(status_, 1);
    actions->addWidget(close);
    layout->addLayout(actions);
    connect(close, &QPushButton::clicked, this, &Comparison::reject);
    connect(previous_, &QPushButton::clicked, this, [this] { selectHunk(selected_ - 1); });
    connect(next_, &QPushButton::clicked, this, [this] { selectHunk(selected_ + 1); });
    connect(undo_, &QPushButton::clicked, this, &Comparison::undo);
    auto *undoKey = new QShortcut(QKeySequence::Undo, this);
    connect(undoKey, &QShortcut::activated, undo_, &QPushButton::click);
    connect(table_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex &index) {
        if (index.isValid() && !computing_) {
            selected_ = model_->rows.at(index.row()).hunk;
            refreshControls();
        }
    });
    connect(diff_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus exitStatus) {
        readDiff(exitStatus == QProcess::NormalExit ? code : -1);
    });
    connect(diff_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            readDiff(-1);
        }
    });

    for (auto &side : sides_) {
        const QString error = prepare(side, shell);
        if (!error.isEmpty()) {
            refreshControls();
            status_->setText(error);
            return;
        }
    }
    recalculate();
}

Comparison::~Comparison()
{
    // QProcess can emit finished while reaping a killed child. Leave the input
    // directory alive until the child is gone and detach callbacks first.
    disconnect(diff_, nullptr, this, nullptr);
    if (diff_->state() != QProcess::NotRunning) {
        diff_->kill();
        diff_->waitForFinished();
    }
    delete diff_;
}

void Comparison::recalculate()
{
    QStringList arguments;
    const QString format = QStringLiteral("%df %dn %dF %dN\n");
    for (const QString &kind : {QStringLiteral("old"), QStringLiteral("new"), QStringLiteral("changed")}) {
        arguments << QStringLiteral("--%1-group-format=").arg(kind) + format;
    }
    arguments << QStringLiteral("--unchanged-group-format=") << QStringLiteral("--");
    for (int side = 0; side < 2; ++side) {
        const QByteArray text = normalized(sides_[side].bytes);
        lines_[side] = splitLines(text);
        QFile file(temporary_.filePath(QString::number(side)));
        if (!temporary_.isValid() || !file.open(QIODevice::WriteOnly) || file.write(text) != text.size() || !file.flush()) {
            ready_ = false;
            refreshControls();
            status_->setText(QCoreApplication::translate("FileComparison", "Cannot prepare comparison input."));
            return;
        }
        arguments << file.fileName();
    }
    computing_ = true;
    refreshControls();
    const auto comparisonTools = QCoreApplication::applicationDirPath()
        + QStringLiteral("/usr/lib/vedit/file-compare");
    QString executable = QStandardPaths::findExecutable(QStringLiteral("diff"), {comparisonTools});
    if (executable.isEmpty()) {
        executable = QStandardPaths::findExecutable(QStringLiteral("diff"));
    }
    if (executable.isEmpty()) {
        readDiff(-1);
        return;
    }
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.remove(QStringLiteral("LD_LIBRARY_PATH"));
    diff_->setProcessEnvironment(environment);
    diff_->start(executable, arguments);
}

void Comparison::readDiff(int code)
{
    const QByteArray result = diff_->readAllStandardOutput();
    const QString error = QString::fromLocal8Bit(diff_->readAllStandardError()).trimmed();
    hunks_.clear();
    std::array<qsizetype, 2> end{};
    bool valid = code == 0 || code == 1;
    for (const auto &line : result.split('\n')) {
        if (line.isEmpty() || !valid) {
            continue;
        }
        const auto numbers = line.split(' ');
        if (numbers.size() != 4) {
            valid = false;
            break;
        }
        Hunk hunk{};
        for (int side = 0; side < 2; ++side) {
            bool startOk = false;
            bool countOk = false;
            const int start = numbers.at(2 * side).toInt(&startOk);
            const int count = numbers.at(2 * side + 1).toInt(&countOk);
            if (!startOk || !countOk || start < 1 || count < 0 || start - 1 < end[side]
                || start - 1 > lines_[side].size() || count > lines_[side].size() - (start - 1)) {
                valid = false;
                break;
            }
            hunk.first[side] = start - 1;
            hunk.count[side] = count;
        }
        if (!valid || hunk.first[0] - end[0] != hunk.first[1] - end[1]) {
            valid = false;
            break;
        }
        end = {hunk.first[0] + hunk.count[0], hunk.first[1] + hunk.count[1]};
        hunks_.append(hunk);
    }
    ready_ = valid && (code == 0 || !hunks_.isEmpty())
        && lines_[0].size() - end[0] == lines_[1].size() - end[1];
    if (ready_) {
        model_->reset(lines_, hunks_);
    }
    computing_ = false;
    if (ready_ && !hunks_.isEmpty()) {
        selectHunk(qBound(0, selected_, static_cast<int>(hunks_.size()) - 1));
    } else {
        selected_ = -1;
        refreshControls();
    }
    if (!ready_) {
        status_->setText(error.isEmpty()
                             ? QCoreApplication::translate("FileComparison", "Comparison could not run. GNU diff is required.")
                             : QCoreApplication::translate("FileComparison", "Comparison could not run: %1").arg(error));
    }
}

void Comparison::selectHunk(int hunk)
{
    if (hunk < 0 || hunk >= hunks_.size()) {
        return;
    }
    selected_ = hunk;
    const auto index = model_->index(model_->starts.at(hunk), 1);
    table_->setCurrentIndex(index);
    table_->scrollTo(index, QAbstractItemView::PositionAtCenter);
    refreshControls();
}

void Comparison::refreshControls()
{
    const bool selected = ready_ && !computing_ && selected_ >= 0;
    for (int side = 0; side < 2; ++side) {
        const auto &state = sides_[side];
        QString name = state.input.label;
        if (state.input.missing) {
            name += QCoreApplication::translate("FileComparison", "  ·  Absent at this revision");
        } else if (!state.writable()) {
            name += QCoreApplication::translate("FileComparison", "  ·  Read-only");
        }
        if (state.dirty()) {
            name += QStringLiteral("  *");
        }
        labels_[side]->setText(name);
        arrows_[side]->setEnabled(selected && state.writable());
        saves_[side]->setEnabled(canSave(side));
    }
    previous_->setEnabled(selected && selected_ > 0);
    next_->setEnabled(ready_ && !computing_ && selected_ + 1 < hunks_.size());
    undo_->setEnabled(!computing_ && !undoHistory_.isEmpty());
    if (computing_) {
        status_->setText(QCoreApplication::translate("FileComparison", "Comparing…"));
    } else if (ready_) {
        status_->setText(hunks_.isEmpty() ? QCoreApplication::translate("FileComparison", "No differences")
                         : selected ? QCoreApplication::translate("FileComparison", "Difference %1 of %2").arg(selected_ + 1).arg(hunks_.size())
                                    : QCoreApplication::translate("FileComparison", "%1 differences").arg(hunks_.size()));
    }
}

void Comparison::apply(int target)
{
    if (!arrows_[target]->isEnabled()) {
        return;
    }
    const auto &hunk = hunks_.at(selected_);
    const int source = 1 - target;
    auto &side = sides_[target];
    const bool hasBom = side.bytes.startsWith(bom);
    const bool crlf = side.bytes.contains("\r\n") || (side.bytes.isEmpty() && sides_[source].bytes.contains("\r\n"));
    const auto original = splitLines(hasBom ? side.bytes.mid(bom.size()) : side.bytes);
    QByteArray next = hasBom ? bom : QByteArray();
    for (qsizetype i = 0; i < hunk.first[target]; ++i) {
        next += original.at(i);
    }
    const qsizetype splice = next.size();
    QByteArray removed;
    const qsizetype end = hunk.first[target] + hunk.count[target];
    for (qsizetype i = hunk.first[target]; i < end; ++i) {
        removed += original.at(i);
    }
    for (qsizetype i = hunk.first[source]; i < hunk.first[source] + hunk.count[source]; ++i) {
        QByteArray line = lines_[source].at(i);
        if (crlf && line.endsWith('\n')) {
            line.insert(line.size() - 1, '\r');
        }
        next += line;
    }
    const qsizetype inserted = next.size() - splice;
    for (qsizetype i = end; i < original.size(); ++i) {
        next += original.at(i);
    }
    if (next.size() > maxBytes || next.count('\n') >= maxLines) {
        status_->setText(QCoreApplication::translate("FileComparison", "Applying this difference would exceed the text comparison limit."));
        return;
    }
    if (!side.update(next)) {
        status_->setText(QCoreApplication::translate("FileComparison", "The open editor buffer changed. Reopen the comparison before applying a difference."));
        return;
    }
    undoHistory_.append({target, splice, inserted, std::move(removed)});
    recalculate();
}

void Comparison::undo()
{
    if (!undo_->isEnabled()) {
        return;
    }
    const auto &[target, start, inserted, removed] = undoHistory_.constLast();
    QByteArray restored = sides_[target].bytes;
    restored.replace(start, inserted, removed);
    if (!sides_[target].update(restored)) {
        status_->setText(QCoreApplication::translate("FileComparison", "The open editor buffer changed. Undo could not be applied."));
        return;
    }
    undoHistory_.removeLast();
    recalculate();
}

bool Comparison::canSave(int target) const
{
    const auto &side = sides_[target];
    return !computing_ && sides_[0].clean && sides_[1].clean && side.dirty() && side.writable();
}

void Comparison::save(int target)
{
    if (!canSave(target)) {
        return;
    }
    auto &side = sides_[target];
    const auto current = readLocal(side.input.localPath);
    if (!side.bufferCurrent() || !current || *current != side.disk
        || QFileInfo(side.input.localPath).canonicalFilePath() != side.input.localPath) {
        status_->setText(QCoreApplication::translate("FileComparison", "%1 changed outside this comparison. Reopen it before saving.").arg(side.input.label));
        return;
    }
    QSaveFile file(side.input.localPath);
    if (!file.open(QIODevice::WriteOnly) || file.write(side.bytes) != side.bytes.size() || !file.commit()) {
        status_->setText(QCoreApplication::translate("FileComparison", "Cannot save %1: %2").arg(side.input.label, file.errorString()));
        return;
    }
    side.disk = side.bytes;
    side.clean = side.bytes;
    side.wasModified = false;
    if (side.provider) {
        side.provider->setComparisonModified(side.editor, false);
    }
    refreshControls();
    status_->setText(QCoreApplication::translate("FileComparison", "Saved %1").arg(side.input.label));
}

void Comparison::reject()
{
    if (sides_[0].dirty() || sides_[1].dirty()) {
        if (QMessageBox::question(this, QCoreApplication::translate("FileComparison", "Discard comparison changes?"),
                                  QCoreApplication::translate("FileComparison", "Discard the applied differences that have not been saved?"),
                                  QMessageBox::Discard | QMessageBox::Cancel,
                                  QMessageBox::Cancel) != QMessageBox::Discard) {
            return;
        }
        for (auto &side : sides_) {
            if (side.dirty() && !side.update(*side.clean)) {
                status_->setText(QCoreApplication::translate("FileComparison", "The open editor buffer changed. Its current text has been preserved."));
            }
        }
    }
    QDialog::reject();
}

} // namespace

void showFileComparison(ApplicationShell &shell, CompareInput left, CompareInput right)
{
    for (auto *input : {&left, &right}) {
        if (!input->localPath.isEmpty()) {
            const QFileInfo info(input->localPath);
            if (input->label.isEmpty()) {
                input->label = info.absoluteFilePath();
            }
            const QString canonical = info.canonicalFilePath();
            input->localPath = canonical.isEmpty() ? info.absoluteFilePath() : canonical;
        }
    }
    std::error_code identityError;
    if (!left.localPath.isEmpty() && !right.localPath.isEmpty()
        && (left.localPath == right.localPath
            || std::filesystem::equivalent(QFile::encodeName(left.localPath).constData(),
                                           QFile::encodeName(right.localPath).constData(), identityError))) {
        QMessageBox::information(&shell, QCoreApplication::translate("FileComparison", "Compare Files"), QCoreApplication::translate("FileComparison", "Choose two different files to compare."));
        return;
    }
    auto *comparison = new Comparison(shell, std::move(left), std::move(right));
    comparison->open();
}

void chooseFilesToCompare(ApplicationShell &shell, const QString &first)
{
    const QString left = first.isEmpty() ? QFileDialog::getOpenFileName(&shell, QCoreApplication::translate("FileComparison", "Choose left file"), shell.projectRoot())
                                        : first;
    if (left.isEmpty()) {
        return;
    }
    const QString right = QFileDialog::getOpenFileName(&shell, QCoreApplication::translate("FileComparison", "Choose right file"), QFileInfo(left).absolutePath());
    if (!right.isEmpty()) {
        showFileComparison(shell, {left, {}, {}, false}, {right, {}, {}, false});
    }
}

} // namespace vedit::app
