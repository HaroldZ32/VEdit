#include "project_replace.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMetaObject>
#include <QRegularExpression>
#include <QStringConverter>
#include <QStringList>
#include <QThread>

#include <algorithm>
#include <utility>

#if defined(VEDIT_SEARCH_HAVE_EDITOR_CORE)
#include "kernel/atomic_save.h"
#endif

namespace vedit::workspace::search {

namespace {

// One character as the Rust matcher counts it (a Unicode scalar value). QString
// stores UTF-16, so an astral character is two code units but one "character"
// in the reported columns.
int codePointLength(const QString &line, int utf16Index)
{
    if (utf16Index + 1 < line.size() && line.at(utf16Index).isHighSurrogate() &&
        line.at(utf16Index + 1).isLowSurrogate()) {
        return 2;
    }
    return 1;
}

bool replaceFailed(TextReplaceResult *result, const QString &message)
{
    result->ok = false;
    result->error = message;
    return false;
}

QString hitFileLabel(const ReplaceHit &hit)
{
    const QString name = QFileInfo(hit.filePath).fileName();
    return name.isEmpty() ? hit.filePath : name;
}

// Re-checks the matcher's own range against the CURRENT line (High 2).
//
// The project engine is the Rust `regex` crate: a literal needle is escaped and
// compiled by the same engine, and whole-word wraps the pattern in
// `\b(?:…)\b` (crates/vd-search/src/lib.rs, compile_matcher). This mirrors
// that construction so a hit is only applied when the text at its range is
// still what the matcher reported. It is a validator, never a second search:
// no result is produced here.
//
// An empty pattern is NOT a "validation disabled" mode (b2-repair-r2): the
// matcher is then invalid and the caller refuses. There is no code path where
// "nothing to validate" means "write anyway".
class RangeMatcher
{
public:
    explicit RangeMatcher(const ReplaceQuery &query)
    {
        if (query.pattern.isEmpty()) {
            return;
        }
        wanted_ = true;
        QString pattern =
            query.regex ? query.pattern : QRegularExpression::escape(query.pattern);
        if (query.wholeWords) {
            pattern = QStringLiteral("\\b(?:%1)\\b").arg(pattern);
        }
        QRegularExpression::PatternOptions options =
            QRegularExpression::UseUnicodePropertiesOption;
        if (!query.caseSensitive) {
            options |= QRegularExpression::CaseInsensitiveOption;
        }
        expression_ = QRegularExpression(pattern, options);
    }

    // False when no (non-empty) query was supplied or the expression cannot be
    // compiled: the caller must refuse rather than write without verification.
    bool valid() const { return wanted_ && expression_.isValid(); }

    bool matchesAt(const QString &line, int utf16Start, int utf16End) const
    {
        const QRegularExpressionMatch match = expression_.match(line, utf16Start);
        return match.hasMatch() && match.capturedStart(0) == utf16Start &&
               match.capturedEnd(0) == utf16End;
    }

private:
    bool wanted_ = false;
    QRegularExpression expression_;
};

} // namespace

TextReplaceResult applyReplaceRanges(const QString &text, const QList<ReplaceHit> &hits,
                                     const QString &replacement)
{
    return applyReplaceRanges(text, hits, replacement, ReplaceQuery());
}

TextReplaceResult applyReplaceRanges(const QString &text, const QList<ReplaceHit> &hits,
                                     const QString &replacement, const ReplaceQuery &query)
{
    TextReplaceResult result;
    result.text = text;
    if (hits.isEmpty()) {
        result.ok = true;
        return result;
    }

    QStringList lines = text.split(QLatin1Char('\n'));
    QHash<int, QList<ReplaceHit>> hitsByLine;
    for (const ReplaceHit &hit : hits) {
        if (hit.line < 1 || hit.line > lines.size() || hit.column < 1 || hit.matchLength < 1) {
            replaceFailed(&result, QStringLiteral("replace: hit %1:%2:%3 is outside the file")
                                        .arg(hitFileLabel(hit))
                                        .arg(hit.line)
                                        .arg(hit.column));
            return result;
        }
        hitsByLine[hit.line - 1].append(hit);
    }

    // Checked AFTER the range bounds so a range error still names the file and
    // line, but BEFORE any range is applied. An empty pattern is refused: it
    // used to mean "no validation", which let an emptied query write a stale
    // range unchecked (probe E3/D5).
    const RangeMatcher matcher(query);
    if (!matcher.valid()) {
        replaceFailed(&result,
                      query.pattern.isEmpty()
                          ? QStringLiteral("replace: %1: the operation has no query, so the "
                                           "range cannot be verified; refusing to write without "
                                           "verification")
                                .arg(hitFileLabel(hits.first()))
                          : QStringLiteral("replace: the query cannot be re-checked against the "
                                           "file (invalid regular expression); refusing to write "
                                           "without verification"));
        return result;
    }

    int replaced = 0;
    for (auto entry = hitsByLine.begin(); entry != hitsByLine.end(); ++entry) {
        const int lineIndex = entry.key();
        QList<ReplaceHit> lineHits = entry.value();
        std::sort(lineHits.begin(), lineHits.end(),
                  [](const ReplaceHit &left, const ReplaceHit &right) {
                      return left.column < right.column;
                  });

        QString line = lines.at(lineIndex);
        // The Rust matcher strips a trailing '\r' before matching, so columns
        // are relative to the line WITHOUT it; the separator is restored.
        const bool carriageReturn = line.endsWith(QLatin1Char('\r'));
        if (carriageReturn) {
            line.chop(1);
        }

        // b2-repair-r3: the per-hit CONTENT identity. The scanner reported this
        // line's text TOGETHER with the hit; if the line no longer holds it, the
        // hit was produced from a different document state — even when the same
        // token happens to sit at the old coordinates (a line inserted above the
        // match). Refused before a single character is rebuilt.
        QByteArray currentFingerprint;
        for (const ReplaceHit &hit : lineHits) {
            if (hit.lineFingerprint.isEmpty()) {
                continue; // no per-hit evidence (hand-built ranges): range check only
            }
            if (currentFingerprint.isEmpty()) {
                currentFingerprint = matchedLineFingerprint(line);
            }
            if (currentFingerprint != hit.lineFingerprint) {
                replaceFailed(&result,
                              QStringLiteral("replace: %1: line %2 no longer holds the text the "
                                             "search read (the document changed); nothing was "
                                             "written to that file")
                                  .arg(hitFileLabel(hit))
                                  .arg(hit.line));
                return result;
            }
        }

        QString rebuilt;
        rebuilt.reserve(line.size() + replacement.size());
        int utf16 = 0;
        int characters = 0;
        for (const ReplaceHit &hit : lineHits) {
            const int target = hit.column - 1;
            if (target < characters) {
                replaceFailed(&result,
                              QStringLiteral("replace: %1: overlapping hits on line %2")
                                  .arg(hitFileLabel(hit))
                                  .arg(lineIndex + 1));
                return result;
            }
            while (characters < target && utf16 < line.size()) {
                const int length = codePointLength(line, utf16);
                rebuilt.append(line.mid(utf16, length));
                utf16 += length;
                ++characters;
            }
            if (characters != target) {
                replaceFailed(&result,
                              QStringLiteral("replace: %1: line %2 is shorter than the search "
                                             "result (the file changed)")
                                  .arg(hitFileLabel(hit))
                                  .arg(lineIndex + 1));
                return result;
            }
            // High 2: the range is only valid while the text AT it is exactly
            // the match the matcher reported. A stale range (the file changed
            // after the scan) is refused with the file and line named; never
            // applied to whatever characters now sit at that offset.
            int matchEnd = utf16;
            for (int skipped = 0; skipped < hit.matchLength; ++skipped) {
                if (matchEnd >= line.size()) {
                    replaceFailed(&result,
                                  QStringLiteral("replace: %1: the match on line %2 runs past the "
                                                 "end of the line (the file changed)")
                                      .arg(hitFileLabel(hit))
                                      .arg(lineIndex + 1));
                    return result;
                }
                matchEnd += codePointLength(line, matchEnd);
            }
            if (!matcher.matchesAt(line, utf16, matchEnd)) {
                replaceFailed(&result,
                              QStringLiteral("replace: %1: line %2 column %3 no longer holds "
                                             "\"%4\" (the file changed); nothing was written to "
                                             "that file")
                                  .arg(hitFileLabel(hit))
                                  .arg(hit.line)
                                  .arg(hit.column)
                                  .arg(query.pattern));
                return result;
            }
            rebuilt.append(replacement);
            ++replaced;
            utf16 = matchEnd;
            characters += hit.matchLength;
        }
        rebuilt.append(line.mid(utf16));
        if (carriageReturn) {
            rebuilt.append(QLatin1Char('\r'));
        }
        lines[lineIndex] = rebuilt;
    }

    result.text = lines.join(QLatin1Char('\n'));
    result.replaced = replaced;
    result.ok = true;
    return result;
}

QString readDocumentLine(const QString &absolutePath, int line, QString *error)
{
    const auto fail = [error](const QString &why) {
        if (error != nullptr) {
            *error = why;
        }
        return QString();
    };
    if (line < 1) {
        return fail(QStringLiteral("line %1 is not a 1-based line number").arg(line));
    }
    QFile input(absolutePath);
    if (!input.open(QIODevice::ReadOnly)) {
        return fail(QStringLiteral("cannot read %1: %2").arg(absolutePath, input.errorString()));
    }
    const QByteArray bytes = input.readAll();
    input.close();
    if (bytes.contains('\0')) {
        // The writer skips a document with a NUL byte; there is no line identity
        // to compare for a file it would never rewrite.
        return fail(QStringLiteral("%1 is not a text document (NUL byte)").arg(absolutePath));
    }
    // The SAME decoder the writer uses: `ConvertInitialBom` keeps line 1's BOM so
    // the character columns agree, and a document that does not decode is one the
    // writer refuses to rewrite.
    QStringDecoder decoder(QStringConverter::Utf8,
                           QStringConverter::Flag::Stateless |
                               QStringConverter::Flag::ConvertInitialBom);
    const QString content = decoder.decode(bytes);
    if (decoder.hasError()) {
        return fail(QStringLiteral("%1 is not valid UTF-8").arg(absolutePath));
    }
    // Split exactly like applyReplaceRanges: each '\n' starts a new line (a
    // trailing separator yields a final empty line), so the index is the same
    // 1-based `hit.line` the writer uses.
    const QStringList lines = content.split(QLatin1Char('\n'));
    if (line > lines.size()) {
        return fail(QStringLiteral("%1 has no line %2 (the document changed)")
                        .arg(absolutePath)
                        .arg(line));
    }
    return lines.at(line - 1);
}

FileRewriter atomicSaveRewriter()
{
#if defined(VEDIT_SEARCH_HAVE_EDITOR_CORE)
    return [](const QString &path, const QByteArray &contents) {
        const vedit::editor::AtomicSaveResult saved =
            vedit::editor::atomicSaveText(path, contents);
        ReplaceWriteResult result;
        result.ok = saved.ok;
        result.error = saved.error;
        return result;
    };
#else
    return FileRewriter();
#endif
}

ProjectReplacer::ProjectReplacer(QObject *parent)
    : QObject(parent)
    , cancelRequested_(std::make_shared<std::atomic_bool>(false))
{
}

ProjectReplacer::~ProjectReplacer()
{
    // Same lifetime rule as GlobalSearch: the worker only reads the per-run
    // cancellation token, and the destructor waits for it to return.
    cancel();
    if (thread_ != nullptr) {
        thread_->wait();
        QObject::disconnect(thread_, &QThread::finished, thread_, &QObject::deleteLater);
        QCoreApplication::removePostedEvents(thread_);
        delete thread_;
        thread_ = nullptr;
    }
    QCoreApplication::removePostedEvents(this);
}

bool ProjectReplacer::start(const QList<ReplaceHit> &hits, const QString &replacement,
                            FileRewriter rewriter, const ReplaceSnapshot &snapshot)
{
    if (thread_ != nullptr) {
        cancel();
        if (!thread_->wait(5000)) {
            return false;
        }
        QObject::disconnect(thread_, &QThread::finished, thread_, &QObject::deleteLater);
        QCoreApplication::removePostedEvents(thread_);
        delete thread_;
        thread_ = nullptr;
    }

    auto cancelTag = std::make_shared<std::atomic_bool>(false);
    cancelRequested_ = cancelTag;
    running_.store(true);
    ranOnWorkerThread_.store(false);
    const quint64 generation = ++generation_;
    QThread *thread = QThread::create(
        [this, hits, replacement, rewriter, snapshot, generation, cancelTag]() {
            run(hits, replacement, rewriter, snapshot, generation, cancelTag);
        });
    thread->setObjectName(QStringLiteral("vedit-project-replace"));
    thread_ = thread;
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
    return true;
}

void ProjectReplacer::cancel()
{
    if (cancelRequested_ != nullptr) {
        cancelRequested_->store(true);
    }
}

bool ProjectReplacer::isRunning() const
{
    return running_.load();
}

bool ProjectReplacer::ranOnWorkerThread() const
{
    return ranOnWorkerThread_.load();
}

void ProjectReplacer::run(const QList<ReplaceHit> &hits, const QString &replacement,
                          const FileRewriter &rewriter, const ReplaceSnapshot &snapshot,
                          quint64 generation,
                          const std::shared_ptr<std::atomic_bool> &cancelTag)
{
    if (QThread::currentThread() != QCoreApplication::instance()->thread()) {
        ranOnWorkerThread_.store(true);
    }

    ReplaceOutcome outcome;
    if (!rewriter) {
        outcome.error = QCoreApplication::translate(
            "VEdit", "replace is unavailable: this build does not link the atomic save path");
    } else if (snapshot.query.pattern.isEmpty()) {
        // b2-repair-r2: an empty pattern used to mean "no validation". It is
        // now a refusal, so no caller can reach the write side unverified.
        outcome.error = QCoreApplication::translate(
            "VEdit", "replace refused: the operation has no query, so the ranges cannot be "
                     "verified; nothing was written");
    } else if (snapshot.scanId == 0) {
        outcome.error = QCoreApplication::translate(
            "VEdit", "replace refused: the hit list has no frozen scan identity; nothing was "
                     "written");
    } else {
        bool refused = false;
        for (const ReplaceHit &hit : hits) {
            if (hit.scanId != snapshot.scanId) {
                outcome.error =
                    QCoreApplication::translate(
                        "VEdit",
                        "replace: %1: line %2 belongs to a different search; nothing was written "
                        "to that file (run the search again)")
                        .arg(hitFileLabel(hit))
                        .arg(hit.line);
                refused = true;
                break;
            }
        }
        if (refused) {
            outcome.ok = false;
        } else {
            // Group per file; the hits of one file are applied in ONE write.
            QHash<QString, QList<ReplaceHit>> byFile;
            for (const ReplaceHit &hit : hits) {
                byFile[hit.filePath].append(hit);
            }
            QStringList files = byFile.keys();
            files.sort();

            outcome.ok = true;
            for (const QString &file : files) {
                if (cancelTag->load(std::memory_order_relaxed)) {
                    outcome.ok = false;
                    outcome.cancelled = true;
                    break;
                }
                const QList<ReplaceHit> fileHits = byFile.value(file);
                const auto stampEntry = snapshot.files.constFind(file);
                if (stampEntry == snapshot.files.constEnd()) {
                    outcome.ok = false;
                    outcome.error = QCoreApplication::translate(
                                        "VEdit",
                                        "replace: %1: line %2 is not covered by the frozen scan "
                                        "snapshot; nothing was written to that file (run the "
                                        "search again)")
                                        .arg(hitFileLabel(fileHits.first()))
                                        .arg(fileHits.first().line);
                    break;
                }
                const ReplaceFileStamp &stamp = stampEntry.value();
                const QFileInfo before(file);
                if (stamp.writtenDuringScan) {
                    // b2-repair-r3 (the scan window): the file was written while
                    // the walk was reading it, so its hits describe the pre-save
                    // content while the stamp was captured from the post-save
                    // file. Comparing the two cannot see that, which is why the
                    // flag is part of the frozen identity.
                    outcome.ok = false;
                    outcome.error = QCoreApplication::translate(
                                        "VEdit",
                                        "replace: %1: line %2: the document was written while the "
                                        "scan was running, so the hit positions may be stale; "
                                        "nothing was written to that file (run the search again)")
                                        .arg(hitFileLabel(fileHits.first()))
                                        .arg(fileHits.first().line);
                    break;
                }
                if (before.size() != stamp.size ||
                    before.lastModified().toMSecsSinceEpoch() != stamp.lastModifiedMs) {
                    // The document changed between the scan and this write. A
                    // line shift can put the SAME text at the old line/column,
                    // so the text check alone would replace a different
                    // occurrence (probe C4/A3).
                    outcome.ok = false;
                    outcome.error = QCoreApplication::translate(
                                        "VEdit",
                                        "replace: %1: line %2: the document changed after the "
                                        "scan (size or modification time); nothing was written "
                                        "to that file (run the search again)")
                                        .arg(hitFileLabel(fileHits.first()))
                                        .arg(fileHits.first().line);
                    break;
                }
                QFile input(file);
                if (!input.open(QIODevice::ReadOnly)) {
                    outcome.skippedFiles.append(file);
                    continue;
                }
                const QByteArray bytes = input.readAll();
                input.close();
                // Re-check after the read: a save that landed during the read
                // must not be rewritten with ranges from the older document.
                const QFileInfo after(file);
                if (after.size() != stamp.size ||
                    after.lastModified().toMSecsSinceEpoch() != stamp.lastModifiedMs) {
                    outcome.ok = false;
                    outcome.error = QCoreApplication::translate(
                                        "VEdit",
                                        "replace: %1: line %2: the document changed while it was "
                                        "being read; nothing was written to that file (run the "
                                        "search again)")
                                        .arg(hitFileLabel(fileHits.first()))
                                        .arg(fileHits.first().line);
                    break;
                }
                if (bytes.contains('\0')) {
                    outcome.skippedFiles.append(file);
                    continue;
                }
                // High 4: the Rust matcher keeps a leading U+FEFF
                // (`String::from_utf8_lossy`), so its line-1 columns count the BOM.
                // `ConvertInitialBom` makes the decoder keep it too, which aligns
                // the columns and round-trips the BOM back on write; `Stateless`
                // alone silently drops it and shifts line 1 by one character.
                QStringDecoder decoder(
                    QStringConverter::Utf8,
                    QStringConverter::Flag::Stateless | QStringConverter::Flag::ConvertInitialBom);
                const QString content = decoder.decode(bytes);
                if (decoder.hasError()) {
                    // The Rust matcher decodes lossily; rewriting from lossy text
                    // would corrupt the bytes, so the file is reported, not touched.
                    outcome.skippedFiles.append(file);
                    continue;
                }
                const TextReplaceResult applied =
                    applyReplaceRanges(content, fileHits, replacement, snapshot.query);
                if (!applied.ok) {
                    outcome.ok = false;
                    outcome.error = applied.error;
                    break;
                }
                if (applied.replaced == 0) {
                    continue;
                }
                const ReplaceWriteResult written = rewriter(file, applied.text.toUtf8());
                if (!written.ok) {
                    outcome.ok = false;
                    outcome.error = QStringLiteral("cannot write %1: %2").arg(file, written.error);
                    break;
                }
                ++outcome.filesChanged;
                outcome.replacements += applied.replaced;
                outcome.writtenFiles.append(file);
            }
        }
    }

    QMetaObject::invokeMethod(
        this,
        [this, generation, outcome]() {
            if (generation != generation_) {
                return;
            }
            running_.store(false);
            emit finished(outcome);
        },
        Qt::QueuedConnection);
}

} // namespace vedit::workspace::search
