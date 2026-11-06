// A3 ctest: the extension -> syntax-definition mapping of the delivered product.
//
// What this asserts (frozen contract .mpd/plans/ux-batch-2/00-plan.md §3.A3):
//   * the explicit table wins over the editor repository: `.v` stays "Verilog",
//     `.sdc` (claimed by a decoy definition in the probe share) still becomes
//     "Tcl", so the table is consulted BEFORE the repository;
//   * `.v / .sv / .vhd / .vhdl / .md / .markdown / .tcl / .sdc / .xdc` resolve to
//     the expected definition names;
//   * an extension the table does not know falls back to ANY valid repository
//     definition (A3 dropped the `author == "VEdit"` requirement), and an
//     unknown one resolves to nothing;
//   * the resolved definition really is the shipped one (its file path is inside
//     the share under test) and it produces format ranges for a real sample.
//
// Two ways to run it:
//   highlight-mode-selftest                 builds a private share from
//                                           resources/syntax + writes a decoy
//                                           definition into it (offscreen ctest)
//   highlight-mode-selftest --appdir <dir>  uses <dir>/usr/share and exports the
//   highlight-mode-selftest --share <dir>   XDG variables EXACTLY as AppRun does
//                                           for the assembled AppDir, so the
//                                           packaged layout is what gets tested.
#include "highlight_mode.h"

#ifdef VEDIT_HAVE_SYNTAX_KDE
#include "ksyntax_data.h"
#endif

#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KSyntaxHighlighting/AbstractHighlighter>
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Format>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/State>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <cstdio>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const char *name, bool ok, const QString &detail = QString())
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s%s%s\n", name, ok ? "PASS" : "FAIL",
                detail.isEmpty() ? "" : "  ", detail.toUtf8().constData());
    std::fflush(stdout);
}

void fact(const QString &line)
{
    std::printf("MODE-FACT %s\n", line.toUtf8().constData());
    std::fflush(stdout);
}

struct Expectation {
    const char *fileName;
    const char *definition;
};

// The contract's six extensions, plus the aliases each shipped definition claims.
const Expectation kExpected[] = {
    {"top.v", "Verilog"},         {"top.vh", "Verilog"},      {"top.vlg", "Verilog"},
    {"top.sv", "SystemVerilog"},  {"top.svh", "SystemVerilog"},
    {"mod.vhd", "VHDL"},          {"mod.vhdl", "VHDL"},
    {"notes.md", "Markdown"},     {"doc.markdown", "Markdown"},
    {"run.tcl", "Tcl"},           {"timing.sdc", "Tcl"},      {"pins.xdc", "Tcl"},
};

// A definition that is NOT authored by VEdit and claims an extension the product
// table maps (*.sdc) plus one it does not (*.probe). Written at run time: it must
// never end up in resources/syntax (packaging would ship it).
constexpr auto kDecoyDefinition = R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE language>
<language name="ProbeLanguage" version="1" kateversion="5.0" section="Other"
          extensions="*.probe;*.sdc" mimetype="text/x-probe" author="Probe"
          license="MIT">
  <highlighting>
    <contexts>
      <context name="Normal" attribute="Normal Text" lineEndContext="#stay">
        <RegExpr attribute="Keyword" context="#stay" String="probe"/>
      </context>
    </contexts>
    <itemDatas>
      <itemData name="Normal Text" defStyleNum="dsNormal"/>
      <itemData name="Keyword" defStyleNum="dsKeyword"/>
    </itemDatas>
  </highlighting>
</language>
)";

// Writes `sourcePath` to `destinationPath` with the `<language name="...">` value
// replaced. Used to give a shipped definition a name no embedded KDE definition
// owns, without touching a single rule of it.
bool writeRenamedLanguage(const QString &sourcePath, const QString &destinationPath,
                          const QString &from, const QString &to)
{
    QFile input(sourcePath);
    if (!input.open(QIODevice::ReadOnly)) {
        return false;
    }
    QByteArray content = input.readAll();
    input.close();
    const QByteArray needle = QByteArrayLiteral("<language name=\"") + from.toUtf8() + '"';
    const int index = content.indexOf(needle);
    if (index < 0) {
        return false;
    }
    content.replace(index, needle.size(),
                    QByteArrayLiteral("<language name=\"") + to.toUtf8() + '"');
    QFile output(destinationPath);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return output.write(content) == content.size();
}

bool copyShippedDefinitions(const QString &syntaxDirectory)
{
    if (!QDir().mkpath(syntaxDirectory)) {
        return false;
    }
    const QDir source(QStringLiteral(VEDIT_SYNTAX_RESOURCES_DIR "/syntax"));
    const QStringList files = source.entryList(QStringList{QStringLiteral("vedit-*.xml")}, QDir::Files);
    if (files.isEmpty()) {
        return false;
    }
    for (const QString &name : files) {
        const QString destination = syntaxDirectory + QLatin1Char('/') + name;
        QFile::remove(destination);
        if (!QFile::copy(source.filePath(name), destination)) {
            return false;
        }
    }
    // Probe copies with a unique language name: the distro KSyntaxHighlighting
    // embeds KDE's definitions and a same-named embedded one wins over ours
    // (AGENT.md §9), so the CONSTRUCTION checks in this developer-host run need
    // a name no embedded definition owns. Only the language name changes; the
    // rules are byte-identical to the shipped file. The AppDir run uses the real
    // names instead.
    struct ProbeCopy {
        const char *file;
        const char *language;
    };
    for (const ProbeCopy &probe : {ProbeCopy{"vedit-vhdl.xml", "VHDL"},
                                   ProbeCopy{"vedit-markdown.xml", "Markdown"},
                                   ProbeCopy{"vedit-tcl.xml", "Tcl"}}) {
        if (!writeRenamedLanguage(source.filePath(QString::fromLatin1(probe.file)),
                                  syntaxDirectory + QStringLiteral("/probe-")
                                      + QString::fromLatin1(probe.language) + QStringLiteral(".xml"),
                                  QString::fromLatin1(probe.language),
                                  QString::fromLatin1(probe.language) + QStringLiteral("Probe"))) {
            return false;
        }
    }
    QFile decoy(syntaxDirectory + QStringLiteral("/probe-decoy.xml"));
    if (!decoy.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const QByteArray decoyXml(kDecoyDefinition);
    return decoy.write(decoyXml) == decoyXml.size();
}

// Mirrors packaging/runtime/AppRun: XDG_DATA_DIRS *and* XDG_DATA_HOME point at
// the AppDir's share, VEDIT_SYNTAX_DIR at the KDE data root inside it. Both must
// be set before QApplication exists — KTextEditor's repository is built once.
void narrowXdgDataPaths(const QString &shareDirectory)
{
    qputenv("XDG_DATA_DIRS", shareDirectory.toLocal8Bit());
    qputenv("XDG_DATA_HOME", shareDirectory.toLocal8Bit());
    qputenv("XDG_CONFIG_HOME", (shareDirectory + QStringLiteral("/config")).toLocal8Bit());
    qputenv("VEDIT_SYNTAX_DIR",
            (shareDirectory + QStringLiteral("/org.kde.syntax-highlighting")).toLocal8Bit());
}

// The first definition parsed from `shareRoot` whose language name matches. This
// ignores the name-lookup order on purpose: with the distro library an embedded
// KDE definition of the same name wins the lookup, but our file must still have
// parsed.
QString shippedDefinitionFile(const KSyntaxHighlighting::Repository &repository,
                              const QString &shareRoot, const QString &name)
{
    const auto definitions = repository.definitions();
    for (const KSyntaxHighlighting::Definition &definition : definitions) {
        if (definition.name() == name && definition.filePath().startsWith(shareRoot)) {
            return definition.filePath();
        }
    }
    return {};
}

// --- construction-level checks (A3 hard condition) --------------------------
//
// "definitionForName() resolves" says nothing about whether the RULES work.
// These checks highlight a representative document and assert the FORMAT NAME
// applied to one construct, so a definition whose rules were dropped (unknown
// keyword list, bad regex, missing itemData) fails instead of merely resolving.
struct FormatSpan {
    int offset = 0;
    int length = 0;
    QString name;
};

class SpanCapture final : public KSyntaxHighlighting::AbstractHighlighter {
public:
    void applyFormat(int offset, int length, const KSyntaxHighlighting::Format &format) override
    {
        spans_.append(FormatSpan{base_ + offset, length, format.name()});
    }
    void run(const KSyntaxHighlighting::Definition &definition, const QString &text)
    {
        spans_.clear();
        base_ = 0;
        setDefinition(definition);
        KSyntaxHighlighting::State state;
        const QStringList lines = text.split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            state = highlightLine(QStringView(line), state);
            base_ += line.size() + 1;
        }
    }
    QString formatAt(int offset) const
    {
        for (const FormatSpan &span : spans_) {
            if (offset >= span.offset && offset < span.offset + span.length) {
                return span.name;
            }
        }
        return QStringLiteral("<unformatted>");
    }

private:
    QList<FormatSpan> spans_;
    int base_ = 0;
};

void expectFormatIn(const char *checkName, const SpanCapture &capture, const QString &text,
                    const QString &anchor, const QString &needle, const QString &expected)
{
    const int anchorIndex = text.indexOf(anchor);
    const int index = anchorIndex < 0 ? -1 : text.indexOf(needle, anchorIndex);
    const QString actual = index < 0 ? QStringLiteral("<not found>") : capture.formatAt(index);
    check(checkName, index >= 0 && actual == expected,
          QStringLiteral("'%1' -> '%2' (expected '%3')").arg(needle, actual, expected));
}

void expectFormat(const char *checkName, const SpanCapture &capture, const QString &text,
                  const QString &needle, const QString &expected)
{
    expectFormatIn(checkName, capture, text, QString(), needle, expected);
}

void runConstructionChecks(const KSyntaxHighlighting::Repository &repository, bool appdir)
{
    // Same naming rule as everywhere else: the AppDir run resolves the shipped
    // definition by its real name; the developer-host run uses the renamed probe
    // copy written by copyShippedDefinitions().
    const auto definitionFor = [&repository, appdir](const QString &language) {
        return repository.definitionForName(appdir ? language : language + QStringLiteral("Probe"));
    };

    // --- VHDL ---------------------------------------------------------------
    const KSyntaxHighlighting::Definition vhdl = definitionFor(QStringLiteral("VHDL"));
    check("construct.vhdl-definition", vhdl.isValid(), vhdl.filePath());
    if (vhdl.isValid()) {
        const QString sample = QStringLiteral(
            "library ieee;\n"
            "use ieee.std_logic_1164.all;\n"
            "\n"
            "-- counter with an async reset\n"
            "entity counter is\n"
            "  port (\n"
            "    clk : in  std_logic;\n"
            "    q   : out std_logic_vector(3 downto 0)\n"
            "  );\n"
            "end entity counter;\n"
            "\n"
            "architecture rtl of counter is\n"
            "begin\n"
            "  process (clk) is\n"
            "  begin\n"
            "    if reset = '1' then\n"
            "      q <= (others => '0');\n"
            "      report \"reset asserted\";\n"
            "    end if;\n"
            "  end process;\n"
            "end architecture rtl;\n");
        SpanCapture capture;
        capture.run(vhdl, sample);
        expectFormatIn("construct.vhdl-keyword-entity", capture, sample, "entity counter is", "entity", "Keyword");
        expectFormatIn("construct.vhdl-keyword-is", capture, sample, "entity counter is", "is", "Keyword");
        expectFormat("construct.vhdl-keyword-architecture", capture, sample, "architecture", "Keyword");
        expectFormatIn("construct.vhdl-keyword-end", capture, sample, "end entity counter", "end", "Keyword");
        expectFormat("construct.vhdl-comment", capture, sample, "-- counter", "Comment");
        expectFormatIn("construct.vhdl-bit-literal", capture, sample, "reset =", "'1'", "String");
        expectFormat("construct.vhdl-string", capture, sample, "\"reset asserted\"", "String");
        expectFormat("construct.vhdl-type-name", capture, sample, "std_logic_vector", "Data Type");
        expectFormatIn("construct.vhdl-number", capture, sample, "std_logic_vector(3", "3", "Number");

        // R4(e): folding must be balanced for a LABELLED process. The opener is
        // anchored at line start; before the fix `p1: process (clk) is` opened
        // nothing while `end process;` still closed a region (begins=0/ends=1).
        // Both the plain and the labelled form must open exactly one region.
        const QString foldSample = QStringLiteral("p1: process (clk) is\n"
                                                  "begin\n"
                                                  "  q <= d;\n"
                                                  "end process;\n");
        const vedit::editor::HighlightStats labelledFold =
            vedit::editor::measureHighlighting(vhdl, foldSample);
        check("construct.vhdl-fold-labelled-process",
              labelledFold.foldingBegins == 1 && labelledFold.foldingEnds == 1,
              QStringLiteral("begins=%1 ends=%2").arg(labelledFold.foldingBegins).arg(labelledFold.foldingEnds));
        const vedit::editor::HighlightStats plainFold = vedit::editor::measureHighlighting(
            vhdl, QStringLiteral("process (clk) is\nbegin\n  q <= d;\nend process;\n"));
        check("construct.vhdl-fold-unlabelled-process",
              plainFold.foldingBegins == 1 && plainFold.foldingEnds == 1,
              QStringLiteral("begins=%1 ends=%2").arg(plainFold.foldingBegins).arg(plainFold.foldingEnds));
    }

    // --- Markdown -----------------------------------------------------------
    const KSyntaxHighlighting::Definition markdown = definitionFor(QStringLiteral("Markdown"));
    check("construct.markdown-definition", markdown.isValid(), markdown.filePath());
    if (markdown.isValid()) {
        const QString sample = QStringLiteral(
            "# Timing constraints\n"
            "\n"
            "- first item\n"
            "- second item\n"
            "\n"
            "Some **bold** text, `inline code`, and [a link](https://example.com/doc).\n"
            "\n"
            "```tcl\n"
            "set period 10\n"
            "```\n");
        SpanCapture capture;
        capture.run(markdown, sample);
        expectFormat("construct.markdown-heading", capture, sample, "# Timing constraints", "Heading");
        expectFormatIn("construct.markdown-list-marker", capture, sample, "- first item", "-", "List Marker");
        expectFormat("construct.markdown-inline-code", capture, sample, "`inline code`", "Code");
        expectFormat("construct.markdown-link", capture, sample, "[a link](https://example.com/doc)", "Link");
        // The captain's hard condition: a line INSIDE a fenced block must be code,
        // not plain text — this fails if the FencedCode context is not entered.
        expectFormat("construct.markdown-fenced-content-is-code", capture, sample,
                     "set period 10", "Code Block");
    }

    // --- Tcl (also the definition SDC/XDC resolve to) -----------------------
    const KSyntaxHighlighting::Definition tcl = definitionFor(QStringLiteral("Tcl"));
    check("construct.tcl-definition", tcl.isValid(), tcl.filePath());
    if (tcl.isValid()) {
        const QString sample = QStringLiteral(
            "# build the clock constraint\n"
            "proc make_clock { name period } {\n"
            "    set ns $period\n"
            "    if { $ns > 10 } {\n"
            "        foreach arg $extra {\n"
            "            puts \"period is $ns\"\n"
            "        }\n"
            "    }\n"
            "}\n"
            "set half [expr {$ns / 2}]\n");
        SpanCapture capture;
        capture.run(tcl, sample);
        expectFormat("construct.tcl-proc", capture, sample, "proc", "Keyword");
        expectFormatIn("construct.tcl-set", capture, sample, "set ns", "set", "Keyword");
        expectFormatIn("construct.tcl-if", capture, sample, "if {", "if", "Keyword");
        expectFormat("construct.tcl-foreach", capture, sample, "foreach", "Keyword");
        expectFormat("construct.tcl-comment", capture, sample, "# build", "Comment");
        expectFormatIn("construct.tcl-variable", capture, sample, "set ns", "$period", "Variable");
        expectFormatIn("construct.tcl-double-quoted-string", capture, sample, "puts", "\"period is", "String");
        expectFormatIn("construct.tcl-command-substitution", capture, sample, "set half", "[", "Operator");
        expectFormatIn("construct.tcl-substitution-runs-a-command", capture, sample, "set half", "expr", "Keyword");

        // `.sdc` / `.xdc` are Tcl: the SAME definition must colour an SDC command
        // (that is the whole point of routing those suffixes to "Tcl").
        const QString sdc = QStringLiteral(
            "create_clock -name clk -period 10 [get_ports clk]\n"
            "set_input_delay -clock clk 2.0 [get_ports {din}]\n");
        capture.run(tcl, sdc);
        expectFormat("construct.sdc-create-clock", capture, sdc, "create_clock", "SDC Command");
        expectFormat("construct.sdc-input-delay", capture, sdc, "set_input_delay", "SDC Command");
        expectFormatIn("construct.sdc-command-substitution", capture, sdc, "create_clock", "[", "Operator");
        check("construct.sdc-suffix-maps-to-tcl",
              vedit::editor::highlightModeForFile(QStringLiteral("timing.sdc")) == QLatin1String("Tcl") &&
                  vedit::editor::highlightModeForFile(QStringLiteral("pins.xdc")) == QLatin1String("Tcl"));
    }
}

// R4(d): the editor kernel (kExtensionTable in highlight_mode.cpp) and the
// syntax-data loader (KsyntaxData::languageNameForExtension) encode the same
// suffix -> language table. Nothing compared them across all suffixes, so an
// alias added to one path would silently diverge between the editor and the
// breadcrumb/syntax-data path. This iterates the contract's suffixes and
// requires identical answers from both.
void runExtensionDriftChecks()
{
#ifdef VEDIT_HAVE_SYNTAX_KDE
    static const char *const kContractSuffixes[] = {
        "v",   "vh",       "vlg",  "sv",   "svh",  "vhd", "vhdl", "md",
        "markdown", "mdown", "mkd", "tcl",  "tk",   "sdc", "xdc",
    };
    for (const char *suffix : kContractSuffixes) {
        const QString fileName = QStringLiteral("drift.") + QLatin1String(suffix);
        const QString editorMode = vedit::editor::highlightModeForFile(fileName);
        const QString dataMode = vedit::kde::KsyntaxData::languageNameForExtension(fileName);
        check((QStringLiteral("drift.extension.") + QLatin1String(suffix)).toUtf8().constData(),
              editorMode == dataMode,
              QStringLiteral("highlight_mode='%1' ksyntax_data='%2'").arg(editorMode, dataMode));
    }
    // The data path must not invent an answer for a suffix neither table owns
    // (the editor path may still fall back to a repository definition by design).
    check("drift.unknown-extension",
          vedit::kde::KsyntaxData::languageNameForExtension(QStringLiteral("drift.zzz")).isEmpty());
#else
    fact(QStringLiteral("extension drift guard skipped: vedit_syntax_kde not linked"));
#endif
}

void runChecks(KTextEditor::Editor *editor, const QString &shareRoot, const QString &modeName)
{
    const KSyntaxHighlighting::Repository &repository = editor->repository();    fact(QStringLiteral("mode=%1 share=%2 repository_definitions=%3")
             .arg(modeName, shareRoot)
             .arg(repository.definitions().size()));

    // The decoy proves the table is consulted first: the repository maps `.sdc`
    // to "ProbeLanguage", while the product must still answer "Tcl".
    const QString repositorySaysSdc =
        repository.definitionForFileName(QStringLiteral("timing.sdc")).name();
    fact(QStringLiteral("repository.definitionForFileName(timing.sdc)='%1'").arg(repositorySaysSdc));

    for (const Expectation &expectation : kExpected) {
        const QString fileName = QString::fromLatin1(expectation.fileName);
        const QString expected = QString::fromLatin1(expectation.definition);
        const QString mode = vedit::editor::highlightModeForFile(QStringLiteral("/tmp/a3/") + fileName);
        check(expectation.fileName, mode == expected,
              QStringLiteral("got '%1', expected '%2'").arg(mode, expected));

        const KSyntaxHighlighting::Definition definition =
            vedit::editor::editorDefinitionForMode(mode);
        check((QStringLiteral("definition-valid.") + fileName).toUtf8().constData(),
              definition.isValid(), QStringLiteral("mode '%1'").arg(mode));

        // The shipped file must really have parsed and registered. This can only
        // be asserted where the library embeds no definitions of its own: the
        // distro KSyntaxHighlighting carries 450+ KDE definitions compiled in,
        // and a same-named embedded one wins over our custom-search-path file
        // (AGENT.md §9). The delivered AppDir ships the self-built F2 library,
        // which embeds none — that run (`--appdir` + the AppDir's own
        // libKF6SyntaxHighlighting) is where this becomes a hard assertion; the
        // developer-host run records the shadowing as a fact instead of a fail.
        const QString shipped = shippedDefinitionFile(repository, shareRoot, expected);
        if (modeName == QLatin1String("appdir")) {
            check((QStringLiteral("definition-parsed.") + fileName).toUtf8().constData(),
                  !shipped.isEmpty(),
                  shipped.isEmpty()
                      ? QStringLiteral("no definition named '%1' parsed from %2").arg(expected, shareRoot)
                      : shipped);
            if (definition.isValid()) {
                check((QStringLiteral("definition-shipped.") + fileName).toUtf8().constData(),
                      definition.filePath().startsWith(shareRoot),
                      QStringLiteral("file='%1' author='%2'").arg(definition.filePath(),
                                                                  definition.author()));
            }
        } else if (shipped.isEmpty()) {
            fact(QStringLiteral("%1: shipped definition not registered — shadowed by the "
                                "library's embedded same-named definition (distro build; "
                                "AGENT.md §9). Names still resolve correctly.")
                     .arg(fileName));
        } else {
            fact(QStringLiteral("%1 -> parsed='%2' chosen='%3'")
                     .arg(fileName, shipped, definition.filePath()));
        }
    }

    // Table first, proven by a case where the two answers DIFFER.
    const KSyntaxHighlighting::Definition decoy =
        repository.definitionForName(QStringLiteral("ProbeLanguage"));    if (decoy.isValid()) {
        check("table-first.sdc-beats-repository",
              vedit::editor::highlightModeForFile(QStringLiteral("t.sdc")).startsWith(QLatin1String("Tcl")) &&
                  repositorySaysSdc == QLatin1String("ProbeLanguage"),
              QStringLiteral("repository='%1' product='%2'")
                  .arg(repositorySaysSdc, vedit::editor::highlightModeForFile(QStringLiteral("t.sdc"))));
    } else {
        // The AppDir run has no decoy: the repository simply has no `.sdc`
        // definition, so the table is the only source. Record that instead.
        check("table-first.sdc-only-in-table", repository.definitionForFileName(QStringLiteral("t.sdc")).isValid() == false);
    }

    // Fallback: an extension the table does not know accepts ANY valid definition.
    const QString probeMode = vedit::editor::highlightModeForFile(QStringLiteral("device.probe"));
    if (decoy.isValid()) {
        check("fallback.accepts-non-vedit-definition", probeMode == QLatin1String("ProbeLanguage"),
              QStringLiteral("got '%1'").arg(probeMode));
        check("fallback.author-is-not-vedit", decoy.author() != QLatin1String("VEdit"), decoy.author());
    } else {
        fact(QStringLiteral("fallback decoy absent (AppDir share): probeMode='%1'").arg(probeMode));
        check("fallback.unknown-extension-stays-empty", probeMode.isEmpty(), probeMode);
    }
    check("unknown-extension-empty",
          vedit::editor::highlightModeForFile(QStringLiteral("thing.zzz")).isEmpty());

    // Construction-level checks: the shipped RULES actually colour constructs.
    runConstructionChecks(repository, modeName == QLatin1String("appdir"));

    // Drift guard: the editor kernel table and the syntax-data table must agree.
    runExtensionDriftChecks();

    // Real document path: the mode is set on a KTextEditor document and the
    // definition produces format ranges for a VHDL sample.
    KTextEditor::Document *document = editor->createDocument(nullptr);
    check("document.created", document != nullptr);
    if (document != nullptr) {
        QString diagnostic;
        const bool applied =
            vedit::editor::applyHighlightingMode(document, QStringLiteral("/tmp/a3/mod.vhd"), &diagnostic);
        check("document.applied", applied, diagnostic);
        check("document.highlighting-mode", document->highlightingMode() == QLatin1String("VHDL"),
              document->highlightingMode());
        const auto stats = vedit::editor::measureHighlighting(
            vedit::editor::editorDefinitionForMode(document->highlightingMode()),
            QStringLiteral("library ieee;\nuse ieee.std_logic_1164.all;\n-- comment\n"
                           "entity e is end entity;\n"));
        fact(QStringLiteral("vhdl formats=%1 folds=%2/%3")
                 .arg(stats.formats)
                 .arg(stats.foldingBegins)
                 .arg(stats.foldingEnds));
        check("document.formats", stats.formats > 0, QString::number(stats.formats));
        delete document;
    }
}

} // namespace

int main(int argc, char **argv)
{
    QString appDir;
    QString shareDirectory;
    for (int index = 1; index < argc; ++index) {
        const QString argument = QString::fromLocal8Bit(argv[index]);
        if (argument == QStringLiteral("--appdir") && index + 1 < argc) {
            appDir = QString::fromLocal8Bit(argv[++index]);
        } else if (argument == QStringLiteral("--share") && index + 1 < argc) {
            shareDirectory = QString::fromLocal8Bit(argv[++index]);
        }
    }

    QTemporaryDir temporaryShare;
    QString modeName = QStringLiteral("resources-temp");
    if (!shareDirectory.isEmpty()) {
        modeName = QStringLiteral("share");
    } else if (!appDir.isEmpty()) {
        shareDirectory = appDir + QStringLiteral("/usr/share");
        modeName = QStringLiteral("appdir");
    } else {
        if (!temporaryShare.isValid()) {
            std::printf("MODE-FACT no temporary directory\n");
            return 1;
        }
        shareDirectory = temporaryShare.path();
        if (!copyShippedDefinitions(shareDirectory
                                    + QStringLiteral("/org.kde.syntax-highlighting/syntax"))) {
            std::printf("MODE-FACT cannot build the probe share from %s\n",
                        VEDIT_SYNTAX_RESOURCES_DIR);
            return 1;
        }
    }
    narrowXdgDataPaths(shareDirectory);

    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-highlight-mode-selftest"));

    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    check("editor.instance", editor != nullptr);
    if (editor != nullptr) {
        runChecks(editor, shareDirectory, modeName);
    }

    std::printf("HIGHLIGHT-MODE-SELFTEST mode=%s checks=%d failures=%d\n",
                qPrintable(modeName), g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
