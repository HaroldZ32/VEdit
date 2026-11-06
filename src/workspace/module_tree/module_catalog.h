#pragma once

// The module tree lane's data model after UX batch 2 (item 1/2).
//
// It is a PLAIN value model of the project-wide module catalog the Rust service
// returns through the `vd_catalog_*` C ABI: every MODULE definition with its
// definition position, its `is_top` flag, its ports and its child
// instantiations. It deliberately carries NO nets/drivers/loads - the user
// ruling for this batch is that the module tree is a pure module hierarchy - and
// no Qt widget or C ABI type, so the whole decision surface is unit-testable.
//
// Positions are kept EXACTLY as the ABI reports them: **0-based** file/line/
// column (the crate documentation and `crates/vd-ffi/tests/ux_batch_2.rs` are
// the source of truth; the ABI header spells "0-based" on every field). The
// conversion to the 1-based positions of the application-wide jump contract
// happens ONLY in the `jumpFrom*` helpers below: the tree panel fills its item
// roles from `jumpFromPosition`, the interface panel from `jumpFromPort` /
// `jumpFromInstance`, and `describePosition` converts for display only. No
// consumer adds 1 by hand (review Nit 7).
#include <QString>
#include <QStringList>
#include <QVector>

namespace vedit::workspace::module_tree {

/// One port of one module definition (`VdModulePort`).
struct ModulePort {
    QString name;
    QString direction; ///< `In`, `Out`, `InOut` or `Ref` (verbatim)
    QString type;      ///< declared type verbatim, e.g. `logic[3:0]`
    QString file;
    int line = -1;   ///< 0-based
    int column = -1; ///< 0-based
};

/// One child instantiation inside a module definition (`VdModuleInstance`).
struct ModuleInstance {
    QString module; ///< instantiated module name
    QString path;   ///< elaborated instance path (`top_a.u_shared`)
    QString file;   ///< file of the instantiation site
    int line = -1;  ///< 0-based
    int column = -1;
};

/// One module definition (`VdModule`); the name is the deduplication key.
struct ModuleDefinition {
    QString name;
    QString file;    ///< file of the module definition
    int line = -1;   ///< 0-based definition line
    int column = -1; ///< 0-based definition column
    bool isTop = false; ///< never instantiated anywhere == a design root
    QVector<ModulePort> ports;
    QVector<ModuleInstance> children; ///< one entry per instantiation site
};

/// One catalog run (`VdModuleCatalog`), copied into plain values.
struct ModuleCatalog {
    QString projectRoot;
    QString tool; ///< `slang`
    quint64 elapsedMs = 0;
    quint64 peakRssKb = 0;
    QString warning; ///< oversize warning from the service, or empty
    int fileSetSize = 0;
    QVector<ModuleDefinition> modules; ///< sorted by name (the ABI guarantees it)

    /// True when the service answered (even with zero modules).
    bool valid = false;
    /// Non-empty when the last run failed; the panels show it as ONE line.
    QString error;

    const ModuleDefinition *find(const QString &name) const;
    /// The modules the ABI marked `is_top`, in catalog order.
    QStringList topModuleNames() const;
    /// `slang · 6 module(s) · 3 top(s) · 5 ms · 19.1 MB peak` (oversize appended).
    QString summaryLine() const;
};

/// A position the jump contract accepts: 1-based line/column (see
/// src/workspace/jump/jump_target.h). An empty `file` or a line < 1 means
/// "this thing has no location" - the caller then performs NO jump.
struct ModuleJump {
    QString file;
    int line = 0;   ///< 1-based
    int column = 0; ///< 1-based

    bool isValid() const { return !file.isEmpty() && line >= 1; }
};

/// 0-based service position -> 1-based jump position.
ModuleJump jumpFromPosition(const QString &file, int line, int column);
ModuleJump jumpFromDefinition(const ModuleDefinition &module);
ModuleJump jumpFromPort(const ModulePort &port);
ModuleJump jumpFromInstance(const ModuleInstance &instance);
/// `top_a.sv:12:6` (1-based, for a human-readable cell).
QString describePosition(const QString &file, int line, int column);

// --- packed width derivation (item 2: 名称/方向/类型/位宽/位置) --------------

/// The width a port's declared `type` string implies.
struct PortWidth {
    bool known = false;
    int width = 0;
    /// Why the width is unknown (shown as the cell's tooltip); empty when known.
    QString problem;
};

/// Derives the packed width from the verbatim ABI `type` string:
///   * every literal packed range `[msb:lsb]` multiplies in (`logic[3:0]` -> 4,
///     `logic[1:0][3:0]` -> 8);
///   * NO packed dimension -> 1 (`logic`, `int`, an interface name);
///   * a range that is not two integer literals (`logic[WIDTH-1:0]`) -> unknown,
///     which the panel renders as an EMPTY cell plus a tooltip - never as a
///     guessed number.
PortWidth portWidthFromType(const QString &type);

// --- focused module of the interface panel ---------------------------------

/// Rule (b): the module whose DEFINITION is the nearest one at or above the
/// caret line in the SAME file (catalog definition positions, 0-based caret
/// line). Returns nullptr when no definition precedes the caret in that file.
const ModuleDefinition *moduleAtCaret(const ModuleCatalog &catalog, const QString &file,
                                      int caretLine0);

/// One parent instantiation of the focused module (Ctrl+double-click target).
struct InstantiationSite {
    QString parentModule; ///< module whose body contains the instantiation
    ModuleInstance instance;
};

/// Every child-instantiation edge in the whole catalog whose `module` equals
/// `moduleName`, sorted by parent module name and source position so a chooser
/// and the tests are deterministic. Several edges == several chooser entries.
QVector<InstantiationSite> instantiationsOf(const ModuleCatalog &catalog,
                                            const QString &moduleName);

/// True when two paths name the same file (canonical when the file exists,
/// absolute otherwise). The catalog and the editor can disagree about the
/// spelling of the project root, so the focus rule never compares raw strings.
bool sameFile(const QString &left, const QString &right);

} // namespace vedit::workspace::module_tree
