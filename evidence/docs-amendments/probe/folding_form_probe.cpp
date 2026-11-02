// Folding-form probe for AMENDMENTS rev 2 (C15 item 4).
//
// Question: in KSyntaxHighlighting's syntax-definition XML, is the fold source
// expressed by a literal `<folding begin="…" end="…"/>` element (as AMENDMENTS
// A4.1/A4.2 literally wrote), or by `beginRegion` / `endRegion` attributes on a
// rule (as VEdit's own definitions use)?
//
// The probe takes VEdit's real definition (`resources/syntax/vedit-verilog.xml`)
// and derives two variants from it, so nothing but the fold notation differs:
//
//   CASE attribute-form        the file as shipped (beginRegion/endRegion)
//   CASE literal-element-form  the same file with every beginRegion/endRegion
//                              attribute rule removed and a literal
//                              `<folding begin="module" end="endmodule"/>`
//                              element inserted instead (both placements:
//                              directly under <highlighting> and inside the
//                              "Normal" context)
//
// Both are loaded through the public KSyntaxHighlighting API and measured with
// the same observable as src/syntax/kde/tests/validate_syntax_data.cpp:55-86:
// count applyFolding() Begin/End callbacks.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStringList>
#include <QStringView>
#include <QTemporaryDir>

#include <KSyntaxHighlighting/AbstractHighlighter>
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/FoldingRegion>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/State>

#include <cstdio>

namespace {

class FoldingProbe final : public KSyntaxHighlighting::AbstractHighlighter
{
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
        KSyntaxHighlighting::State state;
        const QStringList lines = text.split(QLatin1Char('\n'));
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

bool writeFile(const QString &path, const QString &content)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(content.toUtf8()) >= 0;
}

QString attributeForm(const QString &shipped)
{
    QString text = shipped;
    text.replace(QStringLiteral("name=\"Verilog\""), QStringLiteral("name=\"ProbeAttributeForm\""));
    return text;
}

QString renamedOnly(const QString &content, const QString &from, const QString &to)
{
    QString text = content;
    text.replace(from, to);
    return text;
}

QString literalElementForm(const QString &shipped)
{
    QString text = shipped;
    text.replace(QStringLiteral("name=\"Verilog\""), QStringLiteral("name=\"ProbeLiteralFolding\""));

    QStringList kept;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.contains(QLatin1String("beginRegion")) || line.contains(QLatin1String("endRegion"))) {
            continue; // drop every attribute-form fold source
        }
        kept.append(line);
    }
    text = kept.join(QLatin1Char('\n'));

    // Placement 1: directly under <highlighting>.
    text.replace(QStringLiteral("  <highlighting>"),
                 QStringLiteral("  <highlighting>\n"
                                "    <folding begin=\"module\" end=\"endmodule\"/>"));
    // Placement 2: inside the "Normal" context.
    text.replace(
        QStringLiteral("<context name=\"Normal\" attribute=\"Normal Text\" lineEndContext=\"#stay\">"),
        QStringLiteral("<context name=\"Normal\" attribute=\"Normal Text\" lineEndContext=\"#stay\">\n"
                       "        <folding begin=\"module\" end=\"endmodule\"/>"));
    return text;
}

QString correctedAttributeForm(const QString &shipped)
{
    QString text = shipped;
    text.replace(QStringLiteral("name=\"Verilog\""), QStringLiteral("name=\"ProbeCorrectedForm\""));

    // Drop only the literal-keyword folding rules (the ones the loader reports
    // as "Unknown keyword list"); every other line stays as shipped.
    QStringList kept;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const bool foldingKeywordRule =
            line.contains(QLatin1String("<keyword")) &&
            (line.contains(QLatin1String("beginRegion")) || line.contains(QLatin1String("endRegion")));
        if (foldingKeywordRule) {
            continue;
        }
        kept.append(line);
    }
    text = kept.join(QLatin1Char('\n'));

    // The supported form: keyword LISTS referenced by name, with beginRegion /
    // endRegion on the referencing rule (this is what KDE's own definitions do).
    text.replace(
        QStringLiteral("  <highlighting>"),
        QStringLiteral("  <highlighting>\n"
                       "    <list name=\"veditFoldBegin\">\n"
                       "      <item>module</item><item>begin</item><item>case</item>\n"
                       "      <item>casex</item><item>casez</item><item>function</item>\n"
                       "      <item>task</item><item>generate</item>\n"
                       "    </list>\n"
                       "    <list name=\"veditFoldEnd\">\n"
                       "      <item>endmodule</item><item>end</item><item>endcase</item>\n"
                       "      <item>endfunction</item><item>endtask</item><item>endgenerate</item>\n"
                       "    </list>"));
    text.replace(
        QStringLiteral("<context name=\"Normal\" attribute=\"Normal Text\" lineEndContext=\"#stay\">"),
        QStringLiteral(
            "<context name=\"Normal\" attribute=\"Normal Text\" lineEndContext=\"#stay\">\n"
            "        <keyword attribute=\"Keyword\" context=\"#stay\" "
            "String=\"veditFoldBegin\" beginRegion=\"veditFold\"/>\n"
            "        <keyword attribute=\"Keyword\" context=\"#stay\" "
            "String=\"veditFoldEnd\" endRegion=\"veditFold\"/>"));
    return text;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const QString sourcePath =
        argc > 1 ? QString::fromLocal8Bit(argv[1])
                 : QStringLiteral("/vedit-build/resources/syntax/vedit-verilog.xml");
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        std::printf("FATAL cannot read %s\n", qPrintable(sourcePath));
        return 2;
    }
    const QString shipped = QString::fromUtf8(source.readAll());
    std::printf("SAMPLE source=%s bytes=%lld\n",
                qPrintable(sourcePath),
                static_cast<long long>(shipped.toUtf8().size()));
    std::printf("SAMPLE shipped_has_literal_folding_element=%d\n",
                shipped.contains(QLatin1String("<folding")) ? 1 : 0);

    const QString svPath =
        argc > 2 ? QString::fromLocal8Bit(argv[2])
                 : QStringLiteral("/vedit-build/resources/syntax/vedit-systemverilog.xml");
    QFile svSource(svPath);
    if (!svSource.open(QIODevice::ReadOnly)) {
        std::printf("FATAL cannot read %s\n", qPrintable(svPath));
        return 2;
    }
    const QString shippedSv = QString::fromUtf8(svSource.readAll());
    std::printf("SAMPLE sv_source=%s bytes=%lld literal_folding_element=%d\n",
                qPrintable(svPath),
                static_cast<long long>(shippedSv.toUtf8().size()),
                shippedSv.contains(QLatin1String("<folding")) ? 1 : 0);

    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::printf("FATAL temporary dir invalid\n");
        return 2;
    }
    const QString syntaxDir = dir.path() + QStringLiteral("/syntax");
    if (!QDir().mkpath(syntaxDir)) {
        std::printf("FATAL cannot create %s\n", qPrintable(syntaxDir));
        return 2;
    }
    if (!writeFile(syntaxDir + QStringLiteral("/probe-attribute.xml"), attributeForm(shipped)) ||
        !writeFile(syntaxDir + QStringLiteral("/probe-literal.xml"), literalElementForm(shipped)) ||
        !writeFile(syntaxDir + QStringLiteral("/probe-corrected.xml"), correctedAttributeForm(shipped)) ||
        !writeFile(syntaxDir + QStringLiteral("/probe-shipped.xml"), shipped) ||
        !writeFile(syntaxDir + QStringLiteral("/probe-sv-attribute.xml"),
                   renamedOnly(shippedSv, QStringLiteral("name=\"SystemVerilog\""),
                               QStringLiteral("name=\"ProbeSVAttributeForm\"")))) {
        std::printf("FATAL cannot write probe definitions\n");
        return 2;
    }

    KSyntaxHighlighting::Repository repository;
    repository.addCustomSearchPath(dir.path());

    const auto attributeDefinition =
        repository.definitionForName(QStringLiteral("ProbeAttributeForm"));
    const auto literalDefinition =
        repository.definitionForName(QStringLiteral("ProbeLiteralFolding"));
    const auto correctedDefinition =
        repository.definitionForName(QStringLiteral("ProbeCorrectedForm"));
    std::printf("CASE attribute-form: definition_valid=%d\n",
                attributeDefinition.isValid() ? 1 : 0);
    std::printf("CASE literal-element-form: definition_valid=%d\n",
                literalDefinition.isValid() ? 1 : 0);
    std::printf("CASE corrected-attribute-form: definition_valid=%d\n",
                correctedDefinition.isValid() ? 1 : 0);

    // Which definition does the name "Verilog" actually resolve to? Our file
    // records author="VEdit"; KDE's built-in Verilog definition does not.
    const auto resolvedVerilog = repository.definitionForName(QStringLiteral("Verilog"));
    std::printf("RESOLVE Verilog: valid=%d author=%s section=%s license=%s\n",
                resolvedVerilog.isValid() ? 1 : 0,
                qPrintable(resolvedVerilog.author()),
                qPrintable(resolvedVerilog.section()),
                qPrintable(resolvedVerilog.license()));

    FoldingProbe resolvedProbe;
    resolvedProbe.run(resolvedVerilog,
                      QStringLiteral("module m;\n  wire a;\n  generate\n    if (1) begin\n"
                                     "      case (a)\n        1: begin end\n      endcase\n"
                                     "    end\n  endgenerate\n  function f;\n  endfunction\n"
                                     "  task t;\n  endtask\nendmodule\n"));
    std::printf("CONTROL resolved-Verilog: formats=%d begins=%d ends=%d\n",
                resolvedProbe.formats(),
                resolvedProbe.begins(),
                resolvedProbe.ends());

    // Same attribution check for SystemVerilog (our file records author="VEdit").
    const auto resolvedSv = repository.definitionForName(QStringLiteral("SystemVerilog"));
    std::printf("RESOLVE SystemVerilog: valid=%d author=%s license=%s\n",
                resolvedSv.isValid() ? 1 : 0,
                qPrintable(resolvedSv.author()),
                qPrintable(resolvedSv.license()));
    FoldingProbe resolvedSvProbe;
    resolvedSvProbe.run(resolvedSv,
                        QStringLiteral("module m;\n  interface i;\n  endinterface\n"
                                       "  program p;\n  endprogram\n  class c;\n  endclass\n"
                                       "  function f;\n  endfunction\n  task t;\n  endtask\n"
                                       "  generate\n  endgenerate\n  case (x)\n  endcase\n"
                                       "  begin\n  end\nendmodule\n"));
    std::printf("CONTROL resolved-SystemVerilog: formats=%d begins=%d ends=%d\n",
                resolvedSvProbe.formats(),
                resolvedSvProbe.begins(),
                resolvedSvProbe.ends());

    const auto svAttributeDefinition =
        repository.definitionForName(QStringLiteral("ProbeSVAttributeForm"));
    FoldingProbe svAttributeProbe;
    svAttributeProbe.run(svAttributeDefinition,
                         QStringLiteral("module m;\n  interface i;\n  endinterface\n"
                                        "  program p;\n  endprogram\n  class c;\n  endclass\n"
                                        "  function f;\n  endfunction\n  task t;\n  endtask\n"
                                        "  generate\n  endgenerate\n  case (x)\n  endcase\n"
                                        "  begin\n  end\nendmodule\n"));
    std::printf("CONTROL shipped-SV-attribute-form: definition_valid=%d formats=%d begins=%d ends=%d\n",
                svAttributeDefinition.isValid() ? 1 : 0,
                svAttributeProbe.formats(),
                svAttributeProbe.begins(),
                svAttributeProbe.ends());

    const QString text = QStringLiteral(
        "module m;\n"
        "  wire a;\n"
        "  begin\n"
        "  end\n"
        "  case (a)\n"
        "  endcase\n"
        "  generate\n"
        "  endgenerate\n"
        "  function f;\n"
        "  endfunction\n"
        "  task t;\n"
        "  endtask\n"
        "endmodule\n");
    FoldingProbe attributeProbe;
    attributeProbe.run(attributeDefinition, text);
    std::printf("CONTROL attribute-form: formats=%d begins=%d ends=%d\n",
                attributeProbe.formats(),
                attributeProbe.begins(),
                attributeProbe.ends());

    FoldingProbe literalProbe;
    literalProbe.run(literalDefinition, text);
    std::printf("CONTROL literal-element-form: formats=%d begins=%d ends=%d\n",
                literalProbe.formats(),
                literalProbe.begins(),
                literalProbe.ends());

    FoldingProbe correctedProbe;
    correctedProbe.run(correctedDefinition, text);
    std::printf("CONTROL corrected-attribute-form: formats=%d begins=%d ends=%d\n",
                correctedProbe.formats(),
                correctedProbe.begins(),
                correctedProbe.ends());

    const bool literalYieldsNoFold = literalProbe.begins() == 0 && literalProbe.ends() == 0;
    const bool shippedRulesInert = attributeProbe.begins() == 0 && attributeProbe.ends() == 0;
    const bool correctedRulesWork = correctedProbe.begins() >= 1 && correctedProbe.ends() >= 1;
    std::printf("VERDICT literal_element_yields_fold=%d shipped_rules_yield_fold=%d "
                "corrected_list_rules_yield_fold=%d\n",
                literalYieldsNoFold ? 0 : 1,
                shippedRulesInert ? 0 : 1,
                correctedRulesWork ? 1 : 0);
    return 0;
}
