# C9 backfill (T112/N2): header added after the fact; the body below is unchanged.
# original_collected_at_utc: 2026-11-01T08:03:50Z   (from the archived file mtime)
# original_git_head: 206033c1faa91ac6b89cca0d575d36283ab69d7f
# original_git_status_porcelain_sha256: 50471d2bac6fbfce420c003a8f921497e7b3eabb201e9ad852d0b8e9c5319c2a (hash of the porcelain block archived in final-206033c/00-C9.txt)
# session_anchor: final-206033c/00-C9.txt
# backfill_collected_at_utc: 2026-11-01T09:23:35Z
# backfill_git_head: 0c8ea9fe4ae18c386a59a55b8868f837f4f80a84
# backfill_git_status_porcelain_sha256: a7feab0230532310375511329de129cad97d49e690c7a9c7336394a1cce151fa (current tree at backfill time)
== T31 phase 2 blockers ==
collected_at: 2026-11-01T08:03:49Z
git_head: 206033c1faa91ac6b89cca0d575d36283ab69d7f
remote_branch_head: 206033c1faa91ac6b89cca0d575d36283ab69d7f

## Blocker 1: baseline 206033c is not Rust-buildable as committed
clean worktree: target/wt-206033c (detached 206033c) + only T31 packaging patches
build error: vd-ffi/src/vcs.rs references JobValue::VcsInfo / JobValue::VcsDiff
baseline service.rs JobValue variants:
  pub(crate) enum JobValue {
      /// Project model / `.f` expansion.
      Project(crate::project::ProjectData),
      /// Verible LSP session description.
      Lsp(crate::lsp::LspData),
      /// SLANG deep-lint diagnostics.
      Deeplint(crate::deeplint::DeeplintData),
      /// FIM completion result.
      Fim(crate::fim::FimData),
      /// Global search hits.
      Search(crate::search::SearchData),
      /// VCS status.
      Vcs(crate::vcs::VcsData),
      /// Settings read from the store.
      Settings(crate::settings::SettingsData),
      /// Settings written successfully (no payload).
      SettingsSaved,
  }
referenced by committed vcs.rs:
  323:    state.start(move || Ok(JobValue::VcsInfo(info_data(&root))))
  348:        Polled::Ready(JobValue::VcsInfo(data)) => {
  411:    state.start(move || Ok(JobValue::VcsDiff(diff_data(&root, &path))))
  436:        Polled::Ready(JobValue::VcsDiff(data)) => {
worktree has an UNCOMMITTED fix (main tree diff):
  diff --git a/crates/vd-ffi/src/service.rs b/crates/vd-ffi/src/service.rs
  index 2883996..a33ac33 100644
  --- a/crates/vd-ffi/src/service.rs
  +++ b/crates/vd-ffi/src/service.rs
  @@ -55,6 +55,10 @@ pub(crate) enum JobValue {
       Search(crate::search::SearchData),
       /// VCS status.
       Vcs(crate::vcs::VcsData),
  +    /// VCS branch + revision identity (T99: job-based, GUI thread never blocks).
  +    VcsInfo(crate::vcs::VcsInfoData),
  +    /// One file's VCS diff (T99: job-based, GUI thread never blocks).
  +    VcsDiff(crate::vcs::VcsDiffData),
       /// Settings read from the store.
       Settings(crate::settings::SettingsData),
       /// Settings written successfully (no payload).
with that 4-line diff applied in the clean worktree: cargo check -p vd-ffi -> see above (CARGO-CHECK-EXIT)

## Blocker 2: pinned appimagetool sha256 mismatch (per red line: stopped, no bypass)
url: APPIMAGETOOL_URL="https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage"
want: APPIMAGETOOL_SHA256="793fca0ce4bcc309dc65d3f66669f58e3755643e4dadbe03932e7b436128494f"
got:  ac7fd56feade6002c2f57db39587113275d351fce911a5fbb29ea84cd0666cab
size: 15137272 bytes
