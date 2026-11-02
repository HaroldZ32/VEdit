// Folding-rule verification probe (task T45).
//
// What it measures, and why it is trustworthy:
//   * the definition is loaded through a Repository whose ONLY custom search path
//     is `resources/`, so the file that is measured is the one in this repository;
//   * the resolved definition's `author` / `section` / `name` are printed and
//     asserted — if a KDE built-in definition were picked instead, the author
//     would not be "VEdit" and the probe FAILS instead of measuring the wrong file
//     (the mistake the old evidence/syn-kde-data/03-folding.log made);
//   * folding is counted through the public AbstractHighlighter callbacks
//     (applyFolding Begin/End), the same observable the KDE folding code uses;
//   * `--library` is printed by the runner: the F2 self-built library (QRC_SYNTAX=OFF,
//     zero embedded definitions) is the measurement of record; the distro library
//     is only a fallback and is labelled as such.
#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <KSyntaxHighlighting/AbstractHighlighter>
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/FoldingRegion>
#include <KSyntaxHighlighting/Format>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/State>

#include <cstdio>

namespace {

struct Counts {
    int formats = 0;
    int begins = 0;
    int ends = 0;
};

class Probe final : public KSyntaxHighlighting::AbstractHighlighter
{
public:
    void applyFormat(int, int, const KSyntaxHighlighting::Format &) override
    {
        ++counts_.formats;
    }
    void applyFolding(int, int, KSyntaxHighlighting::FoldingRegion region) override
    {
        if (region.type() == KSyntaxHighlighting::FoldingRegion::Begin) {
            ++counts_.begins;
        } else if (region.type() == KSyntaxHighlighting::FoldingRegion::End) {
            ++counts_.ends;
        }
    }
    Counts run(const KSyntaxHighlighting::Definition &definition, const QString &text)
    {
        counts_ = {};
        setDefinition(definition);
        KSyntaxHighlighting::State state;
        const QStringList lines = text.split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            state = highlightLine(QStringView(line), state);
        }
        return counts_;
    }

private:
    Counts counts_;
};

QString verilogSample()
{
    return QStringLiteral(
        "module top;\n"
        "  generate\n"
        "    if (1) begin : g\n"
        "      always @(*) begin\n"
        "        case (sel)\n"
        "          1'b0: y = a;\n"
        "          default: y = b;\n"
        "        endcase\n"
        "      end\n"
        "    end\n"
        "  endgenerate\n"
        "  function automatic integer f;\n"
        "    f = 0;\n"
        "  endfunction\n"
        "  task t;\n"
        "  endtask\n"
        "endmodule\n");
}

QString systemVerilogSample()
{
    return QStringLiteral(
        "package pkg;\n"
        "endpackage\n"
        "interface ifc;\n"
        "endinterface\n"
        "program prog;\n"
        "endprogram\n"
        "class c;\n"
        "endclass\n"
        "module top;\n"
        "  generate\n"
        "    if (1) begin : g\n"
        "      always_comb begin\n"
        "        case (sel)\n"
        "          1'b0: y = a;\n"
        "          default: y = b;\n"
        "        endcase\n"
        "      end\n"
        "    end\n"
        "  endgenerate\n"
        "  function automatic integer f;\n"
        "    f = 0;\n"
        "  endfunction\n"
        "  task t;\n"
        "  endtask\n"
        "endmodule\n");
}

} // namespace

// The sample language is an explicit argument (T66): the old probe selected it
// with `definitionName == "SystemVerilog"`, but the runner passes the unique
// probe name `VEditProbeSystemVerilog`, so the SV branch never ran and both runs
// measured the 18-line Verilog sample. Keep the selector typed.
enum class SampleLanguage {
    Verilog,
    SystemVerilog,
};

bool parseSampleLanguage(const QString &text, SampleLanguage *language)
{
    if (text == QStringLiteral("verilog")) {
        *language = SampleLanguage::Verilog;
        return true;
    }
    if (text == QStringLiteral("systemverilog")) {
        *language = SampleLanguage::SystemVerilog;
        return true;
    }
    return false;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 4) {
        std::printf("usage: %s <resources-dir> <definition-name> <verilog|systemverilog>\n",
                    argv[0]);
        return 2;
    }
    const QString resourcesDir = QString::fromLocal8Bit(argv[1]);
    const QString definitionName = QString::fromLocal8Bit(argv[2]);
    SampleLanguage language = SampleLanguage::Verilog;
    if (!parseSampleLanguage(QString::fromLocal8Bit(argv[3]), &language)) {
        std::printf("FATAL: unknown sample language %s (expected verilog|systemverilog)\n",
                    argv[3]);
        return 2;
    }
    const QString library = qEnvironmentVariable("VEDIT_FOLD_PROBE_LIBRARY", "unknown");

    KSyntaxHighlighting::Repository repository;
    // The repository API expects the PARENT of the "syntax/" directory.
    repository.addCustomSearchPath(resourcesDir);

    const KSyntaxHighlighting::Definition definition = repository.definitionForName(definitionName);
    if (!definition.isValid()) {
        std::printf("RESULT library=%s definition=%s valid=0\n", qPrintable(library),
                    qPrintable(definitionName));
        return 1;
    }
    std::printf("DEFINITION library=%s name=%s author=%s section=%s license=%s version=%d\n",
                qPrintable(library), qPrintable(definition.name()),
                qPrintable(definition.author()), qPrintable(definition.section()),
                qPrintable(definition.license()), definition.version());

    // Proof that the measured definition is OURS (not a KDE built-in): the file in
    // resources/syntax declares author="VEdit" section="Hardware".
    const bool ours = definition.author() == QStringLiteral("VEdit")
        && definition.section() == QStringLiteral("Hardware");

    const QString sample = language == SampleLanguage::SystemVerilog
        ? systemVerilogSample()
        : verilogSample();
    Probe probe;
    const Counts counts = probe.run(definition, sample);

    std::printf("SAMPLE language=%s text-lines=%lld formats=%d begins=%d ends=%d\n",
                language == SampleLanguage::SystemVerilog ? "systemverilog" : "verilog",
                static_cast<long long>(sample.count(QLatin1Char('\n')) + 1), counts.formats,
                counts.begins, counts.ends);
    std::printf("RESULT library=%s definition=%s ours=%d valid=1 formats=%d begins=%d ends=%d\n",
                qPrintable(library), qPrintable(definitionName), ours ? 1 : 0, counts.formats,
                counts.begins, counts.ends);
    return (ours && counts.begins > 0 && counts.ends > 0) ? 0 : 1;
}
