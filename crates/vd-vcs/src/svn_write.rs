//! Arguments and containment checks for the typed Subversion write operations.

use crate::runner::{CommandRunner, SystemRunner};
use crate::write::{
    redact_url_userinfo, WriteError, WriteInvocation, WriteOp, WriteOutcome, WriteRequest,
};
use std::path::{Component, Path, PathBuf};

fn invalid(message: &str) -> WriteError {
    WriteError::Invalid(message.into())
}

pub(crate) fn scoped_path(root: &Path, path: &str) -> Result<(), WriteError> {
    let relative = Path::new(path);
    if path.trim().is_empty()
        || path.starts_with('-')
        || path.chars().any(char::is_control)
        || !relative
            .components()
            .all(|part| matches!(part, Component::Normal(name) if name != ".svn" && name != ".git"))
    {
        return Err(invalid(
            "expected a relative path inside the SVN working copy",
        ));
    }
    let mut prefix = root.to_path_buf();
    for component in relative.components() {
        prefix.push(component);
        match prefix.symlink_metadata() {
            Ok(metadata) if metadata.is_symlink() => {
                return Err(invalid("SVN operation cannot traverse a symbolic link"));
            }
            Ok(_) => (),
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => break,
            Err(error) => return Err(invalid(&format!("cannot inspect the SVN path: {error}"))),
        }
    }
    Ok(())
}

pub(crate) fn literal_target(value: &str) -> String {
    if value.contains('@') {
        format!("{value}@")
    } else {
        value.to_owned()
    }
}

fn required<'a>(value: Option<&'a str>, label: &str) -> Result<&'a str, WriteError> {
    value
        .filter(|value| !value.trim().is_empty())
        .ok_or_else(|| invalid(&format!("SVN operation requires {label}")))
}

fn label(value: &str) -> Result<&str, WriteError> {
    if value.trim().is_empty() || value.starts_with('-') || value.chars().any(char::is_control) {
        Err(invalid("invalid SVN name or pattern"))
    } else {
        Ok(value)
    }
}

fn repository_relative(value: &str) -> Result<&str, WriteError> {
    let Some(path) = value.strip_prefix("^/") else {
        return Err(invalid(
            "use a repository-relative URL such as ^/branches/topic",
        ));
    };
    if path
        .split('/')
        .any(|part| part.is_empty() || part == "." || part == "..")
        || path.chars().any(|ch| ch.is_whitespace() || ch.is_control())
        || path.contains(['%', '\\', '@', '?', '#'])
    {
        return Err(invalid("invalid repository-relative SVN URL"));
    }
    Ok(value)
}

pub(crate) fn checkout_url(value: &str) -> Result<&str, WriteError> {
    let Some((scheme, location)) = value.split_once("://") else {
        return Err(invalid("enter a full SVN repository URL"));
    };
    if !matches!(scheme, "file" | "https" | "http" | "svn" | "svn+ssh")
        || value
            .chars()
            .any(|ch| ch.is_whitespace() || ch.is_control())
        || value.contains(['\\', '?', '#'])
        || location
            .split('/')
            .next()
            .is_some_and(|host| host.contains(['@', '%']))
        || (scheme == "file" && !location.starts_with('/'))
        || (scheme != "file" && (location.is_empty() || location.starts_with('/')))
    {
        return Err(invalid("invalid SVN URL; embedded credentials are refused"));
    }
    Ok(value)
}

pub(crate) fn invocation(
    request: &WriteRequest,
    root: PathBuf,
) -> Result<WriteInvocation, WriteError> {
    let op = request.op;
    let mut args = vec![op.subcommand().to_owned()];
    let name = request.name.as_deref();
    let target = request.target.as_deref();
    let url = request.url.as_deref();
    let message = request.message.as_deref().map(str::trim);
    if request.author.is_some()
        || request.amend
        || request.set_upstream
        || request.mode.is_some()
        || request.credential_user.is_some()
        || request.credential_password.is_some()
        || (url.is_some() && op != WriteOp::SvnCheckout)
        || (message.is_some() && !matches!(op, WriteOp::SvnCommit | WriteOp::SvnBranchCreate))
        || (name.is_some()
            && !matches!(
                op,
                WriteOp::SvnCommit | WriteOp::SvnChangelist | WriteOp::SvnIgnore
            ))
        || (target.is_some()
            && !matches!(
                op,
                WriteOp::SvnCheckout
                    | WriteOp::SvnBranchCreate
                    | WriteOp::SvnSwitch
                    | WriteOp::SvnMerge
                    | WriteOp::SvnRename
            ))
    {
        return Err(invalid("unsupported parameters for this SVN operation"));
    }
    if op.requires_paths() && request.paths.is_empty() {
        return Err(invalid("select at least one path for this SVN operation"));
    }
    if !request.paths.is_empty()
        && !op.requires_paths()
        && !matches!(op, WriteOp::SvnCommit | WriteOp::SvnUpdate)
    {
        return Err(invalid("this SVN operation does not take selected paths"));
    }
    for path in &request.paths {
        if path != "."
            || !matches!(
                op,
                WriteOp::SvnCommit | WriteOp::SvnRevert | WriteOp::SvnResolve
            )
        {
            scoped_path(&root, path)?;
        }
    }
    if matches!(
        op,
        WriteOp::SvnCommit
            | WriteOp::SvnUpdate
            | WriteOp::SvnCheckout
            | WriteOp::SvnBranchCreate
            | WriteOp::SvnSwitch
            | WriteOp::SvnMerge
    ) {
        args.extend(["--non-interactive".into(), "--no-auth-cache".into()]);
    }
    match op {
        WriteOp::SvnCommit => {
            args.extend(["-m".into(), required(message, "a commit message")?.into()]);
            if let Some(name) = name {
                if !request.paths.is_empty() {
                    return Err(invalid("choose selected paths or a changelist"));
                }
                args.push(format!("--changelist={}", label(name)?));
            }
            if !request.paths.is_empty() {
                args.extend(["--depth".into(), "empty".into()]);
            }
        }
        WriteOp::SvnCheckout => {
            let destination = required(target, "a new checkout directory name")?;
            scoped_path(&root, destination)?;
            if Path::new(destination)
                .file_name()
                .and_then(|part| part.to_str())
                != Some(destination)
                || !matches!(root.join(destination).symlink_metadata(), Err(error) if error.kind() == std::io::ErrorKind::NotFound)
            {
                return Err(invalid(
                    "checkout destination must be a new direct child of the chosen directory",
                ));
            }
            args.extend([
                "--ignore-externals".into(),
                "--".into(),
                literal_target(checkout_url(required(url, "a checkout URL")?)?),
                destination.into(),
            ]);
        }
        WriteOp::SvnBranchCreate => {
            args.extend([
                "-r".into(),
                "HEAD".into(),
                "-m".into(),
                required(message, "a commit message")?.into(),
                "--".into(),
                ".".into(),
                repository_relative(required(target, "a destination URL")?)?.into(),
            ]);
        }
        WriteOp::SvnSwitch | WriteOp::SvnMerge => {
            if op == WriteOp::SvnSwitch {
                args.push("--ignore-externals".into());
            }
            args.extend([
                "--accept".into(),
                "postpone".into(),
                "--".into(),
                repository_relative(required(target, "a source URL")?)?.into(),
                ".".into(),
            ]);
        }
        WriteOp::SvnUpdate => args.push("--ignore-externals".into()),
        WriteOp::SvnChangelist => args.push(label(required(name, "a changelist name")?)?.into()),
        WriteOp::SvnChangelistRemove => args.push("--remove".into()),
        WriteOp::SvnResolve => args.extend([
            "--accept".into(),
            "working".into(),
            "--depth".into(),
            "empty".into(),
        ]),
        WriteOp::SvnRename => {
            let destination = required(target, "a rename destination")?;
            scoped_path(&root, destination)?;
            if request.paths.len() != 1 || root.join(destination).symlink_metadata().is_ok() {
                return Err(invalid("rename requires one source and a new destination"));
            }
            args.extend([
                "--".into(),
                literal_target(&request.paths[0]),
                destination.into(),
            ]);
        }
        WriteOp::SvnIgnore => {
            let [path] = request.paths.as_slice() else {
                return Err(invalid("ignore requires one file or folder"));
            };
            let relative = Path::new(path);
            let parent = relative
                .parent()
                .and_then(Path::to_str)
                .filter(|text| !text.is_empty())
                .unwrap_or(".");
            let pattern = if let Some(pattern) = name {
                label(pattern)?.to_owned()
            } else {
                let basename = relative
                    .file_name()
                    .and_then(|part| part.to_str())
                    .ok_or_else(|| invalid("invalid pathname"))?;
                basename
                    .replace('\\', "\\\\")
                    .replace('[', "\\[")
                    .replace('*', "\\*")
                    .replace('?', "\\?")
            };
            return ignore_invocation(root, parent, &pattern, args);
        }
        WriteOp::SvnCleanup | WriteOp::SvnRevert | WriteOp::SvnAdd | WriteOp::SvnRemove => (),
        _ => return Err(invalid("expected an SVN operation")),
    }
    if !request.paths.is_empty() && op != WriteOp::SvnRename {
        args.push("--".into());
        args.extend(request.paths.iter().map(|path| literal_target(path)));
    }
    Ok(WriteInvocation {
        program: "svn",
        working_tree: root,
        args,
        env: Vec::new(),
    })
}

pub(crate) fn delete_unversioned(
    request: &WriteRequest,
    root: &Path,
) -> Result<WriteOutcome, WriteError> {
    if request.paths.is_empty()
        || request.name.is_some()
        || request.target.is_some()
        || request.url.is_some()
        || request.message.is_some()
        || request.author.is_some()
        || request.mode.is_some()
        || request.credential_user.is_some()
        || request.credential_password.is_some()
        || request.amend
        || request.set_upstream
    {
        return Err(invalid("select explicit unversioned files to delete"));
    }
    let mut files = Vec::new();
    for path in &request.paths {
        scoped_path(root, path)?;
        let file = root.join(path);
        let is_directory = inspect_delete_tree(&file)?;
        if !files.iter().any(|(item, _)| item == &file) {
            files.push((file, is_directory));
        }
    }
    let status = crate::svn_read::status(root, false).map_err(WriteError::Failed)?;
    if request
        .paths
        .iter()
        .any(|path| !status.iter().any(|row| row.path == *path && row.kind == 7))
    {
        return Err(invalid(
            "only unversioned files and directories may be deleted",
        ));
    }
    files.sort_unstable_by_key(|(path, _)| path.components().count());
    let mut removed_dirs: Vec<PathBuf> = Vec::new();
    for (file, is_directory) in files {
        if removed_dirs.iter().any(|parent| file.starts_with(parent)) {
            continue;
        }
        if inspect_delete_tree(&file)? != is_directory {
            return Err(invalid("unversioned path changed during deletion"));
        }
        let result = if is_directory {
            std::fs::remove_dir_all(&file)
        } else {
            std::fs::remove_file(&file)
        };
        result.map_err(|error| {
            WriteError::Failed(format!("cannot delete {}: {error}", file.display()))
        })?;
        if is_directory {
            removed_dirs.push(file);
        }
    }
    Ok(WriteOutcome {
        backend: crate::Backend::Svn,
        op: request.op,
        success: true,
        stdout: String::new(),
        stderr: String::new(),
    })
}

fn inspect_delete_tree(path: &Path) -> Result<bool, WriteError> {
    let metadata = |path: &Path| {
        path.symlink_metadata().map_err(|error| {
            WriteError::Failed(format!("cannot inspect {}: {error}", path.display()))
        })
    };
    let top = metadata(path)?;
    if top.is_file() {
        return Ok(false);
    }
    if !top.is_dir() {
        return Err(invalid("cannot delete a symbolic link or special path"));
    }
    let mut pending = vec![path.to_path_buf()];
    let mut checked = 0;
    #[cfg(unix)]
    use std::os::unix::fs::MetadataExt;
    while let Some(directory) = pending.pop() {
        let entries = directory
            .read_dir()
            .map_err(|error| WriteError::Failed(error.to_string()))?;
        for entry in entries {
            checked += 1;
            if checked > 100_000 {
                return Err(invalid(
                    "unversioned directory exceeds the deletion inspection limit",
                ));
            }
            let entry = entry.map_err(|error| WriteError::Failed(error.to_string()))?;
            if matches!(entry.file_name().to_str(), Some(".svn" | ".git" | ".hg")) {
                return Err(invalid(
                    "unversioned directory contains repository metadata",
                ));
            }
            let path = entry.path();
            let details = metadata(&path)?;
            #[cfg(unix)]
            if details.dev() != top.dev() {
                return Err(invalid("unversioned directory contains a mounted path"));
            }
            if details.is_dir() {
                pending.push(path);
            } else if !details.is_file() {
                return Err(invalid(
                    "unversioned directory contains a symbolic link or special path",
                ));
            }
        }
    }
    Ok(true)
}

fn ignore_invocation(
    root: PathBuf,
    parent: &str,
    pattern: &str,
    mut args: Vec<String>,
) -> Result<WriteInvocation, WriteError> {
    if pattern.contains('/') {
        return Err(invalid(
            "an ignore pattern must apply to a name within its own directory",
        ));
    }
    let parent = literal_target(parent);
    let read = [
        "propget",
        "--no-newline",
        "svn:ignore",
        "--",
        parent.as_str(),
    ]
    .map(str::to_owned);
    let output = SystemRunner
        .run("svn", &root, &read)
        .map_err(WriteError::Failed)?;
    let mut property = if output.success {
        output.stdout
    } else if output.stderr.contains("W200017:") {
        String::new()
    } else {
        return Err(WriteError::Failed(redact_url_userinfo(
            output.stderr.trim(),
        )));
    };
    if !property.lines().any(|line| line == pattern) {
        if !property.is_empty() && !property.ends_with('\n') {
            property.push('\n');
        }
        property.push_str(pattern);
        property.push('\n');
    }
    args.extend(["--".into(), "svn:ignore".into(), property, parent]);
    Ok(WriteInvocation {
        program: "svn",
        working_tree: root,
        args,
        env: Vec::new(),
    })
}
