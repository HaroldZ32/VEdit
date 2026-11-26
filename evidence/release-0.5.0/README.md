# VEdit 0.5.0 release evidence

The release branch starts at `c4933a8e783d2774b97a937bf2e0c82b52efab6e` on `dev`, after PRs #24 and #25. The 0.5.0 release metadata and the version-specific instructions were checked during preparation.

| Preparation check | Observed result |
| --- | --- |
| `bash scripts/ci-qt6.sh version` | `0.5.0` |
| Cargo locked workspace metadata | 11 workspace members at 0.5.0, 217 resolved packages |
| Root and terminal CMake configuration | `CMAKE_PROJECT_VERSION:STATIC=0.5.0` in both caches |
| `bash build/gates/lrelease.sh` | 1,888 finished, 0 unfinished, 0 untranslated |
| `bash build/gates/docs.sh` | Passed |
| `cargo +stable test --offline --locked -p vd-config --test project_define_precedence` | 1 passed |

The [release notes](../../docs/releases/0.5.0.md) describe the shipped changes. The release PR and the annotated tag each trigger a separate build of the three packages. Public release verification uses the packages and the checksum list from the tag build.
