#pragma once

// Product wiring of the syntax stack (Slice 1, task T12: G2 + G3).
//
// The shell owns window, panels and docks; the syntax stack is Qt-side: the
// KSyntaxHighlighting data layer plus the tree-sitter `SyntaxProvider` seam.
// `src/app` must not have to know about either, so this lane registers itself
// with Qt's application startup hook: every `QCoreApplication` (the real
// `vedit-app` and every `--selfcheck` run) executes `initialize()` exactly once
// before the event loop starts.
//
// What the product gains from this translation unit being LINKED (G2):
//   1. the built-in grammars are registered into the provider registry
//      (compile-time set decided by the grammar lane);
//   2. the bundled syntax definitions are loaded through the private-path
//      loader (own LGPL-2.1+ XML only; KDE's GPLv3+ definitions stay embargoed);
//   3. the editing kernel (KF6TextEditor) is proven present and its version is
//      recorded — the kernel's document integration belongs to edit-core (G1).
//
// Everything here is headless-provable (grammar id, registry ids, definition
// names, applied format ranges). It makes no claim about what the user sees:
// the demo editor does not paint through this stack until edit-core lands.
#include <QString>
#include <QStringList>

#include <cstddef>

namespace KSyntaxHighlighting {
class Repository;
}

namespace vedit::syntax::product {

/// What one initialisation of the syntax stack observed.
struct Report {
    /// Grammar id baked into the build (`grammar_config_generated.h`).
    QString compiled_grammar;
    /// Whether the tree-sitter backend was compiled in.
    bool backend_compiled = false;
    /// Whether libtree-sitter was linked statically (C18).
    bool static_backend = false;
    /// Bindings this initialisation added (`0` when they were already present).
    std::size_t registered_now = 0;
    /// Grammar ids the registry holds after registration.
    QStringList registered_grammars;
    /// Whether `SyntaxService::activate()` accepted the compiled grammar id.
    bool grammar_activated = false;
    /// Grammar id the runtime provider was activated for (empty = not activated).
    QString active_grammar;
    /// Whether `SyntaxService` reports a usable provider for the active grammar.
    bool backend_available = false;
    /// Whether creating the provider for the active grammar succeeded.
    bool provider_created = false;
    /// Whether the syntax-data loader completed.
    bool syntax_data_ok = false;
    /// The private data directory the loader was given (empty = standard paths).
    QString syntax_private_dir;
    /// Number of definitions the repository exposes.
    int syntax_definition_count = 0;
    /// Definition files found in the private directory (ours).
    QStringList syntax_private_files;
    /// Whether a Verilog definition is usable.
    bool verilog_definition = false;
    /// Whether a SystemVerilog definition is usable.
    bool systemverilog_definition = false;
    /// Theme names the repository exposes.
    QStringList theme_names;
    /// KDE definitions the loader refused to use (C7/C11).
    QStringList embargoed_definitions;
    /// Non-fatal problems collected while initialising.
    QStringList diagnostics;
    /// `KTextEditor <version>` when the editing kernel is present, else empty.
    QString editing_kernel;
    /// Whether the Qt startup hook actually ran (true in any real app run).
    bool startup_hook_ran = false;
};

/// Initialise the syntax stack once. Idempotent and thread-safe; the Qt startup
/// hook calls it, and so does the lane's acceptance test.
const Report &initialize();

/// The report of the initialisation (calls `initialize()` when needed).
const Report &report();

/// Record that the Qt startup hook executed (set by the hook, asserted by tests).
void noteStartupHook();

/// The repository backing the loaded definitions, or nullptr before loading.
KSyntaxHighlighting::Repository *repository();

/// One-line, log-friendly summary of the report.
QString describe();

} // namespace vedit::syntax::product
