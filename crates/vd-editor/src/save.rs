//! Atomic file save with a fault-injection seam.
//!
//! The save is atomic in the usual POSIX sense: the data is written to a
//! temporary file **in the same directory** (so the final rename cannot cross a
//! file-system boundary), fsynced, then renamed over the target; the directory
//! is fsynced afterwards. On any failure the original file is left untouched and
//! the temporary file is removed.
//!
//! [`SaveHooks`] exists so the tests can inject the three failure classes the
//! contract asks about — a failure while the temporary file is being written, a
//! crash before the rename, and a rename that fails the way a read-only target
//! does (`PermissionDenied`). Production code always uses [`SaveHooks::none`].

use std::ffi::OsStr;
use std::fs::{self, File, OpenOptions};
use std::io::{self, Write};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::SystemTime;

/// A fault-injection callback receiving the temporary file path.
pub type SaveHook<'a> = &'a dyn Fn(&Path) -> io::Result<()>;

/// Fault-injection hooks for tests. `None` means "no injected fault".
#[derive(Default, Clone, Copy)]
pub struct SaveHooks<'a> {
    /// Called after the temporary file was created, before the contents are
    /// written. A hook that fails (or writes junk into the temp file and then
    /// fails) simulates a failure in the middle of the write.
    pub during_temp_write: Option<SaveHook<'a>>,
    /// Called after the temporary file is complete and synced, immediately
    /// before the rename. A failing hook simulates a crash before the atomic
    /// swap, including a `PermissionDenied` rename (read-only target class).
    pub before_rename: Option<SaveHook<'a>>,
}

impl<'a> SaveHooks<'a> {
    /// No injected fault (production path).
    #[must_use]
    pub const fn none() -> Self {
        Self {
            during_temp_write: None,
            before_rename: None,
        }
    }
}

/// [`save_atomic`] with a fault-injection seam (tests only in practice).
///
/// # Errors
/// Returns the I/O error of the failed step; the original file is untouched and
/// the temporary file is removed.
pub(crate) fn save_atomic_with(
    path: &Path,
    contents: &[u8],
    hooks: SaveHooks<'_>,
) -> io::Result<()> {
    let parent = normalised_parent(path)?;
    let file_name = path.file_name().ok_or_else(|| {
        io::Error::new(
            io::ErrorKind::InvalidInput,
            "target path has no file name component",
        )
    })?;
    let temp_path = unique_temp_path(parent, file_name)?;

    match write_and_rename(&temp_path, path, contents, hooks) {
        Ok(()) => Ok(()),
        Err(error) => {
            match fs::remove_file(&temp_path) {
                Ok(()) => {}
                Err(cleanup) if cleanup.kind() == io::ErrorKind::NotFound => {}
                Err(cleanup) => {
                    return Err(io::Error::new(
                        error.kind(),
                        format!(
                            "save failed ({error}) and cleaning up {} failed ({cleanup})",
                            temp_path.display()
                        ),
                    ));
                }
            }
            Err(error)
        }
    }
}

fn normalised_parent(path: &Path) -> io::Result<&Path> {
    let parent = path.parent().ok_or_else(|| {
        io::Error::new(
            io::ErrorKind::InvalidInput,
            "target path has no parent directory",
        )
    })?;
    Ok(if parent.as_os_str().is_empty() {
        Path::new(".")
    } else {
        parent
    })
}

fn write_and_rename(
    temp_path: &Path,
    target: &Path,
    contents: &[u8],
    hooks: SaveHooks<'_>,
) -> io::Result<()> {
    let mut file = OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(temp_path)?;
    if let Some(hook) = hooks.during_temp_write {
        hook(temp_path)?;
    }
    file.write_all(contents)?;
    file.sync_all()?;
    drop(file);

    if let Some(hook) = hooks.before_rename {
        hook(temp_path)?;
    }
    fs::rename(temp_path, target)?;

    // Durability of the rename itself: fsync the directory containing the file.
    File::open(normalised_parent(target)?)?.sync_all()?;
    Ok(())
}

fn unique_temp_path(parent: &Path, file_name: &OsStr) -> io::Result<PathBuf> {
    static COUNTER: AtomicU64 = AtomicU64::new(0);
    let nanos = SystemTime::now()
        .duration_since(SystemTime::UNIX_EPOCH)
        .map_err(|error| io::Error::other(format!("system clock before UNIX epoch: {error}")))?
        .as_nanos();
    let counter = COUNTER.fetch_add(1, Ordering::Relaxed);
    let pid = std::process::id();
    Ok(parent.join(format!(
        ".{}.vedit-tmp.{pid}.{nanos}.{counter}",
        file_name.to_string_lossy()
    )))
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;
    use std::sync::atomic::{AtomicU64, Ordering};

    static TEST_DIRS: AtomicU64 = AtomicU64::new(0);

    struct TestDir(PathBuf);

    impl TestDir {
        fn new(label: &str) -> Self {
            let serial = TEST_DIRS.fetch_add(1, Ordering::Relaxed);
            let path = std::env::temp_dir().join(format!(
                "vd-editor-save-{}-{serial}-{label}",
                std::process::id()
            ));
            fs::create_dir_all(&path)
                .unwrap_or_else(|error| panic!("creating {} failed: {error}", path.display()));
            Self(path)
        }

        fn file(&self, name: &str) -> PathBuf {
            self.0.join(name)
        }
    }

    impl Drop for TestDir {
        fn drop(&mut self) {
            if let Err(error) = fs::remove_dir_all(&self.0) {
                eprintln!("WARN: could not remove {}: {error}", self.0.display());
            }
        }
    }

    fn leftovers(dir: &Path) -> Vec<String> {
        fs::read_dir(dir)
            .map(|entries| {
                entries
                    .filter_map(|entry| entry.ok())
                    .map(|entry| entry.file_name().to_string_lossy().into_owned())
                    .filter(|name| name.contains(".vedit-tmp."))
                    .collect()
            })
            .unwrap_or_default()
    }

    #[test]
    fn writes_a_new_file() {
        let dir = TestDir::new("new");
        let target = dir.file("a.sv");
        save_atomic_with(&target, b"module a;\nendmodule\n", SaveHooks::none())
            .unwrap_or_else(|error| panic!("save failed: {error}"));
        assert_eq!(
            fs::read_to_string(&target).unwrap_or_default(),
            "module a;\nendmodule\n"
        );
        assert!(leftovers(&dir.0).is_empty());
    }

    #[test]
    fn replaces_existing_content() {
        let dir = TestDir::new("replace");
        let target = dir.file("a.sv");
        fs::write(&target, "old").unwrap_or_else(|error| panic!("write failed: {error}"));
        save_atomic_with(&target, b"new", SaveHooks::none())
            .unwrap_or_else(|error| panic!("save failed: {error}"));
        assert_eq!(fs::read_to_string(&target).unwrap_or_default(), "new");
        assert!(leftovers(&dir.0).is_empty());
    }

    #[test]
    fn temp_file_lives_in_the_target_directory() {
        let dir = TestDir::new("same-dir");
        let target = dir.file("a.sv");
        let observed = std::cell::RefCell::new(None);
        let hook = |temp: &Path| {
            *observed.borrow_mut() = Some(temp.parent().map(Path::to_path_buf));
            Ok(())
        };
        let hooks = SaveHooks {
            during_temp_write: Some(&hook),
            before_rename: None,
        };
        save_atomic_with(&target, b"x", hooks)
            .unwrap_or_else(|error| panic!("save failed: {error}"));
        assert_eq!(observed.into_inner(), Some(Some(dir.0.clone())));
    }

    #[test]
    fn failure_while_writing_the_temp_file_keeps_the_original() {
        let dir = TestDir::new("mid-write");
        let target = dir.file("a.sv");
        fs::write(&target, "original").unwrap_or_else(|error| panic!("write failed: {error}"));
        let hook = |temp: &Path| {
            // Simulate a partial write that then fails.
            fs::write(temp, "half").unwrap_or_else(|error| panic!("partial write failed: {error}"));
            Err(io::Error::other("injected failure during temp write"))
        };
        let hooks = SaveHooks {
            during_temp_write: Some(&hook),
            before_rename: None,
        };
        let result = save_atomic_with(&target, b"replacement", hooks);
        assert!(result.is_err(), "save must fail");
        assert_eq!(fs::read_to_string(&target).unwrap_or_default(), "original");
        assert!(
            leftovers(&dir.0).is_empty(),
            "temporary file was not removed"
        );
    }

    #[test]
    fn crash_before_rename_keeps_the_original() {
        let dir = TestDir::new("before-rename");
        let target = dir.file("a.sv");
        fs::write(&target, "original").unwrap_or_else(|error| panic!("write failed: {error}"));
        let hook = |_temp: &Path| Err(io::Error::other("injected crash before rename"));
        let hooks = SaveHooks {
            during_temp_write: None,
            before_rename: Some(&hook),
        };
        let result = save_atomic_with(&target, b"replacement", hooks);
        assert!(result.is_err(), "save must fail");
        assert_eq!(fs::read_to_string(&target).unwrap_or_default(), "original");
        assert!(
            leftovers(&dir.0).is_empty(),
            "temporary file was not removed"
        );
    }

    #[test]
    fn permission_denied_at_rename_keeps_the_original() {
        let dir = TestDir::new("read-only-target");
        let target = dir.file("a.sv");
        fs::write(&target, "original").unwrap_or_else(|error| panic!("write failed: {error}"));
        // A read-only target file cannot be replaced on a real file system; the
        // same error class is injected here because this container runs as a
        // namespace root that can bypass mode bits.
        let hook = |_temp: &Path| Err(io::Error::from(io::ErrorKind::PermissionDenied));
        let hooks = SaveHooks {
            during_temp_write: None,
            before_rename: Some(&hook),
        };
        let result = save_atomic_with(&target, b"replacement", hooks);
        assert!(result.is_err(), "save must fail");
        assert_eq!(fs::read_to_string(&target).unwrap_or_default(), "original");
        assert!(
            leftovers(&dir.0).is_empty(),
            "temporary file was not removed"
        );
    }

    #[test]
    fn missing_parent_directory_is_rejected_without_touching_anything() {
        let dir = TestDir::new("missing-parent");
        let target = dir.file("missing/a.sv");
        let result = save_atomic_with(&target, b"x", SaveHooks::none());
        assert!(result.is_err(), "save into a missing directory must fail");
        assert!(!target.exists());
    }
}
