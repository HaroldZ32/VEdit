#include "filelist_path_completion.h"

#include <KTextEditor/CodeCompletionModel>
#include <KTextEditor/CodeCompletionModelControllerInterface>
#include <KTextEditor/Document>
#include <KTextEditor/View>

#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QIcon>
#include <QPromise>
#include <QRegularExpression>
#include <QThreadPool>

#include <algorithm>
#include <optional>

namespace vedit::editor {
namespace {

struct PathContext {
    KTextEditor::Range component;
    QString directory;
    QString prefix;
    bool directoriesOnly;
    bool followedBySlash;
    bool plusSeparated;
    bool barePath;
};

bool isFilelist(KTextEditor::View *view)
{
    const QUrl url = view->document()->url();
    return url.isLocalFile() && QFileInfo(url.toLocalFile()).suffix().compare(u"f", Qt::CaseInsensitive) == 0;
}

bool expandsVariable(QStringView path)
{
    static const QRegularExpression variable(QStringLiteral(R"(\$[({A-Za-z_]|\$\$)"));
    return variable.matchView(path).hasMatch();
}

std::optional<PathContext> pathContext(KTextEditor::View *view, KTextEditor::Cursor caret)
{
    auto *document = view->document();
    if (!caret.isValid() || !isFilelist(view) || !document->isReadWrite() || view->selection()
        || view->cursorPositions().size() > 1 || document->lineLength(caret.line()) > 4096) {
        return std::nullopt;
    }
    const QString line = document->line(caret.line());
    const int column = caret.column();
    qsizetype start = 0;
    qsizetype end = line.size();
    for (qsizetype index = 0; index < end; ++index) {
        if ((line[index] == u'#' && (index == 0 || line[index - 1].isSpace()))
            || (line[index] == u'/' && index + 1 < end && line[index + 1] == u'/')) {
            end = index;
            if (column >= end) {
                return std::nullopt;
            }
        }
    }
    while (start < end && line[start].isSpace()) {
        ++start;
    }
    const QStringView argument = QStringView(line).sliced(start, end - start);
    const bool plusSeparated = argument.startsWith(u"+incdir+");
    bool directoriesOnly = false;
    bool barePath = false;
    if (plusSeparated) {
        directoriesOnly = true;
        start += 8;
        while (start <= end) {
            const qsizetype next = line.indexOf(u'+', start);
            if (next < 0 || next >= end) {
                break;
            }
            if (column <= next) {
                end = next;
                break;
            }
            start = next + 1;
        }
    } else if (argument.startsWith(u"-I")) {
        directoriesOnly = true;
        start += 2;
    } else if (argument.startsWith(u"-f") || argument.startsWith(u"-F")) {
        if (argument.size() < 3 || !argument[2].isSpace()) {
            return std::nullopt;
        }
        start += 2;
    } else if (argument.startsWith(u'+') || argument.startsWith(u'-')) {
        return std::nullopt;
    } else {
        barePath = true;
    }
    while (start < end && start < column && line[start].isSpace()) {
        ++start;
    }
    while (end > column && end > start && line[end - 1].isSpace()) {
        --end;
    }
    if (column < start || column > end) {
        return std::nullopt;
    }
    const QStringView path = QStringView(line).sliced(start, end - start);
    // Variable expansion belongs to the project evaluator and depends on its
    // imported environment and project settings. Leave those tokens intact.
    if (expandsVariable(path)) {
        return std::nullopt;
    }
    const qsizetype slashBefore = column > start ? path.left(column - start).lastIndexOf(u'/') : -1;
    const qsizetype componentStart = start + slashBefore + 1;
    const qsizetype slashAfter = path.indexOf(u'/', column - start);
    const qsizetype componentEnd = slashAfter < 0 ? end : start + slashAfter;
    QString directory = path.left(slashBefore + 1).toString();
    const bool homeAllowed = start == 0 || line[start - 1].isSpace() || line[start - 1] == u'+';
    if (homeAllowed && directory.startsWith(u"~/")) {
        directory.replace(0, 1, QDir::homePath());
    }
    const QDir base(QFileInfo(document->url().toLocalFile()).absolutePath());
    const QString resolved = base.absoluteFilePath(directory.isEmpty() ? QStringLiteral(".") : directory);
    return PathContext{KTextEditor::Range(caret.line(), int(componentStart), caret.line(), int(componentEnd)),
                       QDir::cleanPath(resolved), line.mid(componentStart, column - componentStart),
                       directoriesOnly || slashAfter >= 0, slashAfter >= 0, plusSeparated,
                       barePath && directory.isEmpty()};
}

QStringList findMatches(const PathContext &context, const QPromise<QStringList> &promise)
{
    if (promise.isCanceled()) {
        return {};
    }
    QElapsedTimer timer;
    timer.start();
    auto filters = QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden;
    if (!context.directoriesOnly) {
        filters |= QDir::Files;
    }
    QDirIterator entries(context.directory, filters);
    QStringList matches;
    int examined = 0;
    while (!promise.isCanceled() && examined < 8192 && matches.size() < 192 && timer.elapsed() < 150
           && entries.hasNext()) {
        entries.next();
        ++examined;
        const QString name = entries.fileName();
        if (name.isEmpty() || (!context.prefix.startsWith(u'.') && name.startsWith(u'.'))
            || !name.startsWith(context.prefix) || name.contains(u'\n') || name.contains(u'\r')
            || name.front().isSpace() || name.back().isSpace() || name == u"~" || expandsVariable(name)
            || name.contains(u" #") || name.contains(u"\t#") || (context.plusSeparated && name.contains(u'+'))
            || (context.barePath && (name.startsWith(u'+') || name.startsWith(u'-') || name.startsWith(u'#')))) {
            continue;
        }
        const QFileInfo entry = entries.fileInfo();
        if (entry.isDir()) {
            matches.append(name + u'/');
        } else if (!context.directoriesOnly && entry.isFile()) {
            matches.append(name);
        }
    }
    std::sort(matches.begin(), matches.end());
    return matches;
}

class FilelistPathCompletion final : public KTextEditor::CodeCompletionModel,
                                     public KTextEditor::CodeCompletionModelControllerInterface {
    Q_OBJECT
    Q_INTERFACES(KTextEditor::CodeCompletionModelControllerInterface)

public:
    explicit FilelistPathCompletion(KTextEditor::View *view)
        : CodeCompletionModel(view), watcher_(this)
    {
        setObjectName(QStringLiteral("filelist-path-completion"));
        connect(&watcher_, &QFutureWatcherBase::finished, this, [this] {
            if (!watcher_.isCanceled()) {
                setItems(watcher_.result());
            }
        });
        const auto updateRegistration = [this, view] {
            const bool eligible = isFilelist(view);
            if (eligible == registered_) {
                return;
            }
            registered_ = eligible;
            if (eligible) {
                view->registerCompletionModel(this);
            } else {
                watcher_.cancel();
                view->unregisterCompletionModel(this);
                setItems({});
            }
        };
        connect(view->document(), &KTextEditor::Document::documentUrlChanged, this, updateRegistration);
        updateRegistration();
    }

    ~FilelistPathCompletion() override { watcher_.cancel(); }

    void aborted(KTextEditor::View *) override { watcher_.cancel(); }

    bool shouldStartCompletion(KTextEditor::View *view, const QString &text, bool userInsertion,
                               const KTextEditor::Cursor &cursor) override
    {
        return !text.isEmpty() && !text.back().isSpace() && (userInsertion || text.endsWith(u'/'))
            && pathContext(view, cursor).has_value();
    }

    KTextEditor::Range completionRange(KTextEditor::View *view, const KTextEditor::Cursor &cursor) override
    {
        const auto context = pathContext(view, cursor);
        return context ? context->component : KTextEditor::Range::invalid();
    }

    bool shouldAbortCompletion(KTextEditor::View *view, const KTextEditor::Range &range,
                              const QString &) override
    {
        const auto context = pathContext(view, view->cursorPosition());
        return !context || range != context->component || context->directory != directory_
            || context->directoriesOnly != directoriesOnly_;
    }

    void completionInvoked(KTextEditor::View *view, const KTextEditor::Range &, InvocationType) override
    {
        watcher_.cancel();
        setItems({});
        const auto context = pathContext(view, view->cursorPosition());
        if (!context) {
            return;
        }
        directory_ = context->directory;
        directoriesOnly_ = context->directoriesOnly;
        emit waitForReset();
        QPromise<QStringList> promise;
        watcher_.setFuture(promise.future());
        QThreadPool::globalInstance()->start([context = *context, promise = std::move(promise)]() mutable {
            promise.start();
            promise.addResult(findMatches(context, promise));
            promise.finish();
        });
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() >= items_.size()) {
            return {};
        }
        const QString &item = items_.at(index.row());
        if (index.column() == Name && role == Qt::DisplayRole) {
            return item;
        }
        if (index.column() == Icon && role == Qt::DecorationRole) {
            return QIcon::fromTheme(item.endsWith(u'/') ? QStringLiteral("folder") : QStringLiteral("text-x-generic"));
        }
        return {};
    }

    void executeCompletionItem(KTextEditor::View *view, const KTextEditor::Range &range,
                               const QModelIndex &item) const override
    {
        const auto context = pathContext(view, view->cursorPosition());
        if (!context || range != context->component || context->directory != directory_ || !item.isValid()
            || item.row() >= items_.size()) {
            return;
        }
        QString replacement = items_.at(item.row());
        if (!replacement.startsWith(context->prefix)) {
            return;
        }
        if (context->followedBySlash) {
            replacement.chop(1);
        }
        if (view->document()->replaceText(range, replacement)) {
            view->setCursorPosition(KTextEditor::Cursor(range.start().line(),
                range.start().column() + replacement.size() + (context->followedBySlash ? 1 : 0)));
        }
    }

private:
    void setItems(QStringList items)
    {
        beginResetModel();
        items_ = std::move(items);
        setRowCount(items_.size());
        endResetModel();
    }

    QFutureWatcher<QStringList> watcher_;
    QStringList items_;
    QString directory_;
    bool directoriesOnly_ = false;
    bool registered_ = false;
};

} // namespace

void installFilelistPathCompletion(KTextEditor::View *view)
{
    new FilelistPathCompletion(view);
}

} // namespace vedit::editor

#include "filelist_path_completion.moc"
