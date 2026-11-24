//! Change A of `.mpd/plans/filelist-file-set-and-duplicate-module.md`:
//! the project file set is decided by the DECLARED filelists only.
//!
//! Acceptance clauses covered here (one test each):
//!
//! * **A-1** `declared_filelists_alone_decide_the_file_set`: the checked-in
//!   `tests/fixtures/dup-basename` fixture (its `design.f` names only
//!   `rtl/uart.v` while `sim/uart.v` is an undeclared same-basename source)
//!   expands to `rtl/uart.v` alone.
//! * **A-2** `the_switch_restores_the_legacy_scan_and_auto_discovery`: the
//!   explicit `[project] auto_discover = true` switch returns the same fixture
//!   to the pre-decision behaviour — 2 files, `rtl/uart.v` first.
//! * **A-3** `an_undeclared_root_filelist_is_ignored`: a root-level `b.f` does
//!   not join the file set when only `a.f` was imported.
//! * **A-4** `configured_filelists_always_apply`: a `[project] filelists`
//!   entry works in the default state and keeps working with the switch on.
//!
//! The fixture is never modified in place: A-2 needs the switch written to a
//! `vedit.toml`, so the fixture is copied into a temporary root first and the
//! checked-in fixture stays the default-state (switch absent) case A-1 pins.

use std::path::{Path, PathBuf};

/// The checked-in duplicate-basename fixture.
fn fixture_root() -> PathBuf {
    let path = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../tests/fixtures/dup-basename");
    std::fs::canonicalize(&path).unwrap_or_else(|error| panic!("{}: {error}", path.display()))
}

fn write(path: &Path, text: &str) -> std::io::Result<()> {
    if let Some(parent) = path.parent() {
        std::fs::create_dir_all(parent)?;
    }
    std::fs::write(path, text)
}

/// Copy one fixture tree (files + directories) into `target`.
fn copy_tree(source: &Path, target: &Path) -> std::io::Result<()> {
    std::fs::create_dir_all(target)?;
    for entry in std::fs::read_dir(source)? {
        let entry = entry?;
        let destination = target.join(entry.file_name());
        let kind = entry.file_type()?;
        if kind.is_dir() {
            copy_tree(&entry.path(), &destination)?;
        } else if kind.is_file() {
            std::fs::copy(entry.path(), &destination)?;
        }
    }
    Ok(())
}

/// The expanded file set, relative to `root`, with `/` separators.
fn relative_files(root: &Path, files: &[PathBuf]) -> Vec<String> {
    files
        .iter()
        .map(|file| {
            file.strip_prefix(root)
                .unwrap_or(file)
                .to_string_lossy()
                .replace('\\', "/")
        })
        .collect()
}

/// A-1: the imported `design.f` names `rtl/uart.v`; the same-basename
/// `sim/uart.v` is NOT in the file set (this was the reported defect: the
/// interface panel read the undeclared twin's ports).
#[test]
fn declared_filelists_alone_decide_the_file_set() -> Result<(), Box<dyn std::error::Error>> {
    let root = fixture_root();
    let expansion = vd_config::project::expand(&root, &[root.join("design.f")], &[], &[])?;

    assert_eq!(
        relative_files(&root, &expansion.files),
        vec!["rtl/uart.v"],
        "default state: only the imported filelist's entry is a project file"
    );
    assert_eq!(
        expansion.files.len(),
        1,
        "one declared file, nothing scanned"
    );
    assert_eq!(
        expansion.filelists.len(),
        1,
        "only the imported filelist contributes: {:?}",
        expansion.filelists
    );
    Ok(())
}

/// A-2: `[project] auto_discover = true` restores BOTH legacy behaviours on the
/// same fixture — the root-level `.f` auto-discovery and the recursive scan —
/// so the old 2-file set comes back with the filelist order first.
#[test]
fn the_switch_restores_the_legacy_scan_and_auto_discovery() -> Result<(), Box<dyn std::error::Error>>
{
    let temp = tempfile::tempdir()?;
    let root = temp.path().join("dup-basename");
    copy_tree(&fixture_root(), &root)?;
    write(
        &root.join("vedit.toml"),
        "[project]\nauto_discover = true\n",
    )?;

    // No imported filelist at all: the switch re-enables root-level `.f`
    // auto-discovery, so `design.f` is found by the directory listing.
    let discovered = vd_config::project::expand(&root, &[], &[], &[])?;
    assert_eq!(
        discovered.filelists.len(),
        1,
        "the switch must restore root-level `.f` auto-discovery: {:?}",
        discovered.filelists
    );
    assert!(
        discovered.filelists[0].path.ends_with("design.f"),
        "the auto-discovered list is the root's filelist: {:?}",
        discovered.filelists
    );

    // The same fixture with the filelist imported: the scan is back too, so the
    // undeclared `sim/uart.v` is part of the legacy file set — filelist order
    // first, the sorted scan after it.
    let expansion = vd_config::project::expand(&root, &[root.join("design.f")], &[], &[])?;
    assert_eq!(
        relative_files(&root, &expansion.files),
        vec!["rtl/uart.v", "sim/uart.v"],
        "switch on: the old file set is reproducible"
    );
    Ok(())
}

/// A-3: a `.f` sitting in the root that was NOT imported (and is not in
/// `[project] filelists`) does not contribute, even though its entries are
/// valid sources. Only `a.f` was imported.
#[test]
fn an_undeclared_root_filelist_is_ignored() -> Result<(), Box<dyn std::error::Error>> {
    let temp = tempfile::tempdir()?;
    let root = temp.path();
    write(&root.join("a.f"), "rtl/uart.v\n")?;
    write(&root.join("b.f"), "sim/uart.v\n")?;
    write(
        &root.join("rtl/uart.v"),
        "module uart (input clk, output done);\nendmodule\n",
    )?;
    write(
        &root.join("sim/uart.v"),
        "module uart (input clk, output done);\nendmodule\n",
    )?;

    let expansion = vd_config::project::expand(root, &[root.join("a.f")], &[], &[])?;
    assert_eq!(
        relative_files(root, &expansion.files),
        vec!["rtl/uart.v"],
        "the sibling `b.f` must stay out of the project"
    );
    assert!(
        expansion
            .filelists
            .iter()
            .all(|filelist| !filelist.path.ends_with("b.f")),
        "`b.f` was never declared: {:?}",
        expansion.filelists
    );
    Ok(())
}

/// A-4: `[project] filelists` is the user's written declaration, so it applies
/// with the switch OFF (the default) and keeps applying with it ON. A stray
/// root-level `.f` is still ignored in the default state.
#[test]
fn configured_filelists_always_apply() -> Result<(), Box<dyn std::error::Error>> {
    let temp = tempfile::tempdir()?;
    let root = temp.path();
    write(
        &root.join("vedit.toml"),
        "[project]\nfilelists = [\"design.f\"]\n",
    )?;
    write(&root.join("design.f"), "rtl/uart.v\n")?;
    write(&root.join("stray.f"), "sim/uart.v\n")?;
    write(
        &root.join("rtl/uart.v"),
        "module uart (input clk, output done);\nendmodule\n",
    )?;
    write(
        &root.join("sim/uart.v"),
        "module uart (input clk, output done);\nendmodule\n",
    )?;

    let by_default = vd_config::project::expand(root, &[], &[], &[])?;
    assert_eq!(
        relative_files(root, &by_default.files),
        vec!["rtl/uart.v"],
        "the configured filelist applies in the default state; `stray.f` does not"
    );
    assert!(
        by_default
            .filelists
            .iter()
            .any(|filelist| filelist.path.ends_with("design.f")),
        "the configured filelist must be the contributing one: {:?}",
        by_default.filelists
    );

    // Same declaration with the legacy switch on: it still applies (and the
    // stray root-level `.f` now joins, because that is what the switch means).
    write(
        &root.join("vedit.toml"),
        "[project]\nfilelists = [\"design.f\"]\nauto_discover = true\n",
    )?;
    let with_switch = vd_config::project::expand(root, &[], &[], &[])?;
    assert!(
        with_switch
            .filelists
            .iter()
            .any(|filelist| filelist.path.ends_with("design.f")),
        "the configured filelist applies with the switch on too: {:?}",
        with_switch.filelists
    );
    assert_eq!(
        relative_files(root, &with_switch.files),
        vec!["rtl/uart.v", "sim/uart.v"],
        "switch on: `design.f` first, then the auto-discovered `stray.f`"
    );
    Ok(())
}
