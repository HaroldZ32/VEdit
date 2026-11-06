#[path = "/work/current_binary.rs"]
mod binary;
use std::os::unix::fs::PermissionsExt;

fn setup(tag: &str) -> std::path::PathBuf {
    let root = std::env::temp_dir().join(format!("vedit-green-{}-{}", tag, std::process::id()));
    let _ = std::fs::remove_dir_all(&root);
    let appdir = root.join("AppDir");
    std::fs::create_dir_all(appdir.join("bin")).unwrap();
    let slang = appdir.join("bin/slang");
    std::fs::write(&slang, b"#!/bin/sh\nexit 0\n").unwrap();
    std::fs::set_permissions(&slang, std::fs::Permissions::from_mode(0o755)).unwrap();
    appdir
}

fn main() {
    let appdir = setup("new");
    let r = binary::resolve_binary(None, None, &appdir);
    println!("NEW attempts={} chosen={:?}", r.attempts.len(), r.chosen);
    println!("{}", r.describe());
    assert_eq!(r.attempts.len(), 5, "fixed resolver probes 5 bases");
    assert_eq!(r.chosen.as_deref(), Some(appdir.join("bin/slang").as_path()),
               "fixed resolver must CHOOSE <exe_dir>/bin/slang");
    println!("GREEN-OK: fixed resolver chooses the AppDir-root base with no VEDIT_SLANG");
    std::fs::remove_dir_all(appdir.parent().unwrap()).ok();
}
