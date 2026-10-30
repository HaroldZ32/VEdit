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
    fn expose_secret_is_the_only_way_out() {
        let key = ApiKey::new(SECRET);
        assert_eq!(key.expose_secret(), SECRET);
        assert!(!key.is_empty());
        assert!(ApiKey::new("   ").is_empty());
    }
}
