//! Real host Git and SVN through each supported shell installed in the test environment.

use std::collections::BTreeMap;
use std::os::unix::fs::PermissionsExt;
use std::path::{Path, PathBuf};
use std::process::{Command, Output};
use std::sync::Mutex;

use vd_vcs::host::{bind_shell_preference, set_imported_environment, HostEnvironment};
use vd_vcs::runner::{CommandRunner, SystemRunner};
use vd_vcs::write::{write, WriteOp, WriteRequest};

static SELECTED: Mutex<Option<PathBuf>> = Mutex::new(None);

fn executable(name: &str) -> Option<PathBuf> {
    let path = std::env::var_os("PATH")?;
    std::env::split_paths(&path)
        .map(|dir| dir.join(name))
        .find(|path| {
            path.is_absolute()
                && path.metadata().is_ok_and(|metadata| {
                    metadata.is_file() && metadata.permissions().mode() & 0o111 != 0
                })
        })
}

fn launch(tool: &Path, root: &Path, args: &[&str]) -> Output {
    let output = Command::new(tool)
        .current_dir(root)
        .args(args)
        .output()
        .unwrap_or_else(|error| panic!("fixture process: {error:?}"));
    assert!(
        output.status.success(),
        "{args:?}: {}",
        String::from_utf8_lossy(&output.stderr)
    );
    output
}

fn script(path: &Path, content: &str) {
    std::fs::write(path, content).unwrap_or_else(|error| panic!("script file: {error:?}"));
    std::fs::set_permissions(path, std::fs::Permissions::from_mode(0o755))
        .unwrap_or_else(|error| panic!("script mode: {error:?}"));
}

fn write_request(root: &Path, path: &str, op: WriteOp) -> WriteRequest {
    WriteRequest {
        root: root.to_owned(),
        op,
        paths: vec![path.into()],
        message: None,
        author: None,
        amend: false,
        name: None,
        target: None,
        url: None,
        credential_user: None,
        credential_password: None,
        set_upstream: false,
        mode: None,
    }
}

#[test]
fn read_and_write_preserve_arguments_and_use_the_selected_shell_environment() {
    let git = executable("git").unwrap_or_else(|| panic!("host Git"));
    let svn = executable("svn").zip(executable("svnadmin"));
    let fixture =
        tempfile::tempdir().unwrap_or_else(|error| panic!("host tools fixture: {error:?}"));
    let root = fixture.path();
    let shells = HostEnvironment::capture(root).shells();
    let tools = root.join("test tools");
    std::fs::create_dir(&tools).unwrap_or_else(|error| panic!("tools directory: {error:?}"));
    let appdir_bin = root.join("AppDir/bin");
    std::fs::create_dir_all(&appdir_bin).unwrap_or_else(|error| panic!("fake AppDir: {error:?}"));
    let inherited_path =
        std::env::var("PATH").unwrap_or_else(|error| panic!("test PATH: {error:?}"));
    let path = format!("{}:{}", tools.display(), inherited_path);
    let imported = BTreeMap::from([
        ("PATH".into(), path.clone()),
        ("HOST_TOOLS_EXPECT_PATH".into(), path),
        ("HOST_TOOLS_FIXTURE_MARKER".into(), "from-import".into()),
        ("LD_LIBRARY_PATH".into(), appdir_bin.display().to_string()),
        ("LOCPATH".into(), appdir_bin.display().to_string()),
        ("HOST_TOOLS_REAL_GIT".into(), git.display().to_string()),
        (
            "HOST_TOOLS_REAL_SVN".into(),
            svn.as_ref()
                .map(|(svn, _)| svn.display().to_string())
                .unwrap_or_default(),
        ),
    ]);
    let wrapper = "#!/bin/sh\nset -eu\n[ \"$HOST_TOOLS_SHELL_RAN\" = 1 ]\n[ \"$HOST_TOOLS_FIXTURE_MARKER\" = from-import ]\n[ \"$PATH\" = \"$HOST_TOOLS_EXPECT_PATH\" ]\n[ -z \"${LD_LIBRARY_PATH+x}${LOCPATH+x}\" ]\n";
    script(
        &tools.join("git"),
        &format!("{wrapper}exec \"$HOST_TOOLS_REAL_GIT\" \"$@\"\n"),
    );
    script(
        &tools.join("svn"),
        &format!("{wrapper}exec \"$HOST_TOOLS_REAL_SVN\" \"$@\"\n"),
    );
    bind_shell_preference(|| {
        SELECTED
            .lock()
            .unwrap_or_else(|error| panic!("test selection: {error:?}"))
            .clone()
    });
    let mut tested = 0;
    for family in [
        "bash", "csh", "tcsh", "zsh", "fish", "dash", "ash", "ksh", "ksh93", "mksh",
    ] {
        let Some(native) = shells
            .iter()
            .find(|path| path.file_name().is_some_and(|name| name == family))
        else {
            continue;
        };
        let shell = tools.join(family);
        script(
            &shell,
            "#!/bin/sh\nexport HOST_TOOLS_SHELL_RAN=1\nexec \"$HOST_TOOLS_NATIVE_SHELL\" \"$@\"\n",
        );
        let mut imported = imported.clone();
        imported.insert(
            "HOST_TOOLS_NATIVE_SHELL".into(),
            native.display().to_string(),
        );
        set_imported_environment(imported);
        *SELECTED
            .lock()
            .unwrap_or_else(|error| panic!("test selection: {error:?}")) = Some(shell);
        let repo = root.join(format!("git-{family}"));
        std::fs::create_dir(&repo).unwrap_or_else(|error| panic!("Git checkout: {error:?}"));
        launch(&git, &repo, &["init", "-q"]);
        let name = "RTL $() ; & * ? [x] ' ! `file`.sv";
        std::fs::write(repo.join(name), "module top; endmodule\n")
            .unwrap_or_else(|error| panic!("Git fixture file: {error:?}"));
        let staged = write(&write_request(&repo, name, WriteOp::GitStage))
            .unwrap_or_else(|error| panic!("Git stage job: {error:?}"));
        assert!(staged.success, "{family}: {}", staged.stderr);
        let read = SystemRunner
            .run("git", &repo, &["ls-files".into(), "-z".into()])
            .unwrap_or_else(|error| panic!("Git read job: {error:?}"));
        assert!(read.success, "{family}: {}", read.stderr);
        assert_eq!(read.stdout, format!("{name}\0"), "Git via {family}");
        if let Some((svn, admin)) = &svn {
            let repository = root.join(format!("svn-{family}"));
            let wc = root.join(format!("wc {family}"));
            launch(
                admin,
                root,
                &[
                    "create",
                    repository
                        .to_str()
                        .unwrap_or_else(|| panic!("fixture path")),
                ],
            );
            let url = format!("file://{}", repository.display());
            launch(
                svn,
                root,
                &[
                    "checkout",
                    "-q",
                    &url,
                    wc.to_str().unwrap_or_else(|| panic!("fixture path")),
                ],
            );
            std::fs::write(wc.join(name), "module top; endmodule\n")
                .unwrap_or_else(|error| panic!("SVN fixture file: {error:?}"));
            let added = write(&write_request(&wc, name, WriteOp::SvnAdd))
                .unwrap_or_else(|error| panic!("SVN add job: {error:?}"));
            assert!(added.success, "{family}: {}", added.stderr);
            let read = SystemRunner
                .run("svn", &wc, &["status".into(), "--".into(), name.into()])
                .unwrap_or_else(|error| panic!("SVN read job: {error:?}"));
            assert!(read.success, "{family}: {}", read.stderr);
            assert!(
                read.stdout.starts_with('A') && read.stdout.trim_end().ends_with(name),
                "SVN via {family}"
            );
        }
        eprintln!(
            "HOST-TOOLS shell={family} git=pass svn={}",
            if svn.is_some() { "pass" } else { "skip" }
        );
        tested += 1;
    }
    assert!(tested > 0, "no native shell available");
    *SELECTED
        .lock()
        .unwrap_or_else(|error| panic!("test selection: {error:?}")) =
        Some(root.join("missing/bash"));
    let error = SystemRunner
        .run("git", root, &["status".into()])
        .err()
        .unwrap_or_else(|| panic!("invalid manual shell"));
    assert!(error.contains("Preferences") && error.contains("missing/bash"));
    *SELECTED
        .lock()
        .unwrap_or_else(|error| panic!("test selection: {error:?}")) = None;
    set_imported_environment(BTreeMap::from([
        ("PATH".into(), String::new()),
        ("VEDIT_GIT".into(), String::new()),
        ("VEDIT_SVN".into(), String::new()),
    ]));
    let error = SystemRunner
        .run("git", root, &["status".into()])
        .err()
        .unwrap_or_else(|| panic!("missing Git"));
    assert!(error.contains("Install git") && error.contains("VEDIT_GIT"));
    let error = SystemRunner
        .run("svn", root, &["status".into()])
        .err()
        .unwrap_or_else(|| panic!("missing SVN"));
    assert!(error.contains("Install svn") && error.contains("VEDIT_SVN"));
}
