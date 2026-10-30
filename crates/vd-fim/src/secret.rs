//! Secret material with redaction enforced by the type system.
//!
//! This mirrors `vd_config::ApiKey` **deliberately**: `vd-fim` does not depend on
//! `vd-config` (a lane may not change the manifests), so the transport layer keeps
//! its own redacted newtype. The duplicated guarantee is paid for by its own
//! tests below — no `Display`, `Debug` always `<redacted>`, `Serialize` always
//! `<redacted>`, and plaintext only through [`ApiKey::expose_secret`].

use serde::{Deserialize, Deserializer, Serialize, Serializer};

const REDACTED: &str = "<redacted>";

/// An API key for a FIM endpoint. Never printed, never serialized, never logged.
#[derive(Clone, PartialEq, Eq)]
pub struct ApiKey(String);

impl ApiKey {
    /// Wraps a raw key value.
    pub fn new(value: impl Into<String>) -> Self {
        Self(value.into())
    }

    /// True when the key is empty or whitespace-only.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.0.trim().is_empty()
    }

    /// Borrows the raw key.
    ///
    /// Call this only from the transport layer that has to send it, and never put
    /// the result in a log line, an error message or the status bar.
    #[must_use]
    pub fn expose_secret(&self) -> &str {
        &self.0
    }
}

impl std::fmt::Debug for ApiKey {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter.write_str("ApiKey(")?;
        formatter.write_str(REDACTED)?;
        formatter.write_str(")")
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

    const SECRET: &str = "sk-fim-do-not-print-1234567890";

    #[test]
    fn debug_is_redacted() {
        let key = ApiKey::new(SECRET);
        let rendered = format!("{key:?}");
        assert_eq!(rendered, "ApiKey(<redacted>)");
        assert!(!rendered.contains(SECRET));
    }

    #[test]
    fn serialization_is_redacted() {
        let key = ApiKey::new(SECRET);
        let json = serde_json::to_string(&key).unwrap_or_default();
        assert_eq!(json, "\"<redacted>\"");
        assert!(!json.contains(SECRET));
    }

    #[test]
    fn serialization_inside_a_struct_is_redacted() {
        #[derive(serde::Serialize)]
        struct Wrapper {
            api_key: ApiKey,
        }
        let wrapper = Wrapper {
            api_key: ApiKey::new(SECRET),
        };
        let json = serde_json::to_string(&wrapper).unwrap_or_default();
        assert!(
            !json.contains(SECRET),
            "key leaked through nested serialization"
        );
        assert!(json.contains("<redacted>"));
    }

    #[test]
    fn expose_secret_is_the_only_way_to_read_plaintext() {
        let key = ApiKey::new(SECRET);
        assert_eq!(key.expose_secret(), SECRET);
        assert!(!key.is_empty());
        assert!(ApiKey::new("   ").is_empty());
        assert!(ApiKey::new("").is_empty());
    }

    #[test]
    fn deserialization_round_trips_the_plaintext() {
        let parsed: ApiKey = serde_json::from_str("\"sk-abc\"").unwrap_or_else(|_| ApiKey::new(""));
        assert_eq!(parsed.expose_secret(), "sk-abc");
    }
}
