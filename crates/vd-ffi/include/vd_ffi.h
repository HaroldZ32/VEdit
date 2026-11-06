#ifndef VDIT_VD_FFI_H
#define VDIT_VD_FFI_H

#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/*
 Version of the C ABI. Bumped when an existing signature changes meaning.
 */
#define VD_ABI_VERSION 1

/*
 Poll result: the job is still running.
 */
#define VD_STATUS_PENDING 0

/*
 Poll result: the job finished and the out-parameter holds its result.
 */
#define VD_STATUS_READY 1

/*
 Poll result: the job failed; `vd_service_error` holds the reason.
 */
#define VD_STATUS_FAILED 2

/*
 Error code: the caller passed a null or non-UTF-8 argument.
 */
#define VD_ERR_INVALID_ARGUMENT 1

/*
 Error code: a required file or directory does not exist.
 */
#define VD_ERR_NOT_FOUND 2

/*
 Error code: the underlying service failed (I/O, subprocess, parse).
 */
#define VD_ERR_SERVICE 3

/*
 Error code: the job was cancelled and its result discarded.
 */
#define VD_ERR_CANCELLED 4

/*
 Error code: the job id belongs to a different service.
 */
#define VD_ERR_WRONG_JOB_KIND 5

/*
 Error code: an internal invariant failed (worker panic, poisoned lock).
 */
#define VD_ERR_INTERNAL 6

/*
 Error code: the job id is unknown or was already consumed.
 */
#define VD_ERR_JOB_UNKNOWN 7

/*
 Capacity of [`VdError::message`], including the terminating NUL.
 */
#define VD_ERROR_MESSAGE_CAPACITY 256

/*
 Request kind: `textDocument/definition`.
 */
#define VD_LSP_REQ_DEFINITION 1

/*
 Request kind: `textDocument/references`.
 */
#define VD_LSP_REQ_REFERENCES 2

/*
 Request kind: `textDocument/documentSymbol` (the outline).
 */
#define VD_LSP_REQ_SYMBOLS 3

/*
 Request kind: `textDocument/rename`.
 */
#define VD_LSP_REQ_RENAME 4

/*
 Request kind: `textDocument/formatting`.
 */
#define VD_LSP_REQ_FORMAT 5

/*
 Request kind: diagnostics currently recorded for one file.
 */
#define VD_LSP_REQ_DIAGNOSTICS 6

/*
 Response kind: locations (definition / references).
 */
#define VD_LSP_RES_LOCATIONS 1

/*
 Response kind: outline symbols.
 */
#define VD_LSP_RES_SYMBOLS 2

/*
 Response kind: text edits (formatting / rename).
 */
#define VD_LSP_RES_EDITS 3

/*
 Response kind: diagnostics.
 */
#define VD_LSP_RES_DIAGNOSTICS 4

/*
 Request kind: `textDocument/hover` (T16).
 */
#define VD_LSP_REQ_HOVER 7

/*
 Request kind: `textDocument/signatureHelp` (T16).
 */
#define VD_LSP_REQ_SIGNATURE_HELP 8

/*
 Response kind: hover text (`text` + `range`).
 */
#define VD_LSP_RES_HOVER 5

/*
 Response kind: signature help (`signatures` + active indexes).
 */
#define VD_LSP_RES_SIGNATURE_HELP 6

/*
 History kind: one page of the commit list.
 */
#define VD_VCS_HISTORY_COMMITS 1

/*
 History kind: one commit's detail (meta + files + patch).
 */
#define VD_VCS_HISTORY_COMMIT 2

/*
 History kind: one page of a single file's history.
 */
#define VD_VCS_HISTORY_FILE 3

/*
 History kind: one file's blame.
 */
#define VD_VCS_HISTORY_BLAME 4

/*
 History kind: a two-commit comparison.
 */
#define VD_VCS_HISTORY_COMPARE 5

/*
 Workspace kind: the stash stack (`git stash list`).
 */
#define VD_VCS_WORKSPACE_STASHES 1

/*
 Workspace kind: the linked worktrees (`git worktree list`).
 */
#define VD_VCS_WORKSPACE_WORKTREES 2

/*
 `git add -- <paths>`: stage the listed files.
 */
#define VD_VCS_WRITE_GIT_STAGE 1

/*
 `git reset HEAD -- <paths>`: unstage the listed files.
 */
#define VD_VCS_WRITE_GIT_UNSTAGE 2

/*
 `git checkout -- <paths>`: discard changes (irreversible; confirm first).
 */
#define VD_VCS_WRITE_GIT_DISCARD 3

/*
 `git commit -m <message> [--author=<author>] [--amend] [-- <paths>]`.
 */
#define VD_VCS_WRITE_GIT_COMMIT 4

/*
 `git fetch [<remote>]`.
 */
#define VD_VCS_WRITE_GIT_FETCH 5

/*
 `git pull --ff-only [<remote> [<branch>]]`.
 */
#define VD_VCS_WRITE_GIT_PULL 6

/*
 `git push [--set-upstream] [<remote> [<branch>]]`.
 */
#define VD_VCS_WRITE_GIT_PUSH 7

/*
 `git branch <name> [<start>]`.
 */
#define VD_VCS_WRITE_GIT_BRANCH_CREATE 8

/*
 `git checkout <name>`: switch to an existing branch.
 */
#define VD_VCS_WRITE_GIT_BRANCH_SWITCH 9

/*
 `git branch -d <name>` (irreversible; confirm first).
 */
#define VD_VCS_WRITE_GIT_BRANCH_DELETE 10

/*
 `svn commit -m <message> [paths...]`.
 */
#define VD_VCS_WRITE_SVN_COMMIT 11

/*
 `svn update [paths...]`.
 */
#define VD_VCS_WRITE_SVN_UPDATE 12

/*
 `svn revert <paths...>` (irreversible).
 */
#define VD_VCS_WRITE_SVN_REVERT 13

/*
 `svn add <paths...>`.
 */
#define VD_VCS_WRITE_SVN_ADD 14

/*
 `svn remove <paths...>` (irreversible).
 */
#define VD_VCS_WRITE_SVN_REMOVE 15

/*
 `git branch -m <old> <new>`.
 */
#define VD_VCS_WRITE_GIT_BRANCH_RENAME 16

/*
 `git tag <name> [<target>]`.
 */
#define VD_VCS_WRITE_GIT_TAG_CREATE 17

/*
 `git tag -d <name>` (irreversible; confirm first).
 */
#define VD_VCS_WRITE_GIT_TAG_DELETE 18

/*
 `git checkout <ref>`: check out a branch, tag or commit (detached HEAD for
 anything but a branch).
 */
#define VD_VCS_WRITE_GIT_CHECKOUT 19

/*
 `git remote add <name> <url>`.
 */
#define VD_VCS_WRITE_GIT_REMOTE_ADD 20

/*
 `git remote rm <name>` (irreversible; confirm first).
 */
#define VD_VCS_WRITE_GIT_REMOTE_REMOVE 21

/*
 `git remote set-url <name> <url>`.
 */
#define VD_VCS_WRITE_GIT_REMOTE_SET_URL 22

/*
 `git branch --set-upstream-to=<upstream> <branch>`.
 */
#define VD_VCS_WRITE_GIT_SET_UPSTREAM 23

/*
 `git merge --no-edit <target>` (batch 5a).
 */
#define VD_VCS_WRITE_GIT_MERGE 24

/*
 `git rebase <target> [<name>]`.
 */
#define VD_VCS_WRITE_GIT_REBASE 25

/*
 `git cherry-pick <target>`.
 */
#define VD_VCS_WRITE_GIT_CHERRY_PICK 26

/*
 `git revert --no-edit <target>`.
 */
#define VD_VCS_WRITE_GIT_REVERT 27

/*
 `git reset --<mode> <target>` (mode: soft / mixed / hard).
 */
#define VD_VCS_WRITE_GIT_RESET 28

/*
 `git clean -f -- <paths>` (untracked files only).
 */
#define VD_VCS_WRITE_GIT_CLEAN 29

/*
 `git <target> --continue` (target: merge / rebase / cherry-pick / revert).
 */
#define VD_VCS_WRITE_GIT_CONTINUE 30

/*
 `git <target> --abort` (target: merge / rebase / cherry-pick / revert).
 */
#define VD_VCS_WRITE_GIT_ABORT 31

/*
 `git stash save [--] [<message>]` (the message is positional: git 1.8.3.1
 has no `stash save -m` and no `stash push`).
 */
#define VD_VCS_WRITE_GIT_STASH_SAVE 32

/*
 `git stash pop [<stash>]`.
 */
#define VD_VCS_WRITE_GIT_STASH_POP 33

/*
 `git stash apply [<stash>]`.
 */
#define VD_VCS_WRITE_GIT_STASH_APPLY 34

/*
 `git stash drop [<stash>]` (irreversible).
 */
#define VD_VCS_WRITE_GIT_STASH_DROP 35

/*
 `git worktree add <path> [<branch>]` (path in `target`, branch in `name`).
 */
#define VD_VCS_WRITE_GIT_WORKTREE_ADD 36

/*
 `git worktree remove <path>` (irreversible; never `--force`).
 */
#define VD_VCS_WRITE_GIT_WORKTREE_REMOVE 37

/*
 `git submodule init`.
 */
#define VD_VCS_WRITE_GIT_SUBMODULE_INIT 38

/*
 `git submodule update --init`.
 */
#define VD_VCS_WRITE_GIT_SUBMODULE_UPDATE 39

/*
 The C-side opaque handle.

 This type has no variants and no fields on purpose: it exists only as a
 pointer type, and cbindgen renders it as `typedef struct VdService VdService;`.
 The real state is [`ServiceState`]; the handle is a cast of its address.
 */
typedef struct VdService VdService;

/*
 A C array of NUL-terminated strings.
 */
typedef struct VdStringList {
  /*
   Number of entries in `items` (`size_t` on the C side).
   */
  size_t count;
  /*
   Owned array of owned strings; null when `count` is zero.
   */
  char **items;
} VdStringList;

/*
 Inputs of one catalog run (borrowed for the duration of the call).
 */
typedef struct VdCatalogOptions {
  /*
   Explicit slang binary, or null to resolve it.
   */
  const char *slang_binary;
  /*
   Project include dirs (`-I`).
   */
  struct VdStringList include_dirs;
  /*
   Project defines (`-D`).
   */
  struct VdStringList defines;
  /*
   Wall-clock budget for the `slang` child in milliseconds; 0 selects the
   catalog default (120 s). The streamed parse is not covered by it.
   */
  uint64_t timeout_ms;
  /*
   Size threshold for the catalog's AST-JSON warning in bytes; 0 selects
   the default (256 MiB). The document is STREAMED, so a larger dump is
   still catalogued in full: this threshold only adds a size note to
   `VdModuleCatalog.warning`, it never fails the catalog.
   */
  size_t max_json_bytes;
  /*
   Oversize threshold for the file count; 0 selects the default (1000).
   */
  size_t max_files;
  /*
   RM-17: top-level module NAME to elaborate from, or null = Auto.
   */
  const char *top;
} VdCatalogOptions;

/*
 One port of one module definition (ux-batch-2 A1).
 */
typedef struct VdModulePort {
  /*
   Port name.
   */
  char *name;
  /*
   `In`, `Out`, `InOut` or `Ref`.
   */
  char *direction;
  /*
   The declared type verbatim (`logic`, `logic[3:0]`, an interface name, …).
   */
  char *type_name;
  /*
   File of the port declaration.
   */
  char *file;
  /*
   0-based line of the port declaration.
   */
  uint32_t line;
  /*
   0-based column of the port declaration.
   */
  uint32_t column;
} VdModulePort;

/*
 Array of module ports.
 */
typedef struct VdModulePortList {
  /*
   Number of entries.
   */
  size_t count;
  /*
   Owned array of ports.
   */
  struct VdModulePort *items;
} VdModulePortList;

/*
 One child instantiation inside a module definition.
 */
typedef struct VdModuleInstance {
  /*
   Instantiated module name.
   */
  char *module;
  /*
   Hierarchical path of the instance in the elaborated design
   (`top.u_mid`); stable across runs for the same project.
   */
  char *path;
  /*
   File of the instantiation site.
   */
  char *file;
  /*
   0-based line of the instantiation site.
   */
  uint32_t line;
  /*
   0-based column of the instantiation site.
   */
  uint32_t column;
} VdModuleInstance;

/*
 Array of child instantiations.
 */
typedef struct VdModuleInstanceList {
  /*
   Number of entries.
   */
  size_t count;
  /*
   Owned array of instances.
   */
  struct VdModuleInstance *items;
} VdModuleInstanceList;

/*
 One module definition with its ports and child instantiations.
 */
typedef struct VdModule {
  /*
   Module name (the deduplication key).
   */
  char *name;
  /*
   File of the module definition.
   */
  char *file;
  /*
   0-based line of the module definition.
   */
  uint32_t line;
  /*
   0-based column of the module definition.
   */
  uint32_t column;
  /*
   1 when the module is never instantiated anywhere (a design top), else 0.
   */
  uint8_t is_top;
  /*
   Ports in declaration order.
   */
  struct VdModulePortList ports;
  /*
   Child instantiations, one entry per instantiation site.
   */
  struct VdModuleInstanceList children;
} VdModule;

/*
 Array of modules.
 */
typedef struct VdModuleList {
  /*
   Number of entries.
   */
  size_t count;
  /*
   Owned array of modules, sorted by name.
   */
  struct VdModule *items;
} VdModuleList;

/*
 One module catalog (ux-batch-2 A1).
 */
typedef struct VdModuleCatalog {
  /*
   The tool that ran (`slang`).
   */
  char *tool;
  /*
   Wall-clock time of the elaboration run, in milliseconds.
   */
  uint64_t elapsed_ms;
  /*
   Peak RSS of the child, in KiB.
   */
  uint64_t peak_rss_kb;
  /*
   Oversize warning for the UI, or null.
   */
  char *warning;
  /*
   Modules, sorted by name.
   */
  struct VdModuleList modules;
  /*
   RM-17: the effective top the elaboration used, or null = Auto.
   */
  char *top;
  /*
   RM-17: absolute source files reachable from the top (the lint scope and
   the status-dot exclusion set both come from this ONE list).
   */
  struct VdStringList reachable_files;
  /*
   RM-17: module definitions outside the top (empty in Auto), sorted.
   */
  struct VdStringList outside_modules;
} VdModuleCatalog;

/*
 Inputs of one project-wide lint pass (borrowed for the duration of the call).
 */
typedef struct VdLintBatchOptions {
  /*
   0 slang, 1 verible-verilog-lint.
   */
  int32_t engine;
  /*
   Explicit slang binary, or null to resolve it.
   */
  const char *slang_binary;
  /*
   Explicit verible-verilog-lint binary, or null to resolve it.
   */
  const char *verible_binary;
  /*
   Verible lint rules file (`--rules_config=`), or null.
   */
  const char *rules_path;
  /*
   Project include dirs (`-I`; slang only).
   */
  struct VdStringList include_dirs;
  /*
   Project defines (`-D`; slang only).
   */
  struct VdStringList defines;
  /*
   Per-file wall-clock budget in milliseconds; 0 selects the default.
   */
  uint64_t timeout_ms;
  /*
   Maximum diagnostics kept per file; 0 selects the default.
   */
  size_t max_diagnostics;
  /*
   Verible ruleset: 0 = default (NO flag), 1 = `--ruleset=all`,
   2 = `--ruleset=none`; any other value is rejected. Verible-only knob —
   documented no-op for slang (no argv).
   */
  uint8_t ruleset;
  /*
   Verible `--rules=` list (`+rule,-rule`, comma separated), or null/empty
   for no flag. Accepted characters: rule names `[A-Za-z0-9_-]` optionally
   prefixed by `+`/`-`, comma separated. Verible-only knob.
   */
  const char *rules;
  /*
   Verible `--waiver_files=` (comma-separated paths), or null/empty for no
   flag. Entries must be non-empty and free of control characters; a comma
   cannot appear inside a path (it is the separator). Verible-only knob.
   */
  const char *waiver_files;
  /*
   Extra slang `-W…` tokens (whitespace/comma separated), or null/empty for
   none. Each token must match `-W[A-Za-z0-9_=-]+` and becomes its OWN argv
   entry. Slang-only knob — documented no-op for verible.
   */
  const char *slang_suppressions;
  /*
   Slang `--error-limit=N`; 0 sends no flag. Slang-only knob.
   */
  uint32_t error_limit;
} VdLintBatchOptions;

/*
 One lint diagnostic.
 */
typedef struct VdDiagnostic {
  /*
   0 error, 1 warning, 2 note, 3 ignored, 4 unknown.
   */
  int32_t severity;
  /*
   0-based start line.
   */
  uint32_t line;
  /*
   0-based start column (UTF-16 code units).
   */
  uint32_t column;
  /*
   0-based end line.
   */
  uint32_t end_line;
  /*
   0-based end column (UTF-16 code units).
   */
  uint32_t end_column;
  /*
   Diagnostic code (`UndeclaredIdentifier`, or the verible rule name).
   */
  char *code;
  /*
   Human-readable message.
   */
  char *message;
  /*
   The file the diagnostic belongs to (batch runs); null on the single-file
   call, where the caller already knows it.
   */
  char *file;
  /*
   The cross-engine dedup key (AC-2.13): file path, 0-based start line,
   severity class and normalised message separated by `0x1F`. Two engines
   reporting the same problem produce the same key.
   */
  char *key;
  /*
   0 unknown, 1 LSP, 2 CLI, 3 both (the source column, AC-2.13).
   */
  int32_t origin;
} VdDiagnostic;

/*
 All diagnostics of one deep-lint run.
 */
typedef struct VdDiagnostics {
  /*
   Number of entries in `items`.
   */
  size_t count;
  /*
   Owned array of diagnostics.
   */
  struct VdDiagnostic *items;
} VdDiagnostics;

/*
 Progress of one project-wide lint pass (AC-2.7/2.8/2.9).
 */
typedef struct VdLintProgress {
  /*
   0 queued, 1 running, 2 finished, 3 cancelled, 4 failed.
   */
  int32_t phase;
  /*
   Files already linted.
   */
  size_t done;
  /*
   Files in the pass's snapshot.
   */
  size_t total;
  /*
   The file being linted right now, or null.
   */
  char *current;
  /*
   The tool that ran (`slang` / `verible-verilog-lint`).
   */
  char *tool;
  /*
   The failure stage (`resolve`, `spawn`, `run`, `parse`), or null.
   */
  char *stage;
  /*
   One-line state for the status bar.
   */
  char *message;
} VdLintProgress;

/*
 Inputs of one elaboration run (borrowed for the duration of the call).
 */
typedef struct VdHierOptions {
  /*
   Explicit slang binary, or null to resolve it.
   */
  const char *slang_binary;
  /*
   Top module, or null to let slang pick the roots.
   */
  const char *top;
  /*
   Project include dirs (`-I`).
   */
  struct VdStringList include_dirs;
  /*
   Project defines (`-D`).
   */
  struct VdStringList defines;
  /*
   Wall-clock budget in milliseconds; 0 selects the default (30 s).
   */
  uint64_t timeout_ms;
  /*
   Oversize threshold for the instance count; 0 selects the default (10000).
   */
  size_t max_instances;
  /*
   Oversize threshold for the file count; 0 selects the default (1000).
   */
  size_t max_files;
} VdHierOptions;

/*
 One elaborated instance (AC-3.1's data side).
 */
typedef struct VdHierInstance {
  /*
   Hierarchical path (`top.u0`).
   */
  char *path;
  /*
   Module name (`mid`).
   */
  char *module;
  /*
   File of the instantiation site.
   */
  char *file;
  /*
   0-based line of the instantiation site.
   */
  uint32_t line;
  /*
   0-based column of the instantiation site.
   */
  uint32_t column;
} VdHierInstance;

/*
 Array of instances.
 */
typedef struct VdHierInstanceList {
  /*
   Number of entries.
   */
  size_t count;
  /*
   Owned array of instances.
   */
  struct VdHierInstance *items;
} VdHierInstanceList;

/*
 One driver or load of a net.
 */
typedef struct VdHierEndpoint {
  /*
   Hierarchical path of the driving/loading symbol.
   */
  char *path;
  /*
   File of the position.
   */
  char *file;
  /*
   0-based line.
   */
  uint32_t line;
  /*
   0-based column.
   */
  uint32_t column;
  /*
   0 instance-output-port, 1 instance-input-port, 2 continuous-assign,
   3 procedural.
   */
  int32_t kind;
  /*
   Extra label (`AlwaysFF`, `assign`, `port q (out)`).
   */
  char *detail;
} VdHierEndpoint;

/*
 Array of endpoints.
 */
typedef struct VdHierEndpointList {
  /*
   Number of entries.
   */
  size_t count;
  /*
   Owned array of endpoints.
   */
  struct VdHierEndpoint *items;
} VdHierEndpointList;

/*
 One net with its drivers and loads (AC-3.2's data side).
 */
typedef struct VdHierSignal {
  /*
   Hierarchical path (`top.link`).
   */
  char *path;
  /*
   `Net`, `Variable` or `Port`.
   */
  char *kind;
  /*
   1 when a declaration position was reported.
   */
  int32_t has_declaration;
  /*
   Declaration file, or null.
   */
  char *declaration_file;
  /*
   0-based declaration line.
   */
  uint32_t declaration_line;
  /*
   0-based declaration column.
   */
  uint32_t declaration_column;
  /*
   Constructs that drive the net.
   */
  struct VdHierEndpointList drivers;
  /*
   Constructs that read the net.
   */
  struct VdHierEndpointList loads;
  /*
   Driver chain that continues inside a child instance.
   */
  struct VdHierEndpointList chain;
} VdHierSignal;

/*
 Array of nets.
 */
typedef struct VdHierSignalList {
  /*
   Number of entries.
   */
  size_t count;
  /*
   Owned array of nets.
   */
  struct VdHierSignal *items;
} VdHierSignalList;

/*
 One elaboration result.
 */
typedef struct VdHierarchy {
  /*
   The tool that ran (`slang`).
   */
  char *tool;
  /*
   The requested top, or null when slang picked the roots.
   */
  char *top;
  /*
   Wall-clock time of the run, in milliseconds.
   */
  uint64_t elapsed_ms;
  /*
   Peak RSS of the child, in KiB (sampled from `/proc/<pid>/status`).
   */
  uint64_t peak_rss_kb;
  /*
   Oversize warning for the UI (AC-3.5), or null.
   */
  char *warning;
  /*
   Elaborated instances.
   */
  struct VdHierInstanceList instances;
  /*
   Nets of every instance. Named `signal_list`, not `signals`: `signals` is
   a Qt macro (`#define signals public`), and this header is included by Qt
   translation units.
   */
  struct VdHierSignalList signal_list;
} VdHierarchy;

/*
 Result of one FIM completion request.
 */
typedef struct VdFimResult {
  /*
   0 disabled/not configured, 1 text returned, 2 error.
   */
  int32_t state;
  /*
   Completion text, or the reason no request was made.
   */
  char *text;
} VdFimResult;

/*
 State of one Verible LSP session.
 */
typedef struct VdLspInfo {
  /*
   0 stopped, 1 configured (lazy, no process yet), 2 error.
   */
  int32_t state;
  /*
   The server binary the client would start.
   */
  char *server;
  /*
   Human-readable status line.
   */
  char *message;
} VdLspInfo;

/*
 Observable state of one session (A3d).
 */
typedef struct VdLspStatus {
  /*
   0 starting, 1 ready, 2 failed, 3 stopped.
   */
  int32_t phase;
  /*
   Files the caller handed in (the project model's effective set).
   */
  size_t files_expected;
  /*
   Files actually opened on the server.
   */
  size_t files_opened;
  /*
   Diagnostics currently recorded across the open documents.
   */
  size_t diagnostics;
  /*
   Diagnostics generation counter.
   */
  uint64_t generation;
  /*
   Server PID, or 0 when no process is running.
   */
  uint32_t server_pid;
  /*
   How many times a crashed server was restarted.
   */
  uint32_t restarts;
  /*
   Human-readable state line.
   */
  char *message;
  /*
   `path: reason` for files that could not be opened.
   */
  struct VdStringList skipped;
  /*
   The backend this session drives: 0 verible, 1 slang (T138).
   */
  int32_t backend;
  /*
   The backend name for the status line (`backend=slang`).
   */
  char *backend_name;
} VdLspStatus;

/*
 Zero-based start/end position of a location, symbol or edit.
 */
typedef struct VdLspPositionRange {
  /*
   Start line (zero-based).
   */
  uint32_t start_line;
  /*
   Start UTF-16 character (zero-based).
   */
  uint32_t start_character;
  /*
   End line (zero-based).
   */
  uint32_t end_line;
  /*
   End UTF-16 character (zero-based).
   */
  uint32_t end_character;
} VdLspPositionRange;

/*
 One location returned by definition/references.
 */
typedef struct VdLspLocation {
  /*
   Target document URI (`file://…`).
   */
  char *uri;
  /*
   Range inside the target document.
   */
  struct VdLspPositionRange range;
} VdLspLocation;

/*
 Array of locations.
 */
typedef struct VdLspLocationList {
  /*
   Number of entries.
   */
  size_t count;
  /*
   Owned array of locations; null when `count` is zero.
   */
  struct VdLspLocation *items;
} VdLspLocationList;

/*
 One outline entry.
 */
typedef struct VdLspSymbol {
  /*
   Symbol name.
   */
  char *name;
  /*
   Detail text, or null.
   */
  char *detail;
  /*
   LSP `SymbolKind` number.
   */
  int32_t kind;
  /*
   Selection range.
   */
  struct VdLspPositionRange range;
} VdLspSymbol;

/*
 Array of outline entries.
 */
typedef struct VdLspSymbolList {
  /*
   Number of entries.
   */
  size_t count;
  /*
   Owned array of symbols; null when `count` is zero.
   */
  struct VdLspSymbol *items;
} VdLspSymbolList;

/*
 One text edit to apply.
 */
typedef struct VdLspTextEdit {
  /*
   Target document URI.
   */
  char *uri;
  /*
   Range replaced by `new_text`.
   */
  struct VdLspPositionRange range;
  /*
   Replacement text.
   */
  char *new_text;
} VdLspTextEdit;

/*
 Array of text edits.
 */
typedef struct VdLspTextEditList {
  /*
   Number of entries.
   */
  size_t count;
  /*
   Owned array of edits; null when `count` is zero.
   */
  struct VdLspTextEdit *items;
} VdLspTextEditList;

/*
 One diagnostic.
 */
typedef struct VdLspDiagnostic {
  /*
   1 error, 2 warning, 3 information, 4 hint, 0 unknown.
   */
  int32_t severity;
  /*
   Diagnostic range.
   */
  struct VdLspPositionRange range;
  /*
   Rule/code, or null.
   */
  char *code;
  /*
   Producing tool, or null.
   */
  char *source;
  /*
   Message text.
   */
  char *message;
  /*
   1 LSP, 2 CLI, 3 both (the source column, AC-2.13). LSP rows are always 1.
   */
  int32_t origin;
} VdLspDiagnostic;

/*
 Array of diagnostics.
 */
typedef struct VdLspDiagnosticList {
  /*
   Number of entries.
   */
  size_t count;
  /*
   Owned array of diagnostics; null when `count` is zero.
   */
  struct VdLspDiagnostic *items;
} VdLspDiagnosticList;

/*
 One call signature of a `SignatureHelp` answer (T16).
 */
typedef struct VdLspSignature {
  /*
   The signature as the server sent it (`foo(int a, int b)`).
   */
  char *label;
  /*
   Parameter labels, in order.
   */
  struct VdStringList parameters;
} VdLspSignature;

/*
 Array of call signatures.
 */
typedef struct VdLspSignatureList {
  /*
   Number of entries.
   */
  size_t count;
  /*
   Owned array of signatures; null when `count` is zero.
   */
  struct VdLspSignature *items;
} VdLspSignatureList;

/*
 One polled response. Only the list matching `kind` is filled.
 */
typedef struct VdLspResponse {
  /*
   1 locations, 2 symbols, 3 edits, 4 diagnostics.
   */
  int32_t kind;
  /*
   Human-readable note (may be null).
   */
  char *message;
  /*
   Locations, when `kind` is 1.
   */
  struct VdLspLocationList locations;
  /*
   Symbols, when `kind` is 2.
   */
  struct VdLspSymbolList symbols;
  /*
   Edits, when `kind` is 3.
   */
  struct VdLspTextEditList edits;
  /*
   Diagnostics, when `kind` is 4.
   */
  struct VdLspDiagnosticList diagnostics;
  /*
   Hover text, when `kind` is 5 (may be null when the server sent nothing).
   */
  char *text;
  /*
   Hover range, when `kind` is 5 and the server supplied one.
   */
  struct VdLspPositionRange range;
  /*
   Signatures, when `kind` is 6.
   */
  struct VdLspSignatureList signatures;
  /*
   Active signature index, when `kind` is 6 (`-1` = none).
   */
  int32_t active_signature;
  /*
   Active parameter index, when `kind` is 6 (`-1` = none).
   */
  int32_t active_parameter;
} VdLspResponse;

/*
 One row of a language server's capability table (T138).
 */
typedef struct VdLspCapability {
  /*
   Action name (`definition`, `references`, `documentSymbol`, …).
   */
  char *action;
  /*
   1 supported, 0 unsupported, -1 unknown (not measured in this environment).
   */
  int32_t supported;
  /*
   Why; never null (an unsupported action always says why).
   */
  char *reason;
} VdLspCapability;

/*
 A backend's capability table, with how it was established.
 */
typedef struct VdLspCapabilities {
  /*
   Backend name (`verible` / `slang`).
   */
  char *backend;
  /*
   How the table was established: `measured`, `live` or `declared`.
   */
  char *basis;
  /*
   Where the rows come from (binary version / live handshake).
   */
  char *source;
  /*
   What this backend offers that the other does not (the UI-gating
   asymmetry, one sentence; never null).
   */
  char *note;
  /*
   Number of rows.
   */
  size_t count;
  /*
   Owned array of rows.
   */
  struct VdLspCapability *items;
} VdLspCapabilities;

/*
 One project member list: files, include dirs, defines and warnings.
 */
typedef struct VdProjectInfo {
  /*
   Expanded `.f` file list, in filelist order, duplicates removed.
   */
  struct VdStringList files;
  /*
   `+incdir+` directories after expansion.
   */
  struct VdStringList include_dirs;
  /*
   `+define+` entries as `NAME` or `NAME=VALUE`.
   */
  struct VdStringList defines;
  /*
   Non-fatal diagnostics collected while resolving the project.
   */
  struct VdStringList warnings;
  /*
   The SAME file set relative to the project root (absolute only for a file
   outside it), same order as `files`.

   Recommended spelling for a generated `.f`: portable across a moved
   checkout. It is NOT the determining rule for correct jump URIs — the
   controlled 4-way spike (2026-11-02) showed the server's cwd must be the
   project root; with that cwd, absolute entries resolve correctly too, and
   with the wrong cwd even relative entries land on a sibling path. The root
   this projection is based on is the single source of truth for cwd and
   `rootUri` (never re-derive it in the launcher).
   */
  struct VdStringList relative_files;
  /*
   Filelist variable names still unresolved after the project variable
   table + the process environment (ADDENDUM A-9), unique and sorted. The
   UI asks for all of them in ONE dialog; every affected line also appears
   in `warnings` with its `.f` file and 1-based line.
   */
  struct VdStringList missing_variables;
  /*
   Lines that produced no entry (unresolved/invalid variable, unknown
   token, malformed define, missing/cyclic nested `-f`). A non-zero value
   must be shown to the user: a dropped line is a file the pass will not
   lint.
   */
  uint32_t skipped_lines;
  /*
   Absolute path of the `.rules.verible_lint` the project loader DISCOVERED
   (the nearest one, searched upward, T114 semantics unchanged), or null
   when none was found. This is what the Lint settings page and the lint
   pass consume; it is not a user-typed path.
   */
  char *rules_path;
} VdProjectInfo;

/*
 Options of one project (tree) search (ux-batch-2 A1).

 Passed to [`crate::search::vd_search_tree_query_ex`]; every field is a plain
 value with a documented default, so a zeroed struct is the old behaviour.
 */
typedef struct VdSearchOptions {
  /*
   Case-sensitive matching (0 = insensitive, the default).
   */
  uint8_t case_sensitive;
  /*
   Whole-word matching (0 = substring, the default).
   */
  uint8_t whole_word;
  /*
   Treat `needle` as a regular expression (0 = literal, the default). The
   engine is RE2-style: no backreferences, no lookaround.
   */
  uint8_t regex;
  /*
   Report every occurrence on a line as its own hit (0 = first only).
   */
  uint8_t all_matches;
  /*
   Reserved; must be zero.
   */
  uint8_t _reserved[4];
  /*
   File extensions to include (e.g. "v", ".svh"); empty/null = no extension
   filter. Blank entries are ignored.
   */
  struct VdStringList extensions;
  /*
   Result cap; 0 selects the default (2000).
   */
  size_t max_results;
  /*
   Files larger than this are skipped; 0 selects the default (8 MiB).
   */
  uint64_t max_file_bytes;
  /*
   Wall-clock budget in milliseconds; 0 selects the default (15 s).
   */
  uint64_t max_duration_ms;
} VdSearchOptions;

/*
 One global-search hit.
 */
typedef struct VdSearchHit {
  /*
   File path as supplied to the index.
   */
  char *path;
  /*
   1-based line number.
   */
  uint32_t line;
  /*
   1-based character column of the match start (character, not byte: this
   is the editor's position unit).
   */
  uint32_t column;
  /*
   Trimmed, truncated matched line.
   */
  char *preview;
  /*
   1-based character column immediately AFTER the match (exclusive end), so
   `end_column - column == match_length`. This is the range the caller
   replaces; `0` means the query reports no range (never for a tree hit).
   */
  uint32_t end_column;
  /*
   Match length in characters.
   */
  uint32_t match_length;
} VdSearchHit;

/*
 All hits of one search query.
 */
typedef struct VdSearchResults {
  /*
   Number of entries in `items`.
   */
  size_t count;
  /*
   Owned array of hits.
   */
  struct VdSearchHit *items;
} VdSearchResults;

/*
 Observable summary of one tree search (T21): counters, limits and time.

 `truncated`/`timed_out` are `u8` so the C layout is explicit (0/1).
 */
typedef struct VdSearchSummary {
  /*
   Files whose content was read.
   */
  uint32_t files_scanned;
  /*
   Files that produced at least one hit.
   */
  uint32_t files_matched;
  /*
   Recorded hits.
   */
  uint32_t hit_count;
  /*
   Files skipped as binary (NUL byte).
   */
  uint32_t skipped_binary;
  /*
   Files skipped as too large.
   */
  uint32_t skipped_too_large;
  /*
   Files/directories skipped as unreadable.
   */
  uint32_t skipped_unreadable;
  /*
   1 when `max_results` was reached, else 0.
   */
  uint8_t truncated;
  /*
   1 when `max_duration_ms` was reached, else 0.
   */
  uint8_t timed_out;
  /*
   Wall-clock duration of the scan.
   */
  uint64_t elapsed_ms;
} VdSearchSummary;

/*
 Error details of the most recent rejected call or failed job.
 */
typedef struct VdError {
  /*
   One of the `VD_ERR_*` codes.
   */
  int32_t code;
  /*
   NUL-terminated, human-readable reason.
   */
  char message[VD_ERROR_MESSAGE_CAPACITY];
} VdError;

/*
 Input pair for `vd_settings_save` (borrowed for the duration of the call).
 */
typedef struct VdSettingInput {
  /*
   Flat key.
   */
  const char *key;
  /*
   String value.
   */
  const char *value;
} VdSettingInput;

/*
 One stored setting.
 */
typedef struct VdSetting {
  /*
   Flat key.
   */
  char *key;
  /*
   String value.
   */
  char *value;
} VdSetting;

/*
 All settings read from the store, plus warnings.
 */
typedef struct VdSettings {
  /*
   Number of entries in `items`.
   */
  size_t count;
  /*
   Owned array of settings.
   */
  struct VdSetting *items;
  /*
   Non-fatal problems (invalid TOML, non-string values).
   */
  struct VdStringList warnings;
} VdSettings;

/*
 One changed path reported by the VCS backend.
 */
typedef struct VdVcsChange {
  /*
   Path relative to the working tree root.
   */
  char *path;
  /*
   1 added, 2 modified, 3 deleted, 4 renamed, 5 conflict, 6 unknown.
   */
  int32_t kind;
} VdVcsChange;

/*
 The VCS status of one working tree.
 */
typedef struct VdVcsStatus {
  /*
   0 none, 1 git, 2 svn.
   */
  int32_t backend;
  /*
   Current branch when the backend reports one; may be null.
   */
  char *branch;
  /*
   Number of entries in `changes`.
   */
  size_t count;
  /*
   Owned array of changes.
   */
  struct VdVcsChange *changes;
  /*
   Why the backend could not be queried; null on success.
   */
  char *note;
} VdVcsStatus;

/*
 One branch / tag / remote-tracking ref (owned strings).
 */
typedef struct VdVcsRef {
  /*
   Short name: a branch (`main`), a tag (`v1.0`) or a remote-tracking ref
   (`origin/main`); NULL only when the listing was truncated mid-entry.
   */
  char *name;
  /*
   1 branch, 2 tag, 3 remote-tracking, 4 other.
   */
  int32_t kind;
  /*
   Abbreviated object id (7 characters), or NULL.
   */
  char *target;
  /*
   1 when this is the checked-out branch.
   */
  int32_t current;
} VdVcsRef;

/*
 One configured remote (owned strings).
 */
typedef struct VdVcsRemote {
  /*
   Remote name (`origin`), or NULL.
   */
  char *name;
  /*
   Fetch URL as configured, or NULL.
   */
  char *url;
} VdVcsRemote;

/*
 Branch + revision of the working tree containing `root` (owned strings).

 Batch 4a: the same job also carries the ref listing, the configured remotes
 and the current branch's upstream, so one asynchronous read refreshes the
 whole refs column.
 */
typedef struct VdVcsInfo {
  /*
   Backend: 0 none, 1 git, 2 svn.
   */
  int32_t backend;
  /*
   Branch name, or NULL.
   */
  char *branch;
  /*
   Short revision, or NULL.
   */
  char *revision;
  /*
   Why the identity is unavailable, or NULL.
   */
  char *note;
  /*
   Number of entries in `refs`.
   */
  size_t ref_count;
  /*
   Branches / tags / remote-tracking refs, or NULL when `ref_count` is 0.
   */
  struct VdVcsRef *refs;
  /*
   Number of entries in `remotes`.
   */
  size_t remote_count;
  /*
   Configured remotes, or NULL when `remote_count` is 0.
   */
  struct VdVcsRemote *remotes;
  /*
   Upstream of the current branch (`origin/main`), or NULL.
   */
  char *upstream;
  /*
   The multi-commit operation in progress: 0 none, 1 merge, 2 rebase,
   3 cherry-pick, 4 revert (batch 5a). The unmerged files are the status
   changes with kind 5, so the conflict list needs no separate read.
   */
  int32_t operation;
} VdVcsInfo;

/*
 One file's unified diff (owned strings).
 */
typedef struct VdVcsDiff {
  /*
   Path asked for, or NULL.
   */
  char *path;
  /*
   Unified diff text, or NULL.
   */
  char *text;
  /*
   1 when the tool reported binary content.
   */
  int32_t binary;
  /*
   Why the diff is empty, or NULL.
   */
  char *note;
} VdVcsDiff;

/*
 One commit row of the history reader (owned strings).
 */
typedef struct VdVcsHistoryEntry {
  /*
   Full object id, or NULL.
   */
  char *hash;
  /*
   Abbreviated object id, or NULL.
   */
  char *short_hash;
  /*
   Author name, or NULL.
   */
  char *author;
  /*
   Author email, or NULL.
   */
  char *email;
  /*
   Author time, unix seconds.
   */
  int64_t time;
  /*
   First line of the commit message, or NULL.
   */
  char *subject;
} VdVcsHistoryEntry;

/*
 One file of a commit / comparison (owned string).
 */
typedef struct VdVcsFileStat {
  /*
   Repository-relative path, or NULL.
   */
  char *path;
  /*
   Added lines (`-1` when git reported the file as binary).
   */
  int64_t additions;
  /*
   Deleted lines (`-1` when binary).
   */
  int64_t deletions;
} VdVcsFileStat;

/*
 One blamed line (owned strings).
 */
typedef struct VdVcsBlameLine {
  /*
   1-based line number.
   */
  uint64_t line;
  /*
   Full object id of the commit that introduced the line, or NULL.
   */
  char *hash;
  /*
   Abbreviated object id, or NULL.
   */
  char *short_hash;
  /*
   Author name, or NULL.
   */
  char *author;
  /*
   Author time, unix seconds.
   */
  int64_t time;
  /*
   The line's text without the trailing newline, or NULL.
   */
  char *text;
} VdVcsBlameLine;

/*
 One history read (batch 4b, AC-4.3), owned by the caller until freed.

 `kind` says which arrays/strings are meaningful: `1` commit page
 (`commits` with `offset` and `has_more`), `2` commit detail (`hash`, `author`,
 `email`, `time`, `subject`, `body`, `files`, `patch`), `3` file history
 (`path` with `commits`), `4` blame (`path` with `blame`), `5` comparison
 (`from`, `to`, `files`, `patch`). `error` distinguishes a failed read from an
 empty repository or an empty result, which carries `note` instead.
 */
typedef struct VdVcsHistoryResult {
  /*
   Backend: 0 none, 1 git, 2 svn.
   */
  int32_t backend;
  /*
   1 commit page, 2 commit detail, 3 file history, 4 blame, 5 comparison.
   */
  int32_t kind;
  /*
   Number of entries in `commits`.
   */
  size_t commit_count;
  /*
   Commit rows, or NULL when `commit_count` is 0.
   */
  struct VdVcsHistoryEntry *commits;
  /*
   Number of entries in `files`.
   */
  size_t file_count;
  /*
   File stats, or NULL when `file_count` is 0.
   */
  struct VdVcsFileStat *files;
  /*
   Number of entries in `blame`.
   */
  size_t blame_count;
  /*
   Blame lines, or NULL when `blame_count` is 0.
   */
  struct VdVcsBlameLine *blame;
  /*
   Requested page offset.
   */
  uint64_t offset;
  /*
   1 when a further page exists.
   */
  int32_t has_more;
  /*
   1 when the listing was cut at its cap.
   */
  int32_t truncated;
  /*
   1 when the read failed (as opposed to an empty repository / no result).
   */
  int32_t error;
  /*
   Commit hash (detail, or the commit a blame row belongs to), or NULL.
   */
  char *hash;
  /*
   Author name (detail), or NULL.
   */
  char *author;
  /*
   Author email (detail), or NULL.
   */
  char *email;
  /*
   Commit subject (detail), or NULL.
   */
  char *subject;
  /*
   Commit body without the subject (detail), or NULL.
   */
  char *body;
  /*
   Unified patch (detail / comparison), or NULL.
   */
  char *patch;
  /*
   Repository-relative path (file history / blame), or NULL.
   */
  char *path;
  /*
   Comparison "from" side as requested, or NULL.
   */
  char *from;
  /*
   Comparison "to" side as requested, or NULL.
   */
  char *to;
  /*
   Author time, unix seconds (detail).
   */
  int64_t time;
  /*
   Why the read is empty or failed, or NULL.
   */
  char *note;
} VdVcsHistoryResult;

/*
 One stash entry or one linked worktree (batch 5b, AC-5.2), owned by the job
 until it is converted to C. A stash fills `ref_name` / `hash` / `subject`; a
 worktree fills `path` / `hash` / `branch` and the flags.
 */
typedef struct VdVcsWorkspaceEntry {
  /*
   Stash reference (`stash@{0}`), or NULL for a worktree.
   */
  char *ref_name;
  /*
   Commit id the entry points at (abbreviated for a worktree), or NULL.
   */
  char *hash;
  /*
   Stash subject, or NULL for a worktree.
   */
  char *subject;
  /*
   Absolute worktree path, or NULL for a stash.
   */
  char *path;
  /*
   Checked-out branch of a worktree (empty when detached/bare), or NULL.
   */
  char *branch;
  /*
   1 for the MAIN worktree (the first row `git worktree list` prints).
   */
  int32_t main_worktree;
  /*
   1 when the worktree has a detached HEAD.
   */
  int32_t detached;
  /*
   1 when the entry is a bare repository.
   */
  int32_t bare;
} VdVcsWorkspaceEntry;

/*
 One workspace listing (batch 5b, AC-5.2), owned by the caller until freed.

 `error` distinguishes a FAILED read from an empty list: the delivery baseline
 (git 1.8.3.1) has no `git worktree` command, so its worktree listing carries
 `error = 1` and git's real message in `note` instead of an invented empty list.
 */
typedef struct VdVcsWorkspaceResult {
  /*
   Backend: 0 none, 1 git, 2 svn.
   */
  int32_t backend;
  /*
   1 stashes, 2 worktrees.
   */
  int32_t kind;
  /*
   Number of entries in `entries`.
   */
  size_t count;
  /*
   The rows, or NULL when `count` is 0.
   */
  struct VdVcsWorkspaceEntry *entries;
  /*
   1 when the listing was cut at its cap.
   */
  int32_t truncated;
  /*
   1 when the read failed (as opposed to an empty list).
   */
  int32_t error;
  /*
   Why the list is empty or failed (the tool's real reason), or NULL.
   */
  char *note;
} VdVcsWorkspaceResult;

/*
 One write request (every pointer is borrowed only for the duration of
 [`vd_vcs_write_job`]; the job owns its own copies).
 */
typedef struct VdVcsWriteRequest {
  /*
   One of the `VD_VCS_WRITE_*` operation codes; anything else is refused.
   */
  int32_t op;
  /*
   Working tree root (the panel's project root; the repository root is found
   by walking up). Must be a valid UTF-8 path.
   */
  const char *root;
  /*
   Commit message (`GitCommit` / `SvnCommit` only), or NULL.
   */
  const char *message;
  /*
   Author override (`GitCommit` only), or NULL.
   */
  const char *author;
  /*
   Non-zero to amend the previous commit (`GitCommit` only).
   */
  int32_t amend;
  /*
   Number of entries in `paths`.
   */
  size_t count;
  /*
   Repository-relative paths, or NULL when `count` is 0.
   */
  const char *const *paths;
  /*
   Branch / tag / remote name (create, switch, delete, rename source,
   remote add/rm/set-url, upstream local branch; for push/pull it is the
   branch), or NULL.
   */
  const char *name;
  /*
   Start point (branch/tag create), new name (rename), ref to check out, the
   remote (fetch/pull/push), or the upstream ref (set-upstream), or NULL.
   */
  const char *target;
  /*
   Remote URL (`remote add` / `remote set-url` only), or NULL.
   */
  const char *url;
  /*
   Remote username (fetch/pull/push only), or NULL. Held in memory only.
   */
  const char *credential_user;
  /*
   Remote password (fetch/pull/push only), or NULL. Held in memory only: it
   never enters the tool argv, is never written to disk, and is redacted out
   of the captured output.
   */
  const char *credential_password;
  /*
   Non-zero to push with `--set-upstream` (`GitPush` only).
   */
  int32_t set_upstream;
  /*
   Reset mode (`GitReset` only): `soft`, `mixed` or `hard`, or NULL.
   */
  const char *mode;
} VdVcsWriteRequest;

/*
 Result of one write job.
 */
typedef struct VdVcsWriteResult {
  /*
   The operation that ran (one of the `VD_VCS_WRITE_*` codes).
   */
  int32_t op;
  /*
   1 when the tool exited zero, 0 when it reported a failure.
   */
  int32_t ok;
  /*
   Captured stdout (may be empty), or NULL.
   */
  char *stdout_text;
  /*
   Captured stderr (the real reason when `ok` is 0), or NULL.
   */
  char *stderr_text;
  /*
   Why the request produced no tool run, or NULL.
   */
  char *note;
} VdVcsWriteResult;

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/*
 The cross-engine dedup key of one diagnostic (AC-2.13).

 `origin` selects how `severity` is read: `1` = LSP (1 error … 4 hint), `2` =
 CLI (0 error … 4 unknown). The key is `file`, 0-based `line`, the shared
 severity class and the normalised message separated by `0x1F`, so an LSP row
 and a CLI row of the same problem produce the same string. Returns an owned
 string (release with [`vd_string_free`]) or null for invalid arguments.

 # Safety

 `file` and `message` must be null or valid NUL-terminated strings.
 */
char *vd_diagnostic_key(int32_t origin,
                        const char *file,
                        uint32_t line,
                        int32_t severity,
                        const char *message);

/*
 Release an owned string returned by [`vd_diagnostic_key`],
 `vd_lint_command_line` or `vd_lsp_command_line`; null-safe.

 # Safety

 `text` must be null or a pointer this library returned and not freed yet.
 */
void vd_string_free(char *text);

/*
 Start a project-wide module catalog (ux-batch-2 A1).

 `files` is the project's effective file set and `options` carries the `-I`/
 `-D` settings. The work runs on a worker thread, so the UI thread never
 waits; poll with [`vd_catalog_poll`]. Returns a job id (`>= 1 << 52`) or `0`
 when the arguments were rejected.

 # Safety

 `service` must be null or live; `files` must be null or point to a
 [`VdStringList`] of valid NUL-terminated strings; `options` must be null or
 point to a readable [`VdCatalogOptions`] whose lists satisfy that contract.
 */
uint64_t vd_catalog_start(struct VdService *service,
                          const struct VdStringList *files,
                          const struct VdCatalogOptions *options);

/*
 Poll a catalog job.

 `VD_STATUS_PENDING` while it runs; `VD_STATUS_READY` with `out` filled once it
 finished; `VD_STATUS_FAILED` when the catalog itself failed (the reason,
 naming the tool and the stage, is readable with `vd_service_error`). The
 result is delivered once.

 # Safety

 `service` must be null or live; `out` must be null or writable for one
 [`VdModuleCatalog`].
 */
int32_t vd_catalog_poll(struct VdService *service, uint64_t job, struct VdModuleCatalog *out);

/*
 Release a catalog job without delivering its result (null-safe for an unknown
 id). A running job finishes into memory nobody reads.

 # Safety

 `service` must be null or live.
 */
int32_t vd_catalog_release(struct VdService *service, uint64_t job);

/*
 Release a filled [`VdModuleCatalog`]; null-safe and idempotent.

 # Safety

 `catalog` must be null or point to a struct filled by [`vd_catalog_poll`] and
 not freed yet.
 */
void vd_module_catalog_free(struct VdModuleCatalog *catalog);

/*
 Start a SLANG lint job for one file (the original entry point).

 `binary_override` may be null (the service then resolves the binary itself).
 Returns the job id, or `0` when the arguments were rejected.

 # Safety

 `service` must be null or live; the string arguments must be null or valid
 NUL-terminated strings.
 */
uint64_t vd_deeplint_lint(struct VdService *service, const char *file, const char *binary_override);

/*
 Start one lint job with an explicit engine and the project settings (T138).

 `engine` is `0` (slang) or `1` (verible-verilog-lint). `slang_binary`,
 `verible_binary` and `rules_path` may be null. `include_dirs` / `defines` may
 be null; the slang engine receives them as `-I` / `-D`, the verible engine
 does not accept them (see `vd_deeplint::knobs_note`). Returns the job id, or
 `0` when the arguments were rejected.

 # Safety

 `service` must be null or live; every `*const c_char` must be null or a valid
 NUL-terminated string; the lists must satisfy the string-list contract.
 */
uint64_t vd_deeplint_lint_v2(struct VdService *service,
                             const char *file,
                             int32_t engine,
                             const char *slang_binary,
                             const char *verible_binary,
                             const char *rules_path,
                             const struct VdStringList *include_dirs,
                             const struct VdStringList *defines);

/*
 The exact argv one lint run would use, as one owned string (AC-2.5's dump).

 Spawns nothing. `file` may be null (the dump then shows `<file>`). Returns
 null only when `engine` is invalid. Release the result with `vd_string_free`.

 # Safety

 Every pointer argument must be null or valid for the duration of the call.
 */
char *vd_lint_command_line(int32_t engine,
                           const char *binary,
                           const char *file,
                           const char *rules_path,
                           const struct VdStringList *include_dirs,
                           const struct VdStringList *defines);

/*
 The exact argv a PROJECT lint pass would run for `file`, as one line.

 This is the batch twin of [`vd_lint_command_line`]: it applies the typed
 per-engine knobs from [`VdLintBatchOptions`] and echoes the same argv the
 pass would spawn (nothing is spawned). Returns null when the options are
 null, the engine code is invalid, or a knob fails validation. Release the
 result with `vd_string_free`.

 # Safety

 `options` must be null or point to a readable [`VdLintBatchOptions`] whose
 lists satisfy the string-list contract; `file` must be null or a valid
 NUL-terminated string.
 */
char *vd_lint_batch_command_line(const struct VdLintBatchOptions *options, const char *file);

/*
 The option groups the selected engine IGNORES (documented no-op), comma
 separated, or null for an invalid engine code.

 The settings page (ux-batch-2 §0.1 R1) greys those controls out and shows
 the reason instead of sending an argument the tool would reject. Release the
 result with `vd_string_free`.

 # Safety

 The return value is null or a crate-owned NUL-terminated string; it must be
 released exactly once with `vd_string_free` and never freed by the caller's
 allocator. The call itself dereferences nothing.
 */
char *vd_lint_unsupported_knobs(int32_t engine);

/*
 Poll a lint job.

 # Safety

 `service` must be null or live and `out` must be null or writable for one
 [`VdDiagnostics`].
 */
int32_t vd_deeplint_poll(struct VdService *service, uint64_t job, struct VdDiagnostics *out);

/*
 Release a filled [`VdDiagnostics`]; null-safe.

 # Safety

 `diagnostics` must be null or point to a struct filled by
 [`vd_deeplint_poll`] / [`vd_deeplint_batch_poll`] and not freed yet.
 */
void vd_diagnostics_free(struct VdDiagnostics *diagnostics);

/*
 Start a project-wide lint pass (AC-2.7/2.8).

 `files` is the pass's snapshot of the file set (already unioned and
 de-duplicated by the caller). `options` must not be null. Returns a job id
 (`>= 1 << 32`) or `0` when the arguments were rejected.

 # Safety

 `service` must be null or live; `files` must satisfy the string-list
 contract; `options` must be null or point to a readable
 [`VdLintBatchOptions`] whose lists satisfy that same contract.
 */
uint64_t vd_deeplint_batch_start(struct VdService *service,
                                 const struct VdStringList *files,
                                 const struct VdLintBatchOptions *options);

/*
 Progress of a project-wide pass.

 Returns `VD_STATUS_PENDING` while it runs (with `out` filled),
 `VD_STATUS_READY` once it reached a terminal phase (with `out` filled), and
 `POLL_MISUSE` for an unknown id or a null `out`.

 # Safety

 `service` must be null or live; `out` must be null or writable for one
 [`VdLintProgress`].
 */
int32_t vd_deeplint_batch_progress(struct VdService *service,
                                   uint64_t job,
                                   struct VdLintProgress *out);

/*
 Poll the result of a project-wide pass.

 `VD_STATUS_PENDING` while it runs; `VD_STATUS_READY` with the collected
 diagnostics once it finished or was cancelled (a cancel delivers no
 diagnostics collected after it); `VD_STATUS_FAILED` when the pass could not
 run (the reason names the tool and the stage). The result is delivered once.

 # Safety

 `service` must be null or live; `out` must be null or writable for one
 [`VdDiagnostics`].
 */
int32_t vd_deeplint_batch_poll(struct VdService *service, uint64_t job, struct VdDiagnostics *out);

/*
 Cancel a project-wide pass: it stops after the current file, the running
 child is killed, and no later file starts (AC-2.8).

 Returns `1` when the job was live, `0` when the id was unknown.

 # Safety

 `service` must be null or live; only null-safe access happens here.
 */
int32_t vd_deeplint_batch_cancel(struct VdService *service, uint64_t job);

/*
 Release a project-wide pass and its progress state (null-safe for an unknown
 id). A running pass is cancelled first.

 # Safety

 `service` must be null or live.
 */
int32_t vd_deeplint_batch_release(struct VdService *service, uint64_t job);

/*
 Release a filled [`VdLintProgress`]; null-safe.

 # Safety

 `progress` must be null or point to a struct filled by
 [`vd_deeplint_batch_progress`] and not freed yet.
 */
void vd_lint_progress_free(struct VdLintProgress *progress);

/*
 Start a project-wide hierarchical elaboration (T7, AC-3.1/3.2/3.4).

 `files` is the project's effective file set and `options` carries the top and
 the `-I`/`-D` settings. The work runs on a worker thread, so the UI thread
 never waits; poll with [`vd_hier_poll`]. Returns a job id (`>= 1 << 48`) or
 `0` when the arguments were rejected.

 # Safety

 `service` must be null or live; `files` must satisfy the string-list contract;
 `options` must be null or point to a readable [`VdHierOptions`] whose lists
 satisfy that same contract.
 */
uint64_t vd_hier_start(struct VdService *service,
                       const struct VdStringList *files,
                       const struct VdHierOptions *options);

/*
 Poll an elaboration job.

 `VD_STATUS_PENDING` while it runs; `VD_STATUS_READY` with `out` filled once it
 finished; `VD_STATUS_FAILED` when the elaboration itself failed (the reason,
 naming the tool and the stage, is readable with `vd_service_error`). The
 result is delivered once.

 # Safety

 `service` must be null or live; `out` must be null or writable for one
 [`VdHierarchy`].
 */
int32_t vd_hier_poll(struct VdService *service, uint64_t job, struct VdHierarchy *out);

/*
 Release an elaboration job without delivering its result (null-safe for an
 unknown id). A running job finishes into memory nobody reads.

 # Safety

 `service` must be null or live.
 */
int32_t vd_hier_release(struct VdService *service, uint64_t job);

/*
 Release a filled [`VdHierarchy`]; null-safe and idempotent.

 # Safety

 `hierarchy` must be null or point to a struct filled by [`vd_hier_poll`] and
 not freed yet.
 */
void vd_hierarchy_free(struct VdHierarchy *hierarchy);

/*
 Start a FIM completion job.

 Returns the job id, or `0` when the arguments were rejected.

 # Safety

 `service` must be null or live; the string arguments must be null or valid
 NUL-terminated strings.
 */
uint64_t vd_fim_complete(struct VdService *service, const char *prefix, const char *suffix);

/*
 Poll a FIM job.

 # Safety

 `service` must be null or live and `out` must be null or writable for one
 [`VdFimResult`].
 */
int32_t vd_fim_poll(struct VdService *service, uint64_t job, struct VdFimResult *out);

/*
 Release a filled [`VdFimResult`]; null-safe.

 # Safety

 `result` must be null or point to a struct filled by [`vd_fim_poll`] and not
 freed yet.
 */
void vd_fim_result_free(struct VdFimResult *result);

/*
 Start an LSP configuration job.

 `rules_path` may be null. Returns the job id, or `0` when the arguments were
 rejected.

 # Safety

 `service` must be null or live; every `*const c_char` must be null or a valid
 NUL-terminated string.
 */
uint64_t vd_lsp_start(struct VdService *service,
                      const char *ls_path,
                      const char *workspace_root,
                      const char *rules_path);

/*
 Poll an LSP job.

 # Safety

 `service` must be null or live and `out` must be null or writable for one
 [`VdLspInfo`].
 */
int32_t vd_lsp_poll(struct VdService *service, uint64_t job, struct VdLspInfo *out);

/*
 Release a filled [`VdLspInfo`]; null-safe.

 # Safety

 `info` must be null or point to a struct filled by [`vd_lsp_poll`] and not
 freed yet.
 */
void vd_lsp_info_free(struct VdLspInfo *info);

/*
 Start an LSP session over the effective file set (verible backend).

 Kept exactly as it was for the existing callers; it is equivalent to
 [`vd_lsp_session_start_v2`] with `backend = 0` and no include dirs/defines.

 # Safety

 See [`start_session_impl`].
 */
uint64_t vd_lsp_session_start(struct VdService *service,
                              const char *ls_path,
                              const char *workspace_root,
                              const char *rules_path,
                              const struct VdStringList *files);

/*
 Start an LSP session with an explicit backend and the project settings (T138).

 `backend` is `0` (verible) or `1` (slang). `include_dirs` / `defines` may be
 null; when given, they reach the selected backend (see [`start_session_impl`]).
 Returns a session id, or `0` when the arguments were rejected (the reason is
 then readable with `vd_service_error`). Nothing is spawned here.

 # Safety

 `service` must be null or live; the `*const c_char` arguments must be null or
 valid NUL-terminated strings; the lists must satisfy [`read_file_list`]'s
 contract.
 */
uint64_t vd_lsp_session_start_v2(struct VdService *service,
                                 const char *ls_path,
                                 const char *workspace_root,
                                 const char *rules_path,
                                 const struct VdStringList *files,
                                 int32_t backend,
                                 const struct VdStringList *include_dirs,
                                 const struct VdStringList *defines);

/*
 Read the observable state of one session (A3d).

 Returns `VD_STATUS_READY` with `out` filled, or `POLL_MISUSE` when the id is
 unknown or `out` is null. The status is a snapshot: it never waits for the
 server.

 # Safety

 `service` must be null or live; `out` must be null or writable for one
 [`VdLspStatus`].
 */
int32_t vd_lsp_session_status(struct VdService *service, uint64_t session, struct VdLspStatus *out);

/*
 Release a filled [`VdLspStatus`]; null-safe and idempotent.

 # Safety

 `status` must be null or point to a struct filled by
 [`vd_lsp_session_status`] and not freed yet.
 */
void vd_lsp_status_free(struct VdLspStatus *status);

/*
 Queue one request on a session.

 `kind` is one of the `VD_LSP_REQ_*` constants. `file` is required for every
 kind; `line`/`character` are zero-based and used by definition, references
 and rename; `new_name` is used only by rename.

 Returns a request id, or `0` when the arguments were rejected.

 # Safety

 `service` must be null or live; `file` and `new_name` must be null or valid
 NUL-terminated strings.
 */
uint64_t vd_lsp_session_request(struct VdService *service,
                                uint64_t session,
                                int32_t kind,
                                const char *file,
                                uint32_t line,
                                uint32_t character,
                                const char *new_name);

/*
 Replace the tracked text of one document with the caller's EDITOR BUFFER
 (a full-document `didChange`). Nothing is read from disk here, so diagnostics
 can follow unsaved edits; the document is opened first when the session does
 not have it. The updated diagnostics arrive through the normal
 `VD_LSP_REQ_DIAGNOSTICS` request (the server publishes them).

 Returns `VD_STATUS_READY` on success; `POLL_MISUSE` when the session id is
 unknown or the arguments are not valid UTF-8 strings (see
 `vd_service_error` for the reason).

 # Safety

 `service` must be null or live; `path` and `text` must be valid
 NUL-terminated strings.
 */
int32_t vd_lsp_session_change(struct VdService *service,
                              uint64_t session,
                              const char *path,
                              const char *text);

/*
 Poll one request.

 `VD_STATUS_PENDING` while the worker is busy, `VD_STATUS_READY` with `out`
 filled, `VD_STATUS_FAILED` with the reason in `vd_service_error`, or
 `POLL_MISUSE` for an unknown/consumed id or a null `out`.

 # Safety

 `service` must be null or live; `out` must be null or writable for one
 [`VdLspResponse`].
 */
int32_t vd_lsp_request_poll(struct VdService *service,
                            uint64_t session,
                            uint64_t request,
                            struct VdLspResponse *out);

/*
 Release a filled [`VdLspResponse`]; null-safe and idempotent.

 # Safety

 `response` must be null or point to a struct filled by
 [`vd_lsp_request_poll`] and not freed yet.
 */
void vd_lsp_response_free(struct VdLspResponse *response);

/*
 Ask a session to shut its server down. Non-blocking and idempotent.

 Returns `0` when the request was delivered, `POLL_MISUSE` for an unknown id.

 # Safety

 `service` must be null or live.
 */
int32_t vd_lsp_session_stop(struct VdService *service, uint64_t session);

/*
 Stop and forget a session (the worker is asked to shut down before the slot
 is dropped). Null-safe for an unknown id.

 # Safety

 `service` must be null or live.
 */
void vd_lsp_session_free(struct VdService *service, uint64_t session);

/*
 Release a filled [`VdLspCapabilities`]; null-safe and idempotent.

 # Safety

 `capabilities` must be null or point to a struct filled by
 [`vd_lsp_backend_capabilities`] / [`vd_lsp_session_capabilities`] and not
 freed yet.
 */
void vd_lsp_capabilities_free(struct VdLspCapabilities *capabilities);

/*
 The static capability table of one backend (T138, AC-1.3).

 `backend` is `0` (verible) or `1` (slang). The verible rows are measured; the
 slang rows are `-1` (unknown) until a live session answers, because no
 slang-server binary is built in this environment. Never touches a session.

 # Safety

 `out` must be null or writable for one [`VdLspCapabilities`].
 */
int32_t vd_lsp_backend_capabilities(int32_t backend, struct VdLspCapabilities *out);

/*
 The capability table of a live session (T138).

 Before the server answers `initialize` this equals the static table; after it
 the rows are the server's own advertisements (`basis = live`).

 # Safety

 `service` must be null or live; `out` must be null or writable for one
 [`VdLspCapabilities`].
 */
int32_t vd_lsp_session_capabilities(struct VdService *service,
                                    uint64_t session,
                                    struct VdLspCapabilities *out);

/*
 The exact argv a session would use, as one owned string (AC-2.5's dump).

 Spawns nothing. `file_list_path` may be null (the caller can generate one with
 the project settings instead; the string then omits it). Returns null only
 when `backend`, `ls_path` or `workspace_root` is invalid. Release the result
 with `vd_string_free`.

 # Safety

 Every pointer argument must be null or valid for the duration of the call; the
 two lists must satisfy [`read_file_list`]'s contract.
 */
char *vd_lsp_command_line(int32_t backend,
                          const char *ls_path,
                          const char *workspace_root,
                          const char *rules_path,
                          const char *file_list_path,
                          const struct VdStringList *include_dirs,
                          const struct VdStringList *defines);

/*
 The Verible file list this session would write for the project settings.

 This is the AC-2.5 "config dump" for the verible backend: the project's
 include dirs and defines reach `verible-verilog-ls` as `+incdir+` /
 `+define+` lines in a `.f` passed with `--file_list_path`, because the server
 has no include/define flag. Spawns and writes nothing. Returns an owned string
 (release with `vd_string_free`) or null for invalid arguments.

 # Safety

 `files`, `include_dirs` and `defines` must be null or satisfy the
 string-list contract.
 */
char *vd_lsp_file_list_preview(const struct VdStringList *files,
                               const struct VdStringList *include_dirs,
                               const struct VdStringList *defines);

/*
 The exact program plus argv one session was started with (AC-2.5's dump).

 For the verible backend with project include dirs/defines this is where the
 generated file list shows up as `--file_list_path=<path>`; the file itself
 contains the `+incdir+` / `+define+` lines (`vd_lsp_file_list_preview` shows
 the same text before a session exists). Returns an owned string (release with
 `vd_string_free`), or null for an unknown session.

 # Safety

 `service` must be null or live.
 */
char *vd_lsp_session_command_line(struct VdService *service, uint64_t session);

/*
 Start a project/.f expansion job.

 Returns the job id, or `0` when the arguments were rejected (the reason is
 readable with `vd_service_error`).

 # Safety

 `service` must be null or a live handle; `start_dir` must be null or a valid
 NUL-terminated string.
 */
uint64_t vd_project_parse(struct VdService *service, const char *start_dir);

/*
 Start a full project-model expansion job (T6, AC-2.1–2.5).

 Unlike [`vd_project_parse`] — kept unchanged for the existing callers — this
 entry point implements the project model of C7:

 * `start_dir` is the folder the user opened and becomes the ROOT; the upward
   discovery never moves it above that folder;
 * `filelists` (null or empty when none) are the imported `.f` files;
 * `settings_path` (null for "no tools page yet") is the `settings.toml` whose
   `tools.include_dirs` / `tools.defines` are appended to the project's own;
 * the file set is the union of the `.f` expansion and a recursive scan of
   the root (`.v`/`.sv`/`.vh`/`.svh`), de-duplicated.

 Returns the job id, or `0` when the arguments were rejected (the reason is
 readable with `vd_service_error`). Poll it with [`vd_project_poll`] and free
 the result with [`vd_project_info_free`].

 # Safety

 `service` must be null or a live handle; `start_dir`/`settings_path` must be
 null or valid NUL-terminated strings; `filelists` must be null or satisfy the
 [`VdStringList`] contract for the duration of the call.
 */
uint64_t vd_project_expand(struct VdService *service,
                           const char *start_dir,
                           const struct VdStringList *filelists,
                           const char *settings_path);

/*
 Start a full project-model expansion with the project's filelist VARIABLE
 TABLE (ADDENDUM A-9).

 Same contract as [`vd_project_expand`], plus `variables`: a list of
 `NAME=VALUE` entries (a bare `NAME` is a defined-but-empty variable). The
 table wins over the process environment; `$VAR` / `${VAR}` / `$(VAR)` / `~`
 inside the `.f` files expand from it. Unresolved names come back in
 `VdProjectInfo.missing_variables` and each affected line is a warning with
 its `.f` file and 1-based line.

 Returns the job id, or `0` when the arguments were rejected (the reason is
 readable with `vd_service_error`). Poll with [`vd_project_poll`], free with
 [`vd_project_info_free`].

 # Safety

 Same pointer contract as [`vd_project_expand`]; `variables` must be null or
 satisfy the [`VdStringList`] contract for the duration of the call.
 */
uint64_t vd_project_expand_v2(struct VdService *service,
                              const char *start_dir,
                              const struct VdStringList *filelists,
                              const char *settings_path,
                              const struct VdStringList *variables);

/*
 Poll a project job.

 `VD_STATUS_READY` fills `out`; `VD_STATUS_FAILED` leaves it untouched and the
 reason is readable with `vd_service_error`.

 # Safety

 `service` must be null or live and `out` must be null or writable for one
 [`VdProjectInfo`].
 */
int32_t vd_project_poll(struct VdService *service, uint64_t job, struct VdProjectInfo *out);

/*
 Release a filled [`VdProjectInfo`]; null-safe and idempotent.

 # Safety

 `info` must be null or point to a struct filled by [`vd_project_poll`] and not
 freed yet.
 */
void vd_project_info_free(struct VdProjectInfo *info);

/*
 Start a tree-search job (walk + match, T21).

 `case_sensitive`/`verilog_only` are `u8` booleans (0/1). The result is read
 with [`vd_search_tree_poll`], which also returns the observable summary.
 This is the legacy option set; [`vd_search_tree_query_ex`] carries whole-word,
 regex, extension filtering and every-match, and is the one new callers use.

 # Safety

 `service` must be null or live; `root`/`needle` must be null or valid
 NUL-terminated strings.
 */
uint64_t vd_search_tree_query(struct VdService *service,
                              const char *root,
                              const char *needle,
                              size_t limit,
                              uint64_t max_file_bytes,
                              uint64_t max_duration_ms,
                              uint8_t case_sensitive,
                              uint8_t verilog_only);

/*
 Start an EXTENDED tree-search job: the same walk, with the full query option
 set (case, whole word, regular expression, extension filter, every-match)
 and the exact match range on every hit.

 `options` may be null (the legacy behaviour: case-insensitive literal, first
 match per line, default caps). A regular expression that does not compile is
 rejected before the walk: the job fails and `vd_service_error` carries the
 reason. Poll with [`vd_search_tree_poll`].

 # Safety

 `service` must be null or live; `root`/`needle` must be null or valid
 NUL-terminated strings; `options` must be null or point to a readable
 [`VdSearchOptions`] whose extension list satisfies the string-list contract.
 */
uint64_t vd_search_tree_query_ex(struct VdService *service,
                                 const char *root,
                                 const char *needle,
                                 const struct VdSearchOptions *options);

/*
 Start a search job.

 `files` is a C array of `file_count` borrowed strings. Returns the job id, or
 `0` when the arguments were rejected.

 # Safety

 `service` must be null or live; `root`/`needle` must be null or valid
 NUL-terminated strings; `files` must be null or point to `file_count` valid
 NUL-terminated strings.
 */
uint64_t vd_search_query(struct VdService *service,
                         const char *root,
                         const char *const *files,
                         size_t file_count,
                         const char *needle,
                         size_t limit);

/*
 Poll a search job.

 # Safety

 `service` must be null or live and `out` must be null or writable for one
 [`VdSearchResults`].
 */
int32_t vd_search_poll(struct VdService *service, uint64_t job, struct VdSearchResults *out);

/*
 Poll a tree-search job and fill both the hits and the summary.

 `out_summary` may be null; when present it is zero-filled for a job that was
 started by [`vd_search_query`] (no tree summary exists).

 # Safety

 `service` must be null or live; `out_results`/`out_summary` must be null or
 writable for one [`VdSearchResults`] / [`VdSearchSummary`].
 */
int32_t vd_search_tree_poll(struct VdService *service,
                            uint64_t job,
                            struct VdSearchResults *out_results,
                            struct VdSearchSummary *out_summary);

/*
 Release a filled [`VdSearchResults`]; null-safe.

 # Safety

 `results` must be null or point to a struct filled by [`vd_search_poll`] and
 not freed yet.
 */
void vd_search_results_free(struct VdSearchResults *results);

/*
 Copy the last error into `out`; returns 1 when an error was present.

 # Safety

 `service` must be null or live, and `out` must be null or writable for one
 [`VdError`].
 */
int32_t vd_service_error(struct VdService *service, struct VdError *out);

/*
 The C ABI version implemented by this library (touches no memory).
 */
uint32_t vd_abi_version(void);

/*
 Create a service handle: no threads and no I/O until a job is started.
 The returned pointer is released with [`vd_service_destroy`].
 */
struct VdService *vd_service_create(void);

/*
 Destroy a service handle. Running jobs finish into memory nobody reads.

 # Safety

 `service` must be null or a handle from [`vd_service_create`] that was not
 destroyed already; the pointer is invalid after this call.
 */
void vd_service_destroy(struct VdService *service);

/*
 Cancel one job (`1` when it was live, `0` when the id was unknown).

 A job already running is not interrupted; its result is discarded so a stale
 completion can never overwrite a newer one.

 # Safety

 `service` must be null or a live handle; the function touches only null-safely.
 */
int32_t vd_job_cancel(struct VdService *service, uint64_t job);

/*
 Drop one job and its result without reporting an error.

 # Safety

 `service` must be null or a live handle; the function touches only null-safely.
 */
int32_t vd_job_release(struct VdService *service, uint64_t job);

/*
 Start a settings-read job.

 # Safety

 `service` must be null or live; `path` must be null or a valid NUL-terminated
 string.
 */
uint64_t vd_settings_load(struct VdService *service, const char *path);

/*
 Start a settings-write job over `count` borrowed key/value pairs.

 Returns the job id, or `0` when the arguments were rejected.

 # Safety

 `service` must be null or live; `path` must be null or a valid NUL-terminated
 string; `entries` must be null or point to `count` readable
 [`VdSettingInput`] values whose strings are valid NUL-terminated strings.
 */
uint64_t vd_settings_save(struct VdService *service,
                          const char *path,
                          const struct VdSettingInput *entries,
                          size_t count);

/*
 Poll a settings job (load or save). A successful save returns `READY` with an
 empty result.

 # Safety

 `service` must be null or live and `out` must be null or writable for one
 [`VdSettings`].
 */
int32_t vd_settings_poll(struct VdService *service, uint64_t job, struct VdSettings *out);

/*
 Release a filled [`VdSettings`]; null-safe.

 # Safety

 `settings` must be null or point to a struct filled by [`vd_settings_poll`]
 and not freed yet.
 */
void vd_settings_free(struct VdSettings *settings);

/*
 Start a VCS status job for `root`.

 Returns the job id, or `0` when the arguments were rejected.

 # Safety

 `service` must be null or live; `root` must be null or a valid NUL-terminated
 string.
 */
uint64_t vd_vcs_status(struct VdService *service, const char *root);

/*
 Poll a VCS job.

 # Safety

 `service` must be null or live and `out` must be null or writable for one
 [`VdVcsStatus`].
 */
int32_t vd_vcs_poll(struct VdService *service, uint64_t job, struct VdVcsStatus *out);

/*
 Release a filled [`VdVcsStatus`]; null-safe.

 # Safety

 `status` must be null or point to a struct filled by [`vd_vcs_poll`] and not
 freed yet.
 */
void vd_vcs_status_free(struct VdVcsStatus *status);

/*
 Starts a branch + revision job for `root`.

 Returns the job id, or `0` when the arguments were rejected. The panel polls
 with [`vd_vcs_info_poll`], so the GUI thread never waits for `git`/`svn`.

 # Safety

 `service` must be null or live; `root` must be null or a valid NUL-terminated
 string.
 */
uint64_t vd_vcs_info_job(struct VdService *service, const char *root);

/*
 Polls a branch + revision job started by [`vd_vcs_info_job`].

 # Safety

 `service` must be null or live and `out` must be null or writable for one
 [`VdVcsInfo`].
 */
int32_t vd_vcs_info_poll(struct VdService *service, uint64_t job, struct VdVcsInfo *out);

/*
 Releases the strings of a [`VdVcsInfo`] (the struct itself is by value).

 # Safety

 `info` must be null or a struct returned by `vd_vcs_info_poll` that was not freed.
 */
void vd_vcs_info_free(struct VdVcsInfo *info);

/*
 Starts a unified-diff job for one file of the working tree containing `root`.

 Returns the job id, or `0` when the arguments were rejected. The panel polls
 with [`vd_vcs_diff_poll`], so the GUI thread never waits for `git diff`.

 # Safety

 `service` must be null or live; `root` and `path` must be null or valid
 NUL-terminated UTF-8 strings.
 */
uint64_t vd_vcs_diff_job(struct VdService *service, const char *root, const char *path);

/*
 Polls a diff job started by [`vd_vcs_diff_job`].

 # Safety

 `service` must be null or live and `out` must be null or writable for one
 [`VdVcsDiff`].
 */
int32_t vd_vcs_diff_poll(struct VdService *service, uint64_t job, struct VdVcsDiff *out);

/*
 Releases the strings of a [`VdVcsDiff`].

 # Safety

 `diff` must be null or a struct returned by `vd_vcs_diff_poll` that was not freed.
 */
void vd_vcs_diff_free(struct VdVcsDiff *diff);

/*
 Starts a paged commit-list job for `root`.

 Returns the job id, or `0` when the arguments were rejected. Poll with
 [`vd_vcs_history_poll`], so the GUI thread never waits for `git log`.

 # Safety

 `service` must be null or live; `root` and `query` must be null or valid
 NUL-terminated UTF-8 strings.
 */
uint64_t vd_vcs_history_commits_job(struct VdService *service,
                                    const char *root,
                                    uint64_t offset,
                                    uint64_t limit,
                                    const char *query);

/*
 Starts a commit-detail job (meta + files + patch) for `hash`.

 # Safety

 `service` must be null or live; `root` and `hash` must be null or valid
 NUL-terminated UTF-8 strings.
 */
uint64_t vd_vcs_history_commit_job(struct VdService *service, const char *root, const char *hash);

/*
 Starts a paged single-file history job for `path`.

 # Safety

 `service` must be null or live; `root` and `path` must be null or valid
 NUL-terminated UTF-8 strings.
 */
uint64_t vd_vcs_history_file_job(struct VdService *service,
                                 const char *root,
                                 const char *path,
                                 uint64_t offset,
                                 uint64_t limit);

/*
 Starts a blame job for `path`.

 # Safety

 `service` must be null or live; `root` and `path` must be null or valid
 NUL-terminated UTF-8 strings.
 */
uint64_t vd_vcs_history_blame_job(struct VdService *service, const char *root, const char *path);

/*
 Starts a two-commit comparison job (`git diff <from> <to>`).

 # Safety

 `service` must be null or live; `root`, `from` and `to` must be null or valid
 NUL-terminated UTF-8 strings.
 */
uint64_t vd_vcs_history_compare_job(struct VdService *service,
                                    const char *root,
                                    const char *from,
                                    const char *to);

/*
 Polls any history job started by the five `vd_vcs_history_*_job` functions.

 # Safety

 `service` must be null or live and `out` must be null or writable for one
 [`VdVcsHistoryResult`].
 */
int32_t vd_vcs_history_poll(struct VdService *service,
                            uint64_t job,
                            struct VdVcsHistoryResult *out);

/*
 Releases the arrays and strings of a [`VdVcsHistoryResult`].

 # Safety

 `result` must be null or a struct returned by [`vd_vcs_history_poll`] that was
 not freed.
 */
void vd_vcs_history_free(struct VdVcsHistoryResult *result);

/*
 Starts a stash-list job for `root`.

 Returns the job id, or `0` when the argument was rejected. Poll with
 [`vd_vcs_workspace_poll`], so the GUI thread never waits for `git stash list`.

 # Safety

 `service` must be null or live; `root` must be null or a valid NUL-terminated
 UTF-8 string.
 */
uint64_t vd_vcs_workspace_stashes_job(struct VdService *service, const char *root);

/*
 Starts a worktree-list job for `root`.

 Returns the job id, or `0` when the argument was rejected. Poll with
 [`vd_vcs_workspace_poll`].

 # Safety

 `service` must be null or live; `root` must be null or a valid NUL-terminated
 UTF-8 string.
 */
uint64_t vd_vcs_workspace_worktrees_job(struct VdService *service, const char *root);

/*
 Polls any workspace job started by the two `vd_vcs_workspace_*_job`
 functions.

 # Safety

 `service` must be null or live and `out` must be null or writable for one
 [`VdVcsWorkspaceResult`].
 */
int32_t vd_vcs_workspace_poll(struct VdService *service,
                              uint64_t job,
                              struct VdVcsWorkspaceResult *out);

/*
 Releases the array and strings of a [`VdVcsWorkspaceResult`].

 # Safety

 `result` must be null or a struct returned by [`vd_vcs_workspace_poll`] that
 was not freed.
 */
void vd_vcs_workspace_free(struct VdVcsWorkspaceResult *result);

/*
 Starts one write job.

 Returns the job id, or `0` when the request itself was rejected (null
 pointer, unknown operation code, or a non-UTF-8 string). A request that is
 well-formed but violates the operation's contract (no repository, missing
 message, unsafe path) starts a job that fails with
 `VD_ERR_INVALID_ARGUMENT`, so no file-system work runs on the calling
 thread.

 # Safety

 `service` must be null or live; `request` must be null or point to a
 [`VdVcsWriteRequest`] whose pointers are valid for the duration of the call.
 */
uint64_t vd_vcs_write_job(struct VdService *service, const struct VdVcsWriteRequest *request);

/*
 Polls a write job started by [`vd_vcs_write_job`].

 `VD_STATUS_READY` with `out` filled means the tool really ran (read `ok` for
 its exit status). A rejected request or a tool that could not be started
 yields `VD_STATUS_FAILED` plus a typed error in `vd_service_error`.

 # Safety

 `service` must be null or live and `out` must be null or writable for one
 [`VdVcsWriteResult`].
 */
int32_t vd_vcs_write_poll(struct VdService *service, uint64_t job, struct VdVcsWriteResult *out);

/*
 Releases the strings of a [`VdVcsWriteResult`]; null-safe.

 # Safety

 `result` must be null or a struct returned by [`vd_vcs_write_poll`] that was
 not freed yet.
 */
void vd_vcs_write_free(struct VdVcsWriteResult *result);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  /* VDIT_VD_FFI_H */
