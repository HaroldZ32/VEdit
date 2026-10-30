//! Injected environment and XDG base directories.
//!
//! Nothing in this crate reads the process environment directly: [`Loader`](crate::Loader)
//! takes an [`Env`], which keeps every resolution purely logical and lets tests
//! run in parallel without mutating process state.

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

/// A snapshot of the environment variables the config layer is allowed to read.
#[derive(Debug, Clone, Default)]
pub struct Env {
    vars: BTreeMap<String, String>,
}

impl Env {
    /// Snapshot the real process environment.
    ///
    /// Non-Unicode entries are skipped rather than panicking.
    pub fn from_process() -> Self {
        Self {
            vars: std::env::vars_os()
                .filter_map(|(key, value)| {
                    let key = key.into_string().ok()?;
                    let value = value.into_string().ok()?;
                    Some((key, value))
                })
                .collect(),
        }
    }

    /// Build an environment from explicit pairs (tests, embedding).
    pub fn from_pairs<I, K, V>(pairs: I) -> Self
    where
        I: IntoIterator<Item = (K, V)>,
        K: Into<String>,
        V: Into<String>,
    {
        Self {
            vars: pairs
                .into_iter()
                .map(|(key, value)| (key.into(), value.into()))
                .collect(),
        }
    }

    /// The value of `name`, if set (an empty value is still "set").
    pub fn get(&self, name: &str) -> Option<&str> {
        self.vars.get(name).map(String::as_str)
    }

    /// `$HOME`, when usable.
    pub fn home(&self) -> Option<PathBuf> {
        self.get("HOME")
            .map(str::trim)
            .filter(|home| !home.is_empty())
            .map(PathBuf::from)
    }
}

/// XDG base directories, resolved per the XDG Base Directory Specification.
///
/// A variable is used only when it is an absolute path; unset, empty or
/// relative values fall back to the specification's defaults. The loader emits
/// `xdg.invalid` warnings for the relative case so it stays observable.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct XdgDirs {
    /// `$XDG_CONFIG_HOME` or `$HOME/.config`.
    pub config_home: Option<PathBuf>,
    /// `$XDG_DATA_HOME` or `$HOME/.local/share`.
    pub data_home: Option<PathBuf>,
    /// `$XDG_CACHE_HOME` or `$HOME/.cache`.
    pub cache_home: Option<PathBuf>,
}

impl XdgDirs {
    /// Resolve the three directories from an injected environment.
    pub fn from_env(env: &Env) -> Self {
        let home = env.home();
        Self {
            config_home: xdg_dir(
                env,
                "XDG_CONFIG_HOME",
                home.as_deref().map(|h| h.join(".config")),
            ),
            data_home: xdg_dir(
                env,
                "XDG_DATA_HOME",
                home.as_deref().map(|h| h.join(".local/share")),
            ),
            cache_home: xdg_dir(
                env,
                "XDG_CACHE_HOME",
                home.as_deref().map(|h| h.join(".cache")),
            ),
        }
    }

    /// The user-level configuration file: `$XDG_CONFIG_HOME/vedit/vedit.toml`.
    pub fn config_file(&self) -> Option<PathBuf> {
        self.config_home
            .as_ref()
            .map(|home| home.join("vedit").join("vedit.toml"))
    }

    /// The three XDG variable names, for diagnostics.
    pub fn variable_names() -> [&'static str; 3] {
        ["XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME"]
    }

    /// True when `name`'s raw value is present but not an absolute path.
    pub fn invalid_override<'a>(env: &'a Env, name: &str) -> Option<&'a str> {
        let raw = env.get(name)?;
        let trimmed = raw.trim();
        if trimmed.is_empty() {
            return None;
        }
        (!Path::new(trimmed).is_absolute()).then_some(trimmed)
    }
}

fn xdg_dir(env: &Env, name: &str, fallback: Option<PathBuf>) -> Option<PathBuf> {
    let Some(raw) = env.get(name) else {
        return fallback;
    };
    let trimmed = raw.trim();
    if trimmed.is_empty() {
        return fallback;
    }
    let path = PathBuf::from(trimmed);
    if path.is_absolute() {
        Some(path)
    } else {
        fallback
    }
}

#[cfg(test)]
mod tests {
    use super::{Env, XdgDirs};
    use std::path::PathBuf;

    #[test]
    fn xdg_defaults_come_from_home() {
        let env = Env::from_pairs([("HOME", "/home/alice")]);
        let dirs = XdgDirs::from_env(&env);
        assert_eq!(dirs.config_home, Some(PathBuf::from("/home/alice/.config")));
        assert_eq!(
            dirs.data_home,
            Some(PathBuf::from("/home/alice/.local/share"))
        );
        assert_eq!(dirs.cache_home, Some(PathBuf::from("/home/alice/.cache")));
        assert_eq!(
            dirs.config_file(),
            Some(PathBuf::from("/home/alice/.config/vedit/vedit.toml"))
        );
    }

    #[test]
    fn xdg_explicit_absolute_values_win() {
        let env = Env::from_pairs([
            ("HOME", "/home/alice"),
            ("XDG_CONFIG_HOME", "/srv/config"),
            ("XDG_DATA_HOME", "/srv/data"),
            ("XDG_CACHE_HOME", "/srv/cache"),
        ]);
        let dirs = XdgDirs::from_env(&env);
        assert_eq!(dirs.config_home, Some(PathBuf::from("/srv/config")));
        assert_eq!(dirs.data_home, Some(PathBuf::from("/srv/data")));
        assert_eq!(dirs.cache_home, Some(PathBuf::from("/srv/cache")));
    }

    #[test]
    fn relative_xdg_value_falls_back_and_is_reported() {
        let env = Env::from_pairs([("HOME", "/home/alice"), ("XDG_CONFIG_HOME", "relative/dir")]);
        let dirs = XdgDirs::from_env(&env);
        assert_eq!(dirs.config_home, Some(PathBuf::from("/home/alice/.config")));
        assert_eq!(
            XdgDirs::invalid_override(&env, "XDG_CONFIG_HOME"),
            Some("relative/dir")
        );
    }

    #[test]
    fn no_home_means_no_default_directories() {
        let env = Env::from_pairs(Vec::<(String, String)>::new());
        let dirs = XdgDirs::from_env(&env);
        assert!(dirs.config_home.is_none());
        assert!(dirs.config_file().is_none());
    }
}
