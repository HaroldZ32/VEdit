#include "search_types.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QStringLiteral>
#include <QVarLengthArray>

namespace vedit::workspace::search {

namespace {

constexpr int kSnippetChars = 400; // crates/vd-search::TREE_SNIPPET_CHARS

// The content normalisation shared by the fingerprint and the whole-line
// identity: drop the UTF-8 BOM (a decoding artefact, see the comment on
// matchedLineFingerprint) and trim, so decoding / line-ending / surrounding
// whitespace differences are not mistaken for content changes.
QString normalizedLineText(const QString &line)
{
    QString normalized = line;
    if (normalized.startsWith(QChar(0xFEFF))) {
        normalized.remove(0, 1);
    }
    return normalized.trimmed();
}

} // namespace

// Mirrors crates/vd-search/src/lib.rs::snippet_of + TREE_SNIPPET_CHARS: trim,
// then keep at most 400 Unicode scalar values and append U+2026 when truncated.
//
// A leading U+FEFF is the UTF-8 byte-order mark, not content: the scanner's
// preview reaches this lane through `QString::fromUtf8`, which drops a leading
// BOM, while the replace path decodes the file with `ConvertInitialBom` so the
// BOM stays in the text (line-1 columns count it). Dropping it on BOTH sides
// keeps the fingerprint about the line's content; a file whose BOM changed
// still differs in size and is refused by the per-file stamp.
QByteArray matchedLineFingerprint(const QString &line)
{
    QString normalized = normalizedLineText(line);
    const QList<uint> codePoints = normalized.toUcs4();
    if (codePoints.size() > kSnippetChars) {
        QVarLengthArray<char32_t, kSnippetChars + 1> truncated;
        truncated.reserve(kSnippetChars + 1);
        for (int index = 0; index < kSnippetChars; ++index) {
            truncated.append(static_cast<char32_t>(codePoints.at(index)));
        }
        truncated.append(static_cast<char32_t>(0x2026));
        normalized = QString::fromUcs4(truncated.constData(), truncated.size());
    }
    return QCryptographicHash::hash(normalized.toUtf8(), QCryptographicHash::Sha256).toHex();
}

bool matchedLinePreviewTruncated(const QString &preview)
{
    // `snippet_of` appends exactly one U+2026 after the 400 kept characters, so
    // a capped preview is 401 scalar values long and ends with the ellipsis. A
    // line that legitimately ends with '…' but is not capped is at most 400
    // values long, which keeps the two cases apart.
    const QList<uint> codePoints = preview.toUcs4();
    return codePoints.size() == kSnippetChars + 1 && codePoints.constLast() == 0x2026;
}

QString normalizedLineIdentity(const QString &line)
{
    return normalizedLineText(line);
}


QStringList defaultExcludedDirNames()
{
    static const QStringList names = {
        QStringLiteral(".git"),        QStringLiteral(".svn"),      QStringLiteral(".hg"),
        QStringLiteral("build"),       QStringLiteral("target"),    QStringLiteral("node_modules"),
        QStringLiteral(".cache"),      QStringLiteral(".idea"),     QStringLiteral(".vscode"),
        QStringLiteral("CMakeFiles"),  QStringLiteral(".ccache"),   QStringLiteral(".venv"),
    };
    return names;
}

bool isVerilogFileName(const QString &fileName)
{
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    return suffix == QStringLiteral("v") || suffix == QStringLiteral("vh") ||
           suffix == QStringLiteral("sv") || suffix == QStringLiteral("svh");
}

QString searchStateName(SearchState state)
{
    switch (state) {
    case SearchState::Idle:
        return QStringLiteral("idle");
    case SearchState::Running:
        return QStringLiteral("running");
    case SearchState::Done:
        return QStringLiteral("done");
    case SearchState::Cancelled:
        return QStringLiteral("cancelled");
    case SearchState::Failed:
        return QStringLiteral("failed");
    }
    return QStringLiteral("unknown");
}

} // namespace vedit::workspace::search
