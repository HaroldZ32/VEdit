//! Read-only history for a saved SVN repository URL.

use crate::svn_compare::{encode_path_segment, missing};
use crate::svn_read::{self, LogEntry, RevisionDetail};
use crate::svn_write::{checkout_url, literal_target};
use std::path::Path;

/// A credential-free repository or repository-subtree address.
pub struct Remote<'a> {
    directory: &'a Path,
    url: &'a str,
}

impl<'a> Remote<'a> {
    /// Validate the remote address and choose a local subprocess directory.
    ///
    /// # Errors
    /// Returns an error when the URL embeds credentials or has an unsupported form.
    pub fn new(directory: &'a Path, url: &'a str) -> Result<Self, String> {
        let url = checkout_url(url).map_err(|error| error.message().to_owned())?;
        let directory = if directory.is_dir() {
            directory
        } else {
            Path::new(".")
        };
        Ok(Self { directory, url })
    }

    /// Fetch a bounded log page. A decimal or `r`-prefixed query selects one revision.
    ///
    /// # Errors
    /// Returns the SVN log failure.
    pub fn log(&self, query: &str, limit: usize) -> Result<Vec<LogEntry>, String> {
        svn_read::log_at(
            self.directory,
            &literal_target(self.url),
            query,
            limit,
            false,
        )
    }

    /// Fetch one revision and its changed paths within the saved URL.
    ///
    /// # Errors
    /// Returns the SVN log failure or an invalid revision error.
    pub fn detail(&self, revision: &str) -> Result<RevisionDetail, String> {
        let target = literal_target(self.url);
        let info = svn_read::info(self.directory, &target)?;
        svn_read::detail_at(self.directory, &target, revision, info, false)
    }

    fn target(&self, path: &str) -> Result<String, String> {
        let path = path
            .strip_prefix('/')
            .ok_or("Invalid repository file path.")?;
        if path.split('/').any(|part| {
            part.is_empty() || matches!(part, "." | "..") || part.chars().any(char::is_control)
        }) {
            return Err("Invalid repository file path.".into());
        }
        let root = svn_read::info(self.directory, &literal_target(self.url))?.repository_root;
        let path = path
            .split('/')
            .map(encode_path_segment)
            .collect::<Vec<_>>()
            .join("/");
        Ok(format!("{}/{path}", root.trim_end_matches('/')))
    }

    /// Read a historical repository file, following its peg ancestry.
    ///
    /// # Errors
    /// Returns the actual SVN failure. Missing historical files return `Ok(None)`.
    pub fn snapshot(&self, path: &str, revision: u64, peg: u64) -> Result<Option<String>, String> {
        if revision == 0 {
            return Ok(None);
        }
        let target = self.target(path)?;
        match svn_read::cat(
            self.directory,
            &revision.to_string(),
            &format!("{target}@{peg}"),
        ) {
            Err(error) if missing(&error) => Ok(None),
            result => result.map(Some),
        }
    }

    /// Read the previous actual change at the same peg.
    ///
    /// # Errors
    /// Returns the actual SVN failure. A path without an older revision returns `Ok(None)`.
    pub fn previous(&self, path: &str, revision: u64, peg: u64) -> Result<Option<u64>, String> {
        if revision <= 1 {
            return Ok(None);
        }
        let target = self.target(path)?;
        let range = format!("{}:1", revision - 1);
        let output = svn_read::svn(
            self.directory,
            "log",
            &[
                "--xml",
                "--quiet",
                "--limit",
                "1",
                "-r",
                &range,
                "--",
                &format!("{target}@{peg}"),
            ],
        );
        match output {
            Ok(xml) => Ok(svn_read::log_entries(&xml)
                .next()
                .map(|entry| entry.revision)),
            Err(error) if missing(&error) => Ok(None),
            Err(error) => Err(error),
        }
    }
}
