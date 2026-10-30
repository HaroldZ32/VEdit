//! Protocol wire formats: request bodies and response parsing for both backends.
//!
//! The bodies are built here, as pure functions, so the exact field names are
//! testable without any network: OpenAI-compatible `prompt` + `suffix`,
//! llama.cpp `input_prefix` + `input_suffix`. Nothing here echoes the API key.

use serde_json::{json, Value};

use crate::error::FimError;

/// Hard cap for the response body, in bytes. A server that returns more is
/// reported as [`FimError::ResponseTooLarge`] instead of being buffered.
pub const MAX_RESPONSE_BYTES: usize = 64 * 1024;

/// Builds an OpenAI-compatible `/v1/completions` request body.
///
/// Field names are part of the protocol contract: `model`, `prompt`, `suffix`,
/// `max_tokens`.
#[must_use]
pub fn openai_request_body(model: &str, prompt: &str, suffix: &str, max_tokens: u32) -> Value {
    json!({
        "model": model,
        "prompt": prompt,
        "suffix": suffix,
        "max_tokens": max_tokens,
    })
}

/// Builds a llama.cpp `/infill` request body.
///
/// Field names are part of the protocol contract: `input_prefix`,
/// `input_suffix`, `n_predict`.
#[must_use]
pub fn llama_cpp_request_body(prompt: &str, suffix: &str, n_predict: u32) -> Value {
    json!({
        "input_prefix": prompt,
        "input_suffix": suffix,
        "n_predict": n_predict,
    })
}

/// Extracts `choices[0].text` from an OpenAI-compatible response.
pub fn parse_openai_response(body: &str) -> Result<String, FimError> {
    let value = parse_json(body)?;
    let text = value
        .get("choices")
        .and_then(Value::as_array)
        .and_then(|choices| choices.first())
        .and_then(|choice| choice.get("text"))
        .and_then(Value::as_str)
        .ok_or(FimError::InvalidResponse {
            detail: "missing choices[0].text",
        })?;
    Ok(normalise_completion(text))
}

/// Extracts `content` from a llama.cpp `/infill` response.
pub fn parse_llama_cpp_response(body: &str) -> Result<String, FimError> {
    let value = parse_json(body)?;
    let text = value
        .get("content")
        .and_then(Value::as_str)
        .ok_or(FimError::InvalidResponse {
            detail: "missing content",
        })?;
    Ok(normalise_completion(text))
}

fn parse_json(body: &str) -> Result<Value, FimError> {
    serde_json::from_str(body).map_err(|_| FimError::InvalidResponse {
        detail: "body is not JSON",
    })
}

/// Drops the trailing line break a completion endpoint usually appends; inner
/// whitespace and leading spaces are kept because FIM text is inserted verbatim.
fn normalise_completion(text: &str) -> String {
    text.trim_end_matches(['\n', '\r']).to_string()
}

#[cfg(test)]
mod tests {
    use super::{
        llama_cpp_request_body, openai_request_body, parse_llama_cpp_response,
        parse_openai_response, MAX_RESPONSE_BYTES,
    };
    use crate::error::FimError;
    use serde_json::json;

    #[test]
    fn openai_body_has_exactly_the_contract_fields() {
        let body = openai_request_body("qwen2.5-coder", "assign x =", ";\nendmodule", 64);
        assert_eq!(
            body,
            json!({
                "model": "qwen2.5-coder",
                "prompt": "assign x =",
                "suffix": ";\nendmodule",
                "max_tokens": 64,
            })
        );
        if let Some(object) = body.as_object() {
            assert_eq!(object.len(), 4, "unexpected extra fields: {body}");
            for field in ["model", "prompt", "suffix", "max_tokens"] {
                assert!(object.contains_key(field), "missing {field}");
            }
        } else {
            panic!("body is not a JSON object: {body}");
        }
    }

    #[test]
    fn llama_cpp_body_has_exactly_the_contract_fields() {
        let body = llama_cpp_request_body("always_ff @(", ") begin\nend", 32);
        assert_eq!(
            body,
            json!({
                "input_prefix": "always_ff @(",
                "input_suffix": ") begin\nend",
                "n_predict": 32,
            })
        );
        if let Some(object) = body.as_object() {
            assert_eq!(object.len(), 3, "unexpected extra fields: {body}");
            for field in ["input_prefix", "input_suffix", "n_predict"] {
                assert!(object.contains_key(field), "missing {field}");
            }
        } else {
            panic!("body is not a JSON object: {body}");
        }
    }

    #[test]
    fn openai_response_parses_choices_zero_text() {
        let body = r#"{"id":"x","choices":[{"text":" <= 1;","index":0,"finish_reason":"stop"}]}"#;
        assert_eq!(parse_openai_response(body).ok().as_deref(), Some(" <= 1;"));
    }

    #[test]
    fn llama_cpp_response_parses_content() {
        let body = r#"{"content":" <= '0;","stop":true}"#;
        assert_eq!(
            parse_llama_cpp_response(body).ok().as_deref(),
            Some(" <= '0;")
        );
    }

    #[test]
    fn trailing_line_breaks_are_removed_but_leading_spaces_kept() {
        let body = r#"{"content":" x;\n\n"}"#;
        assert_eq!(parse_llama_cpp_response(body).ok().as_deref(), Some(" x;"));
    }

    #[test]
    fn malformed_bodies_are_invalid_response_not_a_panic() {
        for body in [
            "",
            "not json",
            "{}",
            "[]",
            r#"{"choices":[]}"#,
            r#"{"content":1}"#,
        ] {
            assert!(
                parse_openai_response(body).is_err(),
                "{body} should not parse"
            );
        }
        assert_eq!(
            parse_openai_response("{}").err(),
            Some(FimError::InvalidResponse {
                detail: "missing choices[0].text"
            })
        );
        assert_eq!(
            parse_openai_response("not json").err(),
            Some(FimError::InvalidResponse {
                detail: "body is not JSON"
            })
        );
        assert!(parse_llama_cpp_response("{}").is_err());
        assert!(parse_llama_cpp_response("not json").is_err());
    }

    #[test]
    fn response_cap_is_a_documented_constant() {
        assert_eq!(MAX_RESPONSE_BYTES, 64 * 1024);
    }
}
