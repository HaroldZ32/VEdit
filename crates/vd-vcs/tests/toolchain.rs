//! The same resolver feeds the VCS runners and the startup diagnostic.

use std::collections::BTreeMap;
use std::ffi::{OsStr, OsString};
use std::os::unix::fs::{symlink, PermissionsExt};
use std::path::Path;

use vd_vcs::host::HostEnvironment;
use vd_vcs::toolchain::{resolve_in, ToolSource};

fn write_executable(path: &Path) {
    std::fs::create_dir_all(path.parent().unwrap_or_else(|| panic!("fixture parent")))
        .unwrap_or_else(|error| panic!("fixture directory: {error:?}"));
    std::fs::write(path, "#!/bin/sh\nexit 0\n")
        .unwrap_or_else(|error| panic!("fixture tool: {error:?}"));
    std::fs::set_permissions(path, std::fs::Permissions::from_mode(0o755))
        .unwrap_or_else(|error| panic!("fixture permissions: {error:?}"));
}

#[test]
fn effective_path_overrides_host_path_but_cannot_restore_appdir_tools() {
    let fixture =
        tempfile::tempdir().unwrap_or_else(|error| panic!("fixture directory: {error:?}"));
    let root = fixture.path();
    let appdir = root.join("AppDir");
    let appbin = appdir.join("bin");
    let legacy = appdir.join("usr/lib/vcs/bin");
    let hostbin = root.join("host bin");
    let imported_bin = root.join("imported bin");
    let alias = root.join("alias");
    let tool_alias = root.join("symlink bin");
    for directory in [&appbin, &legacy, &hostbin, &imported_bin] {
        write_executable(&directory.join("git"));
    }
    symlink(&appbin, &alias).unwrap_or_else(|error| panic!("AppDir alias: {error:?}"));
    std::fs::create_dir(&tool_alias)
        .unwrap_or_else(|error| panic!("tool alias directory: {error:?}"));
    symlink(legacy.join("git"), tool_alias.join("git"))
        .unwrap_or_else(|error| panic!("legacy tool alias: {error:?}"));
    let launch = BTreeMap::from([
        ("APPDIR".into(), appdir.into_os_string()),
        ("VEDIT_HOST_PATH".into(), hostbin.clone().into_os_string()),
        ("PATH".into(), appbin.clone().into_os_string()),
        ("LD_LIBRARY_PATH".into(), OsString::from("packaged-loader")),
    ]);
    let host = HostEnvironment::new(launch.clone(), &BTreeMap::new(), root);
    assert_eq!(
        resolve_in("git", &host)
            .unwrap_or_else(|| panic!("host Git"))
            .path,
        hostbin.join("git")
    );

    let imported_path = std::env::join_paths([
        appbin,
        alias,
        legacy,
        tool_alias.clone(),
        imported_bin.clone(),
    ])
    .unwrap_or_else(|error| panic!("imported PATH: {error:?}"))
    .into_string()
    .unwrap_or_else(|error| panic!("UTF-8 fixture: {error:?}"));
    let imported = BTreeMap::from([
        ("PATH".into(), imported_path),
        ("LD_LIBRARY_PATH".into(), "imported-loader".into()),
        ("LOCPATH".into(), "imported-locale".into()),
        ("VEDIT_TEST_ENV".into(), "fixture-value".into()),
    ]);
    let host = HostEnvironment::new(launch, &imported, root);
    let chosen = resolve_in("git", &host).unwrap_or_else(|| panic!("imported Git"));
    assert_eq!(chosen.path, imported_bin.join("git"));
    assert_eq!(chosen.source, ToolSource::Path);
    let env = host.variables();
    assert_eq!(
        env.get(OsStr::new("VEDIT_TEST_ENV")),
        Some(&"fixture-value".into())
    );
    assert_eq!(
        env.get(OsStr::new("PATH")),
        Some(
            &std::env::join_paths([tool_alias, imported_bin])
                .unwrap_or_else(|error| panic!("filtered PATH: {error:?}"))
        )
    );
    for name in ["LD_LIBRARY_PATH", "LOCPATH", "VEDIT_HOST_PATH"] {
        assert!(!env.contains_key(OsStr::new(name)), "{name}");
    }
}

#[test]
fn skips_nonexecutables_and_reports_removed_tools() {
    let fixture =
        tempfile::tempdir().unwrap_or_else(|error| panic!("fixture directory: {error:?}"));
    let root = fixture.path();
    std::fs::write(root.join("svn"), "not executable")
        .unwrap_or_else(|error| panic!("plain file: {error:?}"));
    let appdir = root.join("AppDir");
    let old_tool = appdir.join("bin/svn");
    let fallback = root.join("other/svn");
    write_executable(&old_tool);
    write_executable(&fallback);
    let launch = BTreeMap::from([
        ("APPDIR".into(), appdir.into_os_string()),
        ("PATH".into(), root.as_os_str().to_owned()),
        ("VEDIT_SVN".into(), fallback.clone().into_os_string()),
    ]);
    let host = HostEnvironment::new(launch.clone(), &BTreeMap::new(), root);
    assert_eq!(
        resolve_in("svn", &host)
            .unwrap_or_else(|| panic!("fallback SVN"))
            .source,
        ToolSource::Env
    );
    std::fs::remove_file(fallback).unwrap_or_else(|error| panic!("remove fallback: {error:?}"));
    assert!(resolve_in("svn", &host).is_none());
    let host = HostEnvironment::new(
        launch,
        &BTreeMap::from([("VEDIT_SVN".into(), old_tool.to_string_lossy().into_owned())]),
        root,
    );
    assert!(resolve_in("svn", &host).is_none());
    assert!(resolve_in("unknown-tool", &host).is_none());
}

#[test]
fn an_empty_saved_host_path_does_not_fall_back_to_the_launcher_path() {
    let fixture =
        tempfile::tempdir().unwrap_or_else(|error| panic!("fixture directory: {error:?}"));
    let root = fixture.path();
    write_executable(&root.join("git"));
    let host = HostEnvironment::new(
        BTreeMap::from([
            ("PATH".into(), root.as_os_str().to_owned()),
            ("VEDIT_HOST_PATH".into(), OsString::new()),
        ]),
        &BTreeMap::new(),
        root,
    );
    assert!(resolve_in("git", &host).is_none());
    assert_eq!(
        host.variables().get(OsStr::new("PATH")),
        Some(&OsString::new())
    );
}
