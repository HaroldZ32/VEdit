// T61 probe: what syntax definitions can a KSyntaxHighlighting repository see,
// and WHERE do they come from? Used to decide F1 (NO_STANDARD_PATHS=ON) vs
// F2 (NO_STANDARD_PATHS=OFF + XDG_DATA_DIRS) for the whitelist injection form.
//
// It prints machine-readable KSH-ENV lines:
//   * the generic data locations Qt resolves (QStandardPaths),
//   * every *.xml found under <location>/org.kde.syntax-highlighting/syntax*,
//     with the <language name="..."> parsed out of plain XML files (source
//     attribution),
//   * every VALID definition the library actually loaded (sorted names + count +
//     sha256 over the sorted names, so runs are comparable),
//   * (with --editor-repo) the same for KTextEditor's own repository, and
//     optionally a const_cast injection into it (--inject-editor).
//
// Assertions: --expect-names a,b / --forbid-names x,y (exit 1 on violation).
//
// This is a test/diagnostic binary, not product code.

#include <QApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QtGlobal>

#include <KTextEditor/Editor>

#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Repository>

namespace {

QStringList sortedValidNames(const KSyntaxHighlighting::Repository &repo)
{
    QStringList names;
    const auto definitions = repo.definitions();
    for (const auto &definition : definitions) {
        if (definition.isValid()) {
            names.append(definition.name());
        }
    }
    names.sort();
    return names;
}

QString hashNames(const QStringList &names)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(names.join(QLatin1Char('\n')).toUtf8());
    return QString::fromLatin1(hash.result().toHex());
}

// [name, file] for every plain-XML syntax definition under a directory.
QList<QPair<QString, QString>> definitionsOnDisk(const QString &dir)
{
    QList<QPair<QString, QString>> found;
    const QDir syntaxDir(dir + QStringLiteral("/org.kde.syntax-highlighting/syntax"));
    if (!syntaxDir.exists()) {
        return found;
    }
    const QStringList files = syntaxDir.entryList({QStringLiteral("*.xml")}, QDir::Files, QDir::Name);
    static const QRegularExpression languageTag(QStringLiteral(R"re(<language\b[^>]*\bname="([^"]+))re"));
    for (const QString &file : files) {
        QFile handle(syntaxDir.filePath(file));
        if (!handle.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        const QString head = QString::fromUtf8(handle.read(8192));
        const auto match = languageTag.match(head);
        found.append({match.hasMatch() ? match.captured(1) : QStringLiteral("<unparsed>"),
                      syntaxDir.filePath(file)});
    }
    return found;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-ksh-env-probe"));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({QStringLiteral("list"), QStringLiteral("List visible definitions and sources (default).")});
    parser.addOption({QStringLiteral("show-sources"), QStringLiteral("Scan the standard locations for on-disk definitions.")});
    parser.addOption({QStringLiteral("editor-repo"), QStringLiteral("Also inspect KTextEditor's own repository.")});
    parser.addOption({QStringLiteral("inject-editor"), QStringLiteral("const_cast-inject this directory into KTextEditor's repository."),
                      QStringLiteral("dir")});
    parser.addOption({QStringLiteral("expect-names"), QStringLiteral("Comma-separated names that MUST be visible."), QStringLiteral("names")});
    parser.addOption({QStringLiteral("forbid-names"), QStringLiteral("Comma-separated names that must NOT be visible."), QStringLiteral("names")});
    parser.addOption({QStringLiteral("expect-editor-names"),
                      QStringLiteral("KTextEditor repo: comma-separated names that MUST be visible (defaults to --expect-names)."),
                      QStringLiteral("names")});
    parser.addOption({QStringLiteral("forbid-editor-names"),
                      QStringLiteral("KTextEditor repo: names that must NOT be visible (defaults to --forbid-names)."),
                      QStringLiteral("names")});
    parser.process(app);

    QStringList failures;
    QTextStream out(stdout);

    KSyntaxHighlighting::Repository repository;
    const QStringList names = sortedValidNames(repository);
    const QStringList customPaths = repository.customSearchPaths();

    out << QStringLiteral("KSH-ENV defs=%1 hash=%2").arg(names.size()).arg(hashNames(names)) << Qt::endl;
    for (const QString &name : names) {
        out << QStringLiteral("KSH-ENV def name=\"%1\"").arg(name) << Qt::endl;
    }
    out << QStringLiteral("KSH-ENV custom_paths=%1").arg(customPaths.join(QLatin1Char(':'))) << Qt::endl;

    const QStringList standardPaths = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    out << QStringLiteral("KSH-ENV std_paths=%1").arg(standardPaths.join(QLatin1Char(':'))) << Qt::endl;

    if (parser.isSet(QStringLiteral("show-sources"))) {
        for (const QString &dir : standardPaths) {
            const auto found = definitionsOnDisk(dir);
            const QDir syntaxDir(dir + QStringLiteral("/org.kde.syntax-highlighting/syntax"));
            const QDir bundledDir(dir + QStringLiteral("/org.kde.syntax-highlighting/syntax-bundled"));
            out << QStringLiteral("KSH-ENV loc dir=\"%1\" syntax_dir=%2 xml_files=%3 bundled_dir=%4 bundled_files=%5")
                       .arg(dir)
                       .arg(syntaxDir.exists() ? 1 : 0)
                       .arg(found.size())
                       .arg(bundledDir.exists() ? 1 : 0)
                       .arg(bundledDir.exists() ? bundledDir.entryList({QStringLiteral("*.xml")}, QDir::Files).size() : 0)
                << Qt::endl;
            for (const auto &entry : found) {
                out << QStringLiteral("KSH-ENV source name=\"%1\" path=\"%2\"").arg(entry.first, entry.second) << Qt::endl;
            }
        }
    }

    // Optional: KTextEditor's own repository (the F1/F2 question is specifically
    // about the repository KTextEditor uses for Document::setHighlightingMode).
    KTextEditor::Editor *editor = nullptr;
    if (parser.isSet(QStringLiteral("editor-repo")) || parser.isSet(QStringLiteral("inject-editor"))) {
        editor = KTextEditor::Editor::instance();
        if (editor == nullptr) {
            out << QStringLiteral("KSH-ENV editor_repo=null") << Qt::endl;
            failures.append(QStringLiteral("KTextEditor::Editor::instance() is null"));
        } else {
            const QString injectDir = parser.value(QStringLiteral("inject-editor"));
            if (!injectDir.isEmpty()) {
                auto &editable = const_cast<KSyntaxHighlighting::Repository &>(editor->repository());
                editable.addCustomSearchPath(injectDir);
                out << QStringLiteral("KSH-ENV editor_repo_injected=\"%1\"").arg(injectDir) << Qt::endl;
            }
            const QStringList editorNames = sortedValidNames(editor->repository());
            out << QStringLiteral("KSH-ENV editor_repo defs=%1 hash=%2").arg(editorNames.size()).arg(hashNames(editorNames))
                << Qt::endl;
            for (const QString &name : editorNames) {
                out << QStringLiteral("KSH-ENV editor_def name=\"%1\"").arg(name) << Qt::endl;
            }
        }
    }

    const auto report = [&](const QStringList &visible, const QString &scope,
                            const QStringList &expected, const QStringList &forbidden) {
        for (const QString &name : expected) {
            const bool present = visible.contains(name);
            out << QStringLiteral("KSH-ENV %1 expect name=\"%2\" present=%3").arg(scope, name).arg(present ? 1 : 0) << Qt::endl;
            if (!present) {
                failures.append(QStringLiteral("%1: expected definition %2 is absent").arg(scope, name));
            }
        }
        for (const QString &name : forbidden) {
            const bool present = visible.contains(name);
            out << QStringLiteral("KSH-ENV %1 forbid name=\"%2\" present=%3").arg(scope, name).arg(present ? 1 : 0) << Qt::endl;
            if (present) {
                failures.append(QStringLiteral("%1: forbidden definition %2 is visible").arg(scope, name));
            }
        }
    };

    const QStringList repoExpected = parser.value(QStringLiteral("expect-names")).split(QLatin1Char(','), Qt::SkipEmptyParts);
    const QStringList repoForbidden = parser.value(QStringLiteral("forbid-names")).split(QLatin1Char(','), Qt::SkipEmptyParts);
    report(names, QStringLiteral("repo"), repoExpected, repoForbidden);
    if (editor != nullptr) {
        // The editor's repository is a DIFFERENT instance: expectations for it are
        // separate, so a scenario can show "repo is empty but editor is injected".
        const QStringList editorExpected = parser.isSet(QStringLiteral("expect-editor-names"))
            ? parser.value(QStringLiteral("expect-editor-names")).split(QLatin1Char(','), Qt::SkipEmptyParts)
            : repoExpected;
        const QStringList editorForbidden = parser.isSet(QStringLiteral("forbid-editor-names"))
            ? parser.value(QStringLiteral("forbid-editor-names")).split(QLatin1Char(','), Qt::SkipEmptyParts)
            : repoForbidden;
        report(sortedValidNames(editor->repository()), QStringLiteral("editor"), editorExpected, editorForbidden);
    }

    for (const QString &failure : failures) {
        out << QStringLiteral("KSH-ENV FAIL: %1").arg(failure) << Qt::endl;
    }
    out << QStringLiteral("KSH-ENV result=%1").arg(failures.isEmpty() ? QStringLiteral("pass") : QStringLiteral("fail"))
        << Qt::endl;
    return failures.isEmpty() ? 0 : 1;
}
