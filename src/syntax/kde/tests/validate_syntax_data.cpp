// Load / degradation / folding validator for the private syntax data (T16).
//
// Proves: (a) definitions+themes load from the private path; (b) Verilog is
// available and resolves by explicit name (the .v -> "V" trap is detected);
// (c) a missing SystemVerilog definition degrades diagnosably to Verilog
// without crashing; (d) our SystemVerilog port carries folding regions for the
// A4 constructs (module/interface/program/class/function/task/generate/case/
// begin-end); (e) no forbidden KDE definition sits in the data directory.
#include "ksyntax_data.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <KSyntaxHighlighting/AbstractHighlighter>
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/FoldingRegion>
#include <KSyntaxHighlighting/Format>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/State>

#include <cstdio>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const std::string &name, bool ok, const QString &detail = {})
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s%s%s\n", name.c_str(), ok ? "PASS" : "FAIL",
                detail.isEmpty() ? "" : "  ", detail.toUtf8().constData());
    std::fflush(stdout);
}

bool hasDiagnostic(const vedit::kde::LoadReport &report, const QString &needle)
{
    for (const QString &diagnostic : report.diagnostics) {
        if (diagnostic.contains(needle, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

class FoldingProbe final : public KSyntaxHighlighting::AbstractHighlighter {
public:
    void applyFormat(int, int, const KSyntaxHighlighting::Format &) override { ++formats_; }
    void applyFolding(int, int, KSyntaxHighlighting::FoldingRegion region) override
    {
        if (region.type() == KSyntaxHighlighting::FoldingRegion::Begin) {
            ++begins_;
        } else if (region.type() == KSyntaxHighlighting::FoldingRegion::End) {
            ++ends_;
        }
    }
    void run(const KSyntaxHighlighting::Definition &definition, const QString &text)
    {
        formats_ = 0;
        begins_ = 0;
        ends_ = 0;
        setDefinition(definition);
        const QStringList lines = text.split(QLatin1Char('\n'));
        KSyntaxHighlighting::State state;
        for (const QString &line : lines) {
            state = highlightLine(QStringView(line), state);
        }
    }
    int formats() const { return formats_; }
    int begins() const { return begins_; }
    int ends() const { return ends_; }

private:
    int formats_ = 0;
    int begins_ = 0;
    int ends_ = 0;
};

QString joinDefinitions(const QStringList &names, int limit = 12)
{
    const QStringList head = names.mid(0, limit);
    QString text = head.join(QStringLiteral(", "));
    if (names.size() > limit) {
        text += QStringLiteral(" … (+%1)").arg(names.size() - limit);
    }
    return text;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const QString resourcesDir = QStringLiteral(VEDIT_SYNTAX_RESOURCES_DIR);
    std::printf("INFO resources_dir=%s\n", resourcesDir.toUtf8().constData());

    // ---- A: private-path load ----
    vedit::kde::DataConfig config;
    config.private_dir = resourcesDir; // contains syntax/
    vedit::kde::KsyntaxData data(config);
    const auto &report = data.load();
    std::printf("LOAD strategy=%s\n", report.strategy.toUtf8().constData());
    std::printf("LOAD private_dir=%s\n", report.private_dir.toUtf8().constData());
    std::printf("LOAD search_paths=%s\n",
                report.search_paths.join(QStringLiteral(", ")).toUtf8().constData());
    std::printf("LOAD definitions=%lld themes=%lld\n",
                static_cast<long long>(report.definition_names.size()),
                static_cast<long long>(report.theme_names.size()));
    std::printf("LOAD definition_names=%s\n", joinDefinitions(report.definition_names).toUtf8().constData());
    std::printf("LOAD theme_count=%lld\n", static_cast<long long>(report.theme_names.size()));
    std::printf("LOAD probe_v=%s probe_sv=%s\n",
                report.probe_v_definition.toUtf8().constData(),
                report.probe_sv_definition.toUtf8().constData());
    for (const QString &diagnostic : report.diagnostics) {
        std::printf("DIAG %s\n", diagnostic.toUtf8().constData());
    }
    if (!report.private_definition_files.isEmpty()) {
        std::printf("LOAD private_definition_files=%s\n",
                    report.private_definition_files.join(QStringLiteral(", ")).toUtf8().constData());
    }
    check("load.ok", report.ok);
    check("load.verilog-available", report.verilog_available);
    check("load.systemverilog-available", report.systemverilog_available);
    check("load.private-dir-used", !report.search_paths.isEmpty());
    check("load.private-shipped-verilog", report.private_verilog_shipped);
    check("load.private-shipped-systemverilog", report.private_systemverilog_shipped);
    check("load.forbidden-none", report.forbidden_definitions.isEmpty());
    check("load.v-resolved-by-name-not-filename",
          report.probe_v_definition != QLatin1String("Verilog"),
          QStringLiteral("filename probe -> %1 (expected the known V/vlang trap); explicit name mapping is used instead")
              .arg(report.probe_v_definition));
    check("load.extension-map-v", vedit::kde::KsyntaxData::languageNameForExtension(QStringLiteral("a.v")) ==
                                      QLatin1String("Verilog"));
    check("load.extension-map-sv", vedit::kde::KsyntaxData::languageNameForExtension(QStringLiteral("a.sv")) ==
                                       QLatin1String("SystemVerilog"));
    check("load.resolve-sv-when-present",
          data.resolveLanguageForFile(QStringLiteral("a.sv")) == QLatin1String("SystemVerilog"));

    // ---- B: missing SystemVerilog degrades diagnosably ----
    QTemporaryDir degradedDir;
    if (degradedDir.isValid()) {
        QDir srcDir(resourcesDir + QStringLiteral("/syntax"));
        QDir dstDir(degradedDir.path() + QStringLiteral("/syntax"));
        if (!dstDir.exists()) {
            QDir().mkpath(degradedDir.path() + QStringLiteral("/syntax"));
        }
        for (const QString &entry : srcDir.entryList(QStringList{QStringLiteral("*.xml")}, QDir::Files)) {
            if (entry.contains(QLatin1String("systemverilog"))) {
                continue; // simulate the C7-pending delivery: .sv is not shipped
            }
            QFile::copy(srcDir.filePath(entry), dstDir.filePath(entry));
        }
        vedit::kde::DataConfig degradedConfig;
        degradedConfig.private_dir = degradedDir.path();
        vedit::kde::KsyntaxData degraded(degradedConfig);
        const auto &degradedReport = degraded.load();
        std::printf("LOAD degraded.definitions=%lld sv_available=%d\n",
                    static_cast<long long>(degradedReport.definition_names.size()),
                    degradedReport.systemverilog_available ? 1 : 0);
        for (const QString &diagnostic : degradedReport.diagnostics) {
            std::printf("DIAG degraded: %s\n", diagnostic.toUtf8().constData());
        }
        check("degrade.verilog-still-available", degradedReport.verilog_available);
        check("degrade.sv-not-available", !degradedReport.systemverilog_available);
        check("degrade.sv-missing-diagnostic",
              hasDiagnostic(degradedReport, QStringLiteral("SystemVerilog definition not shipped")));
        check("degrade.sv-falls-back-to-verilog",
              degraded.resolveLanguageForFile(QStringLiteral("a.sv")) == QLatin1String("Verilog"));
        check("degrade.fallback-diagnostic",
              hasDiagnostic(degraded.report(), QStringLiteral("falling back to Verilog")));
        check("degrade.no-crash", true);
    }

    // ---- C: forbidden KDE definitions absent from our data dir ----
    QStringList forbiddenHits;
    {
        QDirIterator it(resourcesDir + QStringLiteral("/syntax"),
                        QStringList{QStringLiteral("*.xml")}, QDir::Files);
        while (it.hasNext()) {
            const QString path = it.next();
            if (vedit::kde::KsyntaxData::isForbiddenDefinitionFile(path)) {
                forbiddenHits.append(path);
            }
        }
    }
    check("data.no-forbidden-kde-definitions", forbiddenHits.isEmpty(), forbiddenHits.join(QStringLiteral(", ")));
    {
        const QDir syntaxDir(resourcesDir + QStringLiteral("/syntax"));
        const QStringList names = syntaxDir.entryList(QStringList{QStringLiteral("*.xml")}, QDir::Files);
        check("data.no-kde-file-names",
              !names.contains(QStringLiteral("verilog.xml")) &&
                  !names.contains(QStringLiteral("systemverilog.xml")),
              names.join(QStringLiteral(", ")));
        std::printf("DATA files=%s\n", names.join(QStringLiteral(", ")).toUtf8().constData());
    }

    // ---- D: definitions are valid (Repository loaded them from our dir) ----
    auto *repository = data.repository();
    const auto verilog = repository->definitionForName(QStringLiteral("Verilog"));
    const auto systemverilog = repository->definitionForName(QStringLiteral("SystemVerilog"));
    check("def.verilog-valid", verilog.isValid());
    check("def.systemverilog-valid", systemverilog.isValid());
    check("def.verilog-section", verilog.section() == QLatin1String("Hardware"), verilog.section());
    check("def.systemverilog-extensions-contain-sv",
          systemverilog.extensions().contains(QLatin1String("*.sv")),
          systemverilog.extensions().join(QStringLiteral(", ")));

    // ---- E: highlighting works and folds exist (A4) ----
    FoldingProbe probe;
    const QString verilogSample = QStringLiteral(
        "module m;\n"
        "  wire a;\n"
        "  generate\n"
        "    if (1) begin\n"
        "      case (a)\n"
        "        1: begin end\n"
        "      endcase\n"
        "    end\n"
        "  endgenerate\n"
        "  function f;\n"
        "  endfunction\n"
        "  task t;\n"
        "  endtask\n"
        "endmodule\n");
    probe.run(verilog, verilogSample);
    std::printf("FOLD verilog formats=%d begins=%d ends=%d\n", probe.formats(), probe.begins(), probe.ends());
    check("fold.verilog-highlighting", probe.formats() > 0);
    check("fold.verilog-regions-begin", probe.begins() >= 6, QString::number(probe.begins()));
    check("fold.verilog-regions-balanced", probe.ends() >= 6, QString::number(probe.ends()));

    const QString systemVerilogSample = QStringLiteral(
        "module m;\n"
        "  interface i;\n"
        "  endinterface\n"
        "  program p;\n"
        "  endprogram\n"
        "  class c;\n"
        "  endclass\n"
        "  function f;\n"
        "  endfunction\n"
        "  task t;\n"
        "  endtask\n"
        "  generate\n"
        "  endgenerate\n"
        "  case (x)\n"
        "  endcase\n"
        "  begin\n"
        "  end\n"
        "endmodule\n");
    probe.run(systemverilog, systemVerilogSample);
    std::printf("FOLD systemverilog formats=%d begins=%d ends=%d\n", probe.formats(), probe.begins(),
                probe.ends());
    check("fold.sv-highlighting", probe.formats() > 0);
    check("fold.sv-a4-constructs-begin", probe.begins() >= 9, QString::number(probe.begins()));
    check("fold.sv-a4-constructs-balanced", probe.ends() >= 9, QString::number(probe.ends()));

    std::printf("SYN-KDE-DATA-SELFTEST ok=%d checks=%d failures=%d\n", g_failures == 0 ? 1 : 0,
                g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
