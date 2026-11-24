#pragma once

// AC-5.1/5.6/5.8 (+ AC-N2): the waiver ACTION and DATA layer for the hover and
// the diagnostics panel.
//
// The two engines scope a waiver in completely different ways, so a waiver is
// never "just append a line":
//
//   * verible waives ONE DIAGNOSTIC per entry: a line-oriented
//     `waive --rule=<rule> --line=<N[:M]> --location="<regex>"` command. The
//     rule id is the diagnostic's, the lines are 1-based, and the location is a
//     RE2 regex that defaults to the file NAME the diagnostic belongs to
//     (`(^|/)<name>$`), which is what makes one line cover every directory.
//   * slang waives a (file|hier, diagnostic) SCOPE: a TOML `[[waivers]]` block
//     whose ONLY allowed keys are `file`, `hier`, `diagnostic` and `regex`, with
//     EXACTLY ONE scope key (`file` XOR `hier`) and a `diagnostic` that is the
//     engine's warning option name (a slang `-W` option, not the JSON code).
//
// The data layer below decides WHAT would be written and refuses the cases the
// engines cannot express - BEFORE a worker is started, so the panel can show a
// typed reason instead of a failed job:
//
//   * a slang row without an option name (a typical error) is REFUSED with the
//     whole-scope explanation; it is never widened into a whole-file waiver
//     (AC-5.8), and the refusal text is the one the ABI uses.
//   * a block with zero or two scope keys, an unknown key or a malformed string
//     is refused by `validateSlangWaiverBlock()`; the accepted grammar matches
//     `crates/vd-ffi/src/waiver.rs` (`parse_slang_waivers`), so a preview can
//     never promise a file slang would refuse to load.
//
// The preview builders (`veribleWaiverLine`, `slangWaiverBlock`) mirror the ABI
// writers exactly (location derivation, scope normalisation, TOML escaping); the
// self-test asserts the engine's real output equals the preview.
//
// The action layer (`WaiverActions`) is the real thing: every operation is ONE
// asynchronous Rust job (`crates/vd-ffi/src/waiver.rs`: atomic sibling-temp
// rename, dedupe, path validation, a file slang would refuse is left untouched),
// so nothing touches the disk on the caller's stack - exactly like the lint and
// VCS jobs. It exists only when the lane links the Rust service layer
// (`VEDIT_DIAGNOSTICS_HAVE_FFI`), because the data layer must stay testable on a
// fake-only build.

#include "diagnostics_types.h"
#include "slang_backend.h"

#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

#include <memory>

namespace vedit::workspace::diagnostics {

/// One waiver entry derived from a diagnostic row: the single value both the
/// preview and the engine call are built from, so they cannot disagree.
struct WaiverEntry {
    bool valid = false;
    /// Which engine's waiver FILE format this entry belongs to (the row's
    /// origin engine is irrelevant: the user waives into the file they chose).
    LintEngineKind engine = LintEngineKind::VeribleLint;
    /// The file the waiver is scoped to, as the diagnostic names it (required).
    QString file;
    /// 1-based first line; 0 = a file-wide verible waiver (no `--line`). Slang
    /// waivers have no line scope and ignore this.
    int line = 0;
    /// 1-based INCLUSIVE last line of a verible range; 0 (or == line) = the
    /// single line.
    int endLine = 0;
    /// The verible rule id (empty for a slang entry).
    QString rule;
    /// The slang warning option name (empty for a verible entry).
    QString optionName;
    /// An explicit verible `--location` (empty = derive from `file`).
    QString location;
    /// Why `valid` is false; user-facing, empty when valid.
    QString reason;
};

/// Plan ONE entry from a diagnostic row.
///
/// `optionName` is the engine's typed option name (`VdDiagnostic.option_name`);
/// it is required for a slang entry and ignored for verible. `fileFallback` is
/// used when the row carries no file (single-file runs do not fill it): the
/// active file the caller knows. A slang row without an option name is refused
/// with the whole-scope explanation (AC-5.8) - the product must never turn one
/// error into a file-wide waiver.
WaiverEntry planWaiverEntry(LintEngineKind engine,
                            const DiagnosticEntry &row,
                            const QString &optionName = QString(),
                            const QString &fileFallback = QString());

/// The exact command line a valid verible entry writes. Empty when the entry is
/// not a usable verible entry. Mirrors `verible_line` + `default_location` in
/// `crates/vd-ffi/src/waiver.rs`.
QString veribleWaiverLine(const WaiverEntry &entry);

/// The anchored file-name regex verible defaults to: `(^|/)<escaped name>$`.
/// Mirrors `default_location`/`regex_escape` in the ABI.
QString veribleLocationForFile(const QString &file);

/// The scope value a slang `file` waiver uses: backslashes normalised, an
/// absolute path kept, a relative one prefixed with `**/`. Mirrors `slang_scope`
/// in the ABI, so a preview and the written block say the same thing.
QString slangScopeForFile(const QString &file);

struct SlangWaiverText {
    bool valid = false;
    QString content; ///< the `[[waivers]]` block; empty when invalid
    QString reason;  ///< why it is invalid; empty when valid
};

/// The TOML block for ONE (file, option) waiver with exactly one scope key.
/// `regex` narrows the block (it is a legal alternative to `diagnostic`, and an
/// existing file may carry one) and is omitted when empty.
SlangWaiverText slangWaiverBlock(const QString &file,
                                 const QString &optionName,
                                 const QString &regex = QString());

/// Validate an existing slang waiver file / block the way slang loads it.
/// Returns the FIRST problem (a sentence naming the offending line), or an empty
/// string when the content is loadable. Accepts comments and blank lines, only
/// `[[waivers]]` tables, only the four legal keys, exactly one scope key per
/// block, and a quoted TOML string value.
QString validateSlangWaiverBlock(const QString &content);

#if defined(VEDIT_DIAGNOSTICS_HAVE_FFI)

/// The asynchronous waiver actions. One instance owns one Rust service handle
/// (cheap: no threads until a job starts); the caller owns it for the lifetime
/// of the panel and polls the returned job id from its own timer.
class WaiverActions {
public:
    WaiverActions();
    ~WaiverActions();

    WaiverActions(const WaiverActions &) = delete;
    WaiverActions &operator=(const WaiverActions &) = delete;

    /// False when the Rust service could not be created (never a crash).
    bool available() const;

    /// Append ONE entry to `waiverPath` (verible: one command line; slang: one
    /// `[[waivers]]` block). The entry must be a valid plan: an invalid entry,
    /// an empty path, or a slang entry without an option name is refused here
    /// with `error` and NO job is started. Returns the job id (> 0) otherwise.
    quint64 append(const QString &waiverPath,
                   const WaiverEntry &entry,
                   bool createParentDirs,
                   QString *error);

    /// Register `waiverPath` in the settings store at `settingsPath` (verible:
    /// appended to the waiver-file list; slang: the single waiver file), so the
    /// next lint pass really uses it. Returns the job id (> 0) or 0 + `error`.
    quint64 registerFile(const QString &settingsPath,
                         LintEngineKind engine,
                         const QString &waiverPath,
                         QString *error);

    enum class State {
        Pending = 0,
        Ready = 1,
        Failed = 2,
        Unknown = 3, ///< the job id is not live (already polled/released)
    };

    /// The result of one polled job. A READY job fills the writer facts; a
    /// FAILED job fills `error` (the engine's own sentence).
    struct Result {
        State state = State::Pending;
        bool written = false;   ///< a file was really written
        bool deduped = false;   ///< the entry was already present: nothing written
        QString path;           ///< the file the operation touched
        QString content;        ///< the resulting file content (preview included)
        int entries = 0;        ///< entries in the resulting content
        int skipped = 0;        ///< rows the operation refused to widen
        QStringList warnings;   ///< one sentence per skipped row / store problem
        QString error;          ///< why the job failed (empty otherwise)
    };

    /// Poll one job. PENDING leaves everything empty; a terminal poll releases
    /// the job's slot (the engine side does the same on its side).
    Result poll(quint64 job);

    /// Drop a job without reporting an error (e.g. the panel was closed).
    void release(quint64 job);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif // VEDIT_DIAGNOSTICS_HAVE_FFI

} // namespace vedit::workspace::diagnostics
