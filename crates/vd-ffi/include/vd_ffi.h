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
 The C-side opaque handle.

 This type has no variants and no fields on purpose: it exists only as a
 pointer type, and cbindgen renders it as `typedef struct VdService VdService;`.
 The real state is [`ServiceState`]; the handle is a cast of its address.
 */
typedef struct VdService VdService;

/*
 One slang diagnostic.
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
   slang diagnostic code (`UndeclaredIdentifier`, …).
   */
  char *code;
  /*
   Human-readable message.
   */
  char *message;
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
} VdLspResponse;

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
} VdProjectInfo;

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
   1-based byte column.
   */
  uint32_t column;
  /*
   Trimmed, truncated matched line.
   */
  char *preview;
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

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/*
 Start a SLANG lint job for one file.

 `binary_override` may be null (the service then resolves the binary itself).
 Returns the job id, or `0` when the arguments were rejected.

 # Safety

 `service` must be null or live; the string arguments must be null or valid
 NUL-terminated strings.
 */
uint64_t vd_deeplint_lint(struct VdService *service, const char *file, const char *binary_override);

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
 [`vd_deeplint_poll`] and not freed yet.
 */
void vd_diagnostics_free(struct VdDiagnostics *diagnostics);

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
 Start an LSP session over the effective file set.

 `ls_path` must be the Verible language-server binary, `workspace_root` the
 project root; `rules_path` may be null; `files` must point to the file list
 the project model produced (the same list the caller passes to SLANG).

 Returns a session id, or `0` when the arguments were rejected; the reason is
 then readable with `vd_service_error`. No process is started here: the
 worker starts the server on its first `didOpen`, so this call cannot block
 the UI thread (C2).

 # Safety

 `service` must be null or live; the `*const c_char` arguments must be null or
 valid NUL-terminated strings; `files` must satisfy [`read_file_list`]'s
 contract.
 */
uint64_t vd_lsp_session_start(struct VdService *service,
                              const char *ls_path,
                              const char *workspace_root,
                              const char *rules_path,
                              const struct VdStringList *files);

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
 Start a project/.f expansion job.

 Returns the job id, or `0` when the arguments were rejected (the reason is
 readable with `vd_service_error`).

 # Safety

 `service` must be null or a live handle; `start_dir` must be null or a valid
 NUL-terminated string.
 */
uint64_t vd_project_parse(struct VdService *service, const char *start_dir);

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

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  /* VDIT_VD_FFI_H */
