//! Resolution-order tests for the bundled VCS toolchain (AC-3.0).
//!
//! The resolver is pure: [`resolve_with`] takes the executable directory, the
//! PATH value and the explicit env value, so every branch is tested without
//! mutating the process environment.

use std::ffi::OsStr;
use std::os::unix::fs::PermissionsExt;
use std::path::{Path, PathBuf};

use vd_vcs::toolchain::{bundled_candidates, resolve_with, ToolSource};

fn write_executable(path: &Path) {
    if let Some(parent) = path.parent() {
        std::fs::create_dir_all(parent).ok();
    }
    std::fs::write(path, "#!/bin/sh\nexit 0\n").ok();
    if let Ok(metadata) = std::fs::metadata(path) {
        let mut permissions = metadata.permissions();
        permissions.set_mode(0o755);
        std::fs::set_permissions(path, permissions).ok();
    }
}

fn write_plain_file(path: &Path) {
    if let Some(parent) = path.parent() {
        std::fs::create_dir_all(parent).ok();
    }
    std::fs::write(path, "not executable\n").ok();
}

fn canonical(path: &Path) -> Option<PathBuf> {
    std::fs::canonicalize(path).ok()
}

#[test]
fn bundled_layout_wins() {
    let Ok(dir) = tempfile::tempdir() else {
        return;
    };
    let root = dir.path();
    let exe_dir = root.join("bin");
    std::fs::create_dir_all(&exe_dir).ok();
    let bundled = root.join("usr/lib/vcs/bin/git");
    write_executable(&bundled);

    let resolved = resolve_with("git", &exe_dir, Some(OsStr::new("")), None);
    let Some(resolved) = resolved else {
        panic!("the bundled git must be found");
    };
    assert_eq!(resolved.source, ToolSource::Bundled);
    assert_eq!(canonical(&resolved.path), canonical(&bundled));
    assert_eq!(resolved.tool, "git");
}

#[test]
fn next_to_the_binary_is_also_bundled() {
    let Ok(dir) = tempfile::tempdir() else {
        return;
    };
    let exe_dir = dir.path().join("bin");
    let bundled = exe_dir.join("git");
    write_executable(&bundled);
    let resolved = resolve_with("git", &exe_dir, Some(OsStr::new("")), None);
    assert_eq!(resolved.map(|tool| tool.source), Some(ToolSource::Bundled));
}

#[test]
fn path_is_used_when_nothing_is_bundled() {
    let Ok(dir) = tempfile::tempdir() else {
        return;
    };
    let exe_dir = dir.path().join("bin");
    std::fs::create_dir_all(&exe_dir).ok();
    let path_dir = dir.path().join("usr-bin");
    let on_path = path_dir.join("git");
    write_executable(&on_path);
    let path_env = std::env::join_paths([&path_dir]).ok();
    let resolved = resolve_with("git", &exe_dir, path_env.as_deref(), None);
    let Some(resolved) = resolved else {
        panic!("PATH must provide git");
    };
    assert_eq!(resolved.source, ToolSource::Path);
    assert_eq!(canonical(&resolved.path), canonical(&on_path));
}

#[test]
fn env_is_the_last_resort() {
    let Ok(dir) = tempfile::tempdir() else {
        return;
    };
    let exe_dir = dir.path().join("bin");
    std::fs::create_dir_all(&exe_dir).ok();
    let explicit = dir.path().join("opt/git-2.53/bin/git");
    write_executable(&explicit);
    let resolved = resolve_with(
        "git",
        &exe_dir,
        Some(OsStr::new("")),
        Some(explicit.as_os_str()),
    );
    let Some(resolved) = resolved else {
        panic!("the explicit env value must be used");
    };
    assert_eq!(resolved.source, ToolSource::Env);
    assert_eq!(canonical(&resolved.path), canonical(&explicit));
}

#[test]
fn order_is_bundled_then_path_then_env() {
    let Ok(dir) = tempfile::tempdir() else {
        return;
    };
    let root = dir.path();
    let exe_dir = root.join("bin");
    std::fs::create_dir_all(&exe_dir).ok();
    let bundled = root.join("usr/lib/vcs/bin/svn");
    let on_path = root.join("usr-bin/svn");
    let explicit = root.join("opt/svn/svn");
    write_executable(&bundled);
    write_executable(&on_path);
    write_executable(&explicit);
    let path_env = std::env::join_paths([root.join("usr-bin")]).ok();
    let env = Some(explicit.as_os_str());

    let first = resolve_with("svn", &exe_dir, path_env.as_deref(), env);
    assert_eq!(first.map(|tool| tool.source), Some(ToolSource::Bundled));

    std::fs::remove_file(&bundled).ok();
    let second = resolve_with("svn", &exe_dir, path_env.as_deref(), env);
    assert_eq!(second.map(|tool| tool.source), Some(ToolSource::Path));

    std::fs::remove_file(&on_path).ok();
    let third = resolve_with("svn", &exe_dir, path_env.as_deref(), env);
    assert_eq!(third.map(|tool| tool.source), Some(ToolSource::Env));

    std::fs::remove_file(&explicit).ok();
    assert!(resolve_with("svn", &exe_dir, path_env.as_deref(), env).is_none());
}

#[test]
fn a_missing_tool_is_reported_as_none() {
    let Ok(dir) = tempfile::tempdir() else {
        return;
    };
    let exe_dir = dir.path().join("bin");
    std::fs::create_dir_all(&exe_dir).ok();
    // Empty PATH and no env value: nothing can satisfy the request.
    assert!(resolve_with("git", &exe_dir, Some(OsStr::new("")), None).is_none());
    assert!(resolve_with("unknown-tool", &exe_dir, None, None).is_none());
}

#[test]
fn a_non_executable_candidate_is_skipped() {
    let Ok(dir) = tempfile::tempdir() else {
        return;
    };
    let root = dir.path();
    let exe_dir = root.join("bin");
    std::fs::create_dir_all(&exe_dir).ok();
    write_plain_file(&root.join("usr/lib/vcs/bin/git"));
    let path_dir = root.join("usr-bin");
    let real = path_dir.join("git");
    write_executable(&real);
    let path_env = std::env::join_paths([&path_dir]).ok();
    let resolved = resolve_with("git", &exe_dir, path_env.as_deref(), None);
    assert_eq!(resolved.map(|tool| tool.source), Some(ToolSource::Path));
}

#[test]
fn bundled_candidates_are_the_appdir_layout_first() {
    let exe_dir = Path::new("/opt/vedit/bin");
    let candidates = bundled_candidates("git", exe_dir);
    assert_eq!(candidates.len(), 4);
    assert_eq!(
        candidates[0],
        PathBuf::from("/opt/vedit/bin/../usr/lib/vcs/bin/git")
    );
    // Loader-launched layout: exe_dir IS the AppDir root.
    assert_eq!(
        candidates[1],
        PathBuf::from("/opt/vedit/bin/usr/lib/vcs/bin/git")
    );
    assert_eq!(candidates[2], PathBuf::from("/opt/vedit/bin/git"));
}
