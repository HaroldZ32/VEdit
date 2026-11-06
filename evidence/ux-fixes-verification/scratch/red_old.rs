#[path = "/work/old_binary.rs"]
mod binary;
use std::os::unix::fs::PermissionsExt;

fn setup(tag: &str) -> std::path::PathBuf {
    let root = std::env::temp_dir().join(format!("vedit-red-{}-{}", tag, std::process::id()));
    let _ = std::fs::remove_dir_all(&root);
    let appdir = root.join("AppDir");
    std::fs::create_dir_all(appdir.join("bin")).unwrap();
    let slang = appdir.join("bin/slang");
    std::fs::write(&slang, b"#!/bin/sh\nexit 0\n").unwrap();
    std::fs::set_permissions(&slang, std::fs::Permissions::from_mode(0o755)).unwrap();
    appdir
}

fn main() {
    let appdir = setup("old");
    let r = binary::resolve_binary(None, None, &appdir);
    println!("OLD attempts={} chosen={:?}", r.attempts.len(), r.chosen);
    println!("{}", r.describe());
    assert_eq!(r.attempts.len(), 4, "pre-fix resolver probes 4 bases");
    assert!(r.chosen.is_none(), "pre-fix resolver must MISS <exe_dir>/bin/slang");
    let tarball = appdir.parent().unwrap().join("bin/slang");
    std::fs::create_dir_all(tarball.parent().unwrap()).unwrap();
    std::fs::copy(appdir.join("bin/slang"), &tarball).unwrap();
    std::fs::set_permissions(&tarball, std::fs::Permissions::from_mode(0o755)).unwrap();
    let r2 = binary::resolve_binary(None, None, &appdir);
    let norm = |x: &std::path::Path| std::fs::canonicalize(x).unwrap();
    assert_eq!(r2.chosen.as_deref().map(norm), Some(norm(&tarball)), "old tarball base still works");
    println!("RED-OK: old resolver misses the AppDir-root base (4 attempts), tarball base still works");
    std::fs::remove_dir_all(appdir.parent().unwrap()).ok();
}
