// Product wiring of the syntax stack (Slice 1, task T12: G2 + G3).
//
// This translation unit is linked into `vedit-app` with WHOLE_ARCHIVE (see
// CMakeLists.txt): a plain static-library link would drop it, because until
// edit-core (G1) calls the syntax API nothing references it. Keeping it in the
// binary is what makes the link observable — `nm` on the product binary shows
// the grammar registry and `readelf -d` shows the three libraries.
//
// Startup is a Qt pre-routine, not a call from main(): the shell never learns
// about syntax, and `--selfcheck` (which builds a QCoreApplication and exits
// before the event loop) still exercises the real initialisation path.
#include "syntax_product.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDebug>
#include <QStringList>

#include <KTextEditor/Editor>
#include <ktexteditor_version.h>

#include <mutex>

#include "bindings.h"
#include "grammar_binding.h"
#include "ksyntax_data.h"
#include "syntax_service.h"

namespace vedit::syntax::product {
namespace {

QString privateSyntaxDir()
{
    const QByteArray fromEnvironment = qgetenv("VEDIT_SYNTAX_DIR");
    if (fromEnvironment.isEmpty()) {
        return QString();
    }
    return QString::fromLocal8Bit(fromEnvironment);
}

vedit::kde::KsyntaxData &syntaxData()
{
    static vedit::kde::KsyntaxData data = [] {
        vedit::kde::DataConfig config;
        config.private_dir = privateSyntaxDir();
        return vedit::kde::KsyntaxData(config);
    }();
    return data;
}

Report buildReport()
{
    Report report;
    const CompileTimeConfig config = compileTimeConfig();
    report.compiled_grammar = QString::fromStdString(config.compiled_grammar);
    report.backend_compiled = config.backend_compiled;
    report.static_backend = config.static_backend;

    report.registered_now = grammars::registerBuiltinGrammars();
    for (const std::string &id : GrammarRegistry::instance().ids()) {
        report.registered_grammars.append(QString::fromStdString(id));
    }

    // Activate the compiled grammar in the runtime provider and create it once:
    // this is the product's backend capability probe and the reference that
    // keeps the tree-sitter backend (and libtree-sitter) in the linked binary.
    SyntaxService &service = SyntaxService::instance();
    report.grammar_activated = service.activate(report.compiled_grammar.toStdString());
    if (report.grammar_activated) {
        report.active_grammar = QString::fromStdString(service.activeGrammarId());
    }
    report.backend_available = service.available();
    if (std::unique_ptr<SyntaxProvider> provider = service.createProvider()) {
        report.provider_created = provider != nullptr;
    }

    const vedit::kde::LoadReport &data = syntaxData().load();
    report.syntax_data_ok = data.ok;
    report.syntax_private_dir = data.private_dir;
    report.syntax_definition_count = static_cast<int>(data.definition_names.size());
    report.syntax_private_files = data.private_definition_files;
    report.verilog_definition = data.verilog_available;
    report.systemverilog_definition = data.systemverilog_available;
    report.theme_names = data.theme_names;
    report.embargoed_definitions = data.forbidden_definitions;
    report.diagnostics = data.diagnostics;

    // A real, non-inline symbol reference: this call is what keeps libKF6TextEditor
    // in the product's NEEDED set (G2), and a null result means the kernel is not
    // usable. The version is the pinned build version from the header.
    if (KTextEditor::Editor::instance() != nullptr) {
        report.editing_kernel = QStringLiteral("KTextEditor %1").arg(QStringLiteral(KTEXTEDITOR_VERSION_STRING));
    } else {
        report.diagnostics.append(
            QStringLiteral("KTextEditor::Editor::instance() returned null: the editing kernel "
                           "is not usable (G1/edit-core owns the document integration)"));
    }
    return report;
}

Report &storage()
{
    static Report report;
    return report;
}

std::once_flag &onceFlag()
{
    static std::once_flag flag;
    return flag;
}

} // namespace

const Report &initialize()
{
    std::call_once(onceFlag(), [] { storage() = buildReport(); });
    return storage();
}

const Report &report()
{
    return initialize();
}

void noteStartupHook()
{
    storage().startup_hook_ran = true;
}

KSyntaxHighlighting::Repository *repository()
{
    return syntaxData().repository();
}

QString describe()
{
    const Report &current = initialize();
    QStringList parts;
    parts << QStringLiteral("grammar=%1").arg(current.compiled_grammar);
    parts << QStringLiteral("backend=%1").arg(current.backend_compiled ? 1 : 0);
    parts << QStringLiteral("registered=[%1]").arg(current.registered_grammars.join(QLatin1Char(',')));
    parts << QStringLiteral("active='%1'").arg(current.active_grammar);
    parts << QStringLiteral("backend-available=%1").arg(current.backend_available ? 1 : 0);
    parts << QStringLiteral("private-dir='%1'").arg(current.syntax_private_dir);
    parts << QStringLiteral("definitions=%1").arg(current.syntax_definition_count);
    parts << QStringLiteral("own-xml=[%1]").arg(current.syntax_private_files.join(QLatin1Char(',')));
    parts << QStringLiteral("themes=%1").arg(current.theme_names.size());
    parts << QStringLiteral("verilog=%1").arg(current.verilog_definition ? 1 : 0);
    parts << QStringLiteral("systemverilog=%1").arg(current.systemverilog_definition ? 1 : 0);
    parts << QStringLiteral("kernel='%1'").arg(current.editing_kernel);
    parts << QStringLiteral("embargoed=[%1]").arg(current.embargoed_definitions.join(QLatin1Char(',')));
    parts << QStringLiteral("startup-hook=%1").arg(current.startup_hook_ran ? 1 : 0);
    return parts.join(QStringLiteral(" "));
}

} // namespace vedit::syntax::product

// Runs when the QCoreApplication is constructed, i.e. for the product binary and
// for every selftest that creates one — before any window or event loop exists.
// It must be at file scope: Q_COREAPP_STARTUP_FUNCTION() generates a constructor
// function that calls this name unqualified.
static void veditSyntaxProductStartup()
{
    vedit::syntax::product::initialize();
    vedit::syntax::product::noteStartupHook();
    qInfo().noquote() << "syntax-product:" << vedit::syntax::product::describe();
}
Q_COREAPP_STARTUP_FUNCTION(veditSyntaxProductStartup)
