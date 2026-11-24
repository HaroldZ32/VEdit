//! Secret material with redaction enforced by the type system.
//!
//! `ApiKey` implements `Debug` and `Serialize` in redacted form and deliberately
//! has **no** `Display` implementation, so a key cannot leak through `{:?}`,
//! `serde_json::to_string(config)`, or `format!("{}", …)` (the latter does not
//! compile). The only way to read the raw value is
//! [`ApiKey::expose_secret`], whose name makes the intent explicit at the call
//! site.

use serde::{Deserialize, Deserializer, Serialize, Serializer};

const REDACTED: &str = "<redacted>";

/// The placeholder shown instead of an imported variable whose NAME says it may
/// carry a secret (batch 20261110, AC-4.8).
pub const MASKED_VALUE: &str = "<masked>";

/// True when `name` must be masked by default: the name contains `TOKEN`,
/// `SECRET`, `PASSWORD` or `KEY` (ASCII case-insensitive).
///
/// The match is deliberately by SUBSTRING and deliberately over-inclusive: a
/// variable called `HOTKEY_PATH` is masked too, because the cost of masking a
/// harmless name is one click ("show value") while the cost of leaking a real
/// one is a credential in a log. The selection is still stored as the user
/// approved it — only display and logs are masked.
#[must_use]
pub fn is_secret_name(name: &str) -> bool {
    let upper = name.to_ascii_uppercase();
    ["TOKEN", "SECRET", "PASSWORD", "KEY"]
        .iter()
        .any(|marker| upper.contains(marker))
}

/// A secret API key read from the user configuration file or an environment
/// variable. Never printed, never serialized, never written to a log.
#[derive(Clone, PartialEq, Eq)]
pub struct ApiKey(String);

impl ApiKey {
    /// Wrap a raw key value.
    pub fn new(value: impl Into<String>) -> Self {
        Self(value.into())
    }

    /// True when the key is empty or whitespace-only.
    pub fn is_empty(&self) -> bool {
        self.0.trim().is_empty()
    }

    /// Borrow the raw key.
    ///
    /// Call this **only** from the transport layer that has to send the key.
    /// Never log, format, serialize or include the result in an error message.
    pub fn expose_secret(&self) -> &str {
        &self.0
    }
}

impl std::fmt::Debug for ApiKey {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str("ApiKey(")?;
        f.write_str(REDACTED)?;
        f.write_str(")")
    }
}

impl Serialize for ApiKey {
    fn serialize<S: Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
        serializer.serialize_str(REDACTED)
    }
}

impl<'de> Deserialize<'de> for ApiKey {
    fn deserialize<D: Deserializer<'de>>(deserializer: D) -> Result<Self, D::Error> {
        String::deserialize(deserializer).map(Self)
    }
}

#[cfg(test)]
mod tests {
    use super::ApiKey;

    const SECRET: &str = "sk-test-DO-NOT-LEAK-123456";

    #[test]
    fn debug_is_redacted() {
        let key = ApiKey::new(SECRET);
        let rendered = format!("{key:?}");
        assert!(rendered.contains("<redacted>"), "debug: {rendered}");
        assert!(!rendered.contains(SECRET), "debug leaked the key");
    }

    #[test]
    fn serialization_is_redacted() {
        let key = ApiKey::new(SECRET);
        let rendered = serde_json::to_string(&key).unwrap_or_else(|_| String::from("<error>"));
        assert_eq!(rendered, "\"<redacted>\"");
        assert!(!rendered.contains(SECRET));
    }

    #[test]
    fn secret_names_are_masked_by_name() {
        use super::{is_secret_name, MASKED_VALUE};
        for name in [
            "GITHUB_TOKEN",
            "npm_TOKEN",
            "MY_SECRET",
            "DB_PASSWORD",
            "API_KEY",
            "SSH_KEY_PATH",
            "hotkey_path",
        ] {
            assert!(is_secret_name(name), "{name} must be masked");
        }
        for name in ["PATH", "HOME", "JAVA_HOME", "LANG", "MONTH"] {
            assert!(!is_secret_name(name), "{name} must stay visible");
        }
        assert_eq!(MASKED_VALUE, "<masked>");
    }

    #[test]
    fn expose_secret_is_the_only_way_out() {
        let key = ApiKey::new(SECRET);
        assert_eq!(key.expose_secret(), SECRET);
        assert!(!key.is_empty());
        assert!(ApiKey::new("   ").is_empty());
    }
}
