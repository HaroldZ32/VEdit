//! T10 seam 2 (AC-4.7 / AC-T10.2): the `.f` filelist variable table.
//!
//! Resolution order, most specific first:
//!
//! 1. the project's own variable table (the `NAME=VALUE` list the UI persists);
//! 2. the imported environment selection — its PROJECT layer beats its GLOBAL
//!    layer (that merge is [`EnvironmentSelection::merged_for`], the layer the
//!    product hands over through `vd_ffi::env::apply_environment_selection`);
//! 3. the process environment.
//!
//! Both the bare `$VAR` and the `${VAR}` forms are covered, and the values are
//! consumed exactly like the filelist does it: a bare entry, a `+incdir+` and a
//! `+define+`.

use std::fs;
use std::path::Path;

use vd_config::env::EnvironmentSelection;
use vd_config::project::{expand_with_variables, Define, Variables};

fn write(path: &Path, text: &str) {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)
            .unwrap_or_else(|error| panic!("mkdir {}: {error}", parent.display()));
    }
    fs::write(path, text).unwrap_or_else(|error| panic!("write {}: {error}", path.display()));
}

fn mkdir(path: &Path) {
    fs::create_dir_all(path).unwrap_or_else(|error| panic!("mkdir {}: {error}", path.display()));
}

/// One imported layer entry, as the merge produced it.
fn value_of(entries: &[(String, String)], name: &str) -> Option<String> {
    entries
        .iter()
        .find(|(key, _)| key == name)
        .map(|(_, value)| value.clone())
}

#[test]
fn filelist_variables_resolve_project_then_imported_then_process() {
    let temp = tempfile::tempdir().unwrap_or_else(|error| panic!("tempdir: {error}"));
    let root = temp.path();
    // The candidate targets. One per layer, so a wrong winner is visible.
    for directory in ["proj_inc", "proj_selection_inc", "glob_inc", "proc_inc"] {
        mkdir(&root.join(directory));
    }
    for file in ["proj_file.sv", "glob_file.sv", "proc_file.sv"] {
        write(&root.join(file), "// candidate\n");
    }
    write(
        &root.join("seam.f"),
        "${SEAM_FILE}\n\
         +incdir+$SEAM_DIR\n\
         +incdir+${SEAM_IMPORTED}\n\
         +incdir+${SEAM_SELECTED}\n\
         +incdir+${SEAM_PROC}\n\
         +define+$SEAM_DEFINE\n",
    );

    // The imported selection: a global layer plus THIS project's own layer.
    let mut selection = EnvironmentSelection {
        global: vec![
            ("SEAM_FILE".to_string(), "glob_file.sv".to_string()),
            ("SEAM_DIR".to_string(), "glob_inc".to_string()),
            ("SEAM_IMPORTED".to_string(), "glob_inc".to_string()),
            ("SEAM_SELECTED".to_string(), "glob_inc".to_string()),
            ("SEAM_DEFINE".to_string(), "GLOBAL=1".to_string()),
        ],
        ..EnvironmentSelection::default()
    };
    selection.projects.insert(
        root.display().to_string(),
        vec![(
            "SEAM_SELECTED".to_string(),
            "proj_selection_inc".to_string(),
        )],
    );
    let imported = selection.merged_for(Some(root));
    assert_eq!(
        value_of(&imported, "SEAM_SELECTED").as_deref(),
        Some("proj_selection_inc"),
        "the selection's PROJECT layer must beat its GLOBAL layer: {imported:?}"
    );

    // The project's own table and the process environment, same names where
    // possible so every layer is exercised on ONE variable.
    let project_table = vec![
        ("SEAM_FILE".to_string(), "proj_file.sv".to_string()),
        ("SEAM_DIR".to_string(), "proj_inc".to_string()),
        ("SEAM_DEFINE".to_string(), "PROJ=1".to_string()),
    ];
    let environment = vec![
        ("SEAM_FILE".to_string(), "proc_file.sv".to_string()),
        ("SEAM_DIR".to_string(), "proc_inc".to_string()),
        ("SEAM_IMPORTED".to_string(), "proc_inc".to_string()),
        ("SEAM_SELECTED".to_string(), "proc_inc".to_string()),
        ("SEAM_PROC".to_string(), "proc_inc".to_string()),
        ("SEAM_DEFINE".to_string(), "PROC=1".to_string()),
    ];
    let variables = Variables::with_layers(&project_table, imported, environment, None);

    // The same lookup the filelist preprocessor performs.
    assert_eq!(
        variables.resolve("SEAM_FILE"),
        Some("proj_file.sv"),
        "the project table beats the imported selection and the process"
    );
    assert_eq!(
        variables.resolve("SEAM_DIR"),
        Some("proj_inc"),
        "the project table beats the imported selection and the process"
    );
    assert_eq!(
        variables.resolve("SEAM_IMPORTED"),
        Some("glob_inc"),
        "the imported layer beats the process environment"
    );
    assert_eq!(
        variables.resolve("SEAM_PROC"),
        Some("proc_inc"),
        "a name only the process has still resolves (the environment is not dropped)"
    );

    // CONTRACT CHANGE (2026-11-18 file-set decision, A-5): explicit import (a
    // root-level `.f` is no longer auto-discovered).
    let expansion = expand_with_variables(root, &[root.join("seam.f")], &[], &[], &variables)
        .unwrap_or_else(|error| panic!("expand failed: {error}"));
    println!(
        "T10-SEAM2 filelists={:?} warnings={:?} missing={:?}",
        expansion.filelists, expansion.warnings, expansion.missing_variables
    );
    let filelist = expansion
        .filelists
        .first()
        .unwrap_or_else(|| panic!("no filelist discovered: {:?}", expansion.filelists));
    assert_eq!(
        filelist.entries,
        vec![root.join("proj_file.sv")],
        "`${{SEAM_FILE}}` resolves through all three layers"
    );
    assert_eq!(
        filelist.include_dirs,
        vec![
            root.join("proj_inc"),
            root.join("glob_inc"),
            root.join("proj_selection_inc"),
            root.join("proc_inc"),
        ],
        "each +incdir+ resolves at its own layer (project table, selection global, selection project, process)"
    );
    assert_eq!(
        filelist.defines,
        vec![Define {
            name: "PROJ".to_string(),
            value: Some("1".to_string()),
        }],
        "`+define+$VAR` resolves from the project table (it beats the other two layers)"
    );
    assert!(
        expansion.missing_variables.is_empty(),
        "nothing is silently unresolved: {:?}",
        expansion.missing_variables
    );
}

#[test]
fn an_unresolved_variable_is_named_and_its_line_is_skipped() {
    let temp = tempfile::tempdir().unwrap_or_else(|error| panic!("tempdir: {error}"));
    let root = temp.path();
    write(&root.join("top.sv"), "module top; endmodule\n");
    write(&root.join("seam.f"), "${SEAM_ABSENT}\n+incdir+inc\n");

    // Explicit three layers: the name exists in NONE of them (no process read).
    let variables = Variables::with_layers(&[], Vec::new(), Vec::new(), None);
    // CONTRACT CHANGE (2026-11-18 file-set decision, A-5): explicit import (a
    // root-level `.f` is no longer auto-discovered).
    let expansion = expand_with_variables(root, &[root.join("seam.f")], &[], &[], &variables)
        .unwrap_or_else(|error| panic!("expand failed: {error}"));
    println!(
        "T10-SEAM2 negative missing={:?} warnings={:?}",
        expansion.missing_variables, expansion.warnings
    );
    assert_eq!(
        expansion.missing_variables,
        vec!["SEAM_ABSENT".to_string()],
        "the unresolved name is reported to the one-dialog collection"
    );
    let filelist = expansion
        .filelists
        .first()
        .unwrap_or_else(|| panic!("no filelist discovered: {:?}", expansion.filelists));
    assert!(
        filelist.entries.is_empty(),
        "the line is skipped, never resolved to a literal `$VAR`: {:?}",
        filelist.entries
    );
    assert!(
        expansion.warnings.iter().any(|warning| warning
            .message
            .contains("unresolved variable `$SEAM_ABSENT`")),
        "the warning names the variable: {:?}",
        expansion.warnings
    );
}
