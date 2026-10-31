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
