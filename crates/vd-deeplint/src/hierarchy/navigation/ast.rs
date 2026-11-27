//! Streaming projection of slang's AST onto the fields the trace graph reads.

use std::cell::Cell;
use std::fmt;
use std::fs::File;
use std::io::{self, BufReader, Read};
use std::path::Path;
use std::sync::atomic::{AtomicBool, Ordering};

use serde::de::{DeserializeSeed, IgnoredAny, MapAccess, SeqAccess, Visitor};
use serde::Deserializer;
use serde_json::{Map, Value};

use crate::LintFailure;

// Charge retained maps, fields and strings. The raw bytes of the dump do not
// count: layout whitespace, definitions, types, literals, and unrelated symbols
// must not consume the navigation budget.
const PROJECTION_BUDGET: usize = 256 * 1024 * 1024;
const PROJECTION_LIMIT: &str = "signal tracing exceeded its retained AST budget";

pub(in crate::hierarchy) fn read_ast(
    path: &Path,
    root: &Path,
    cancel: &AtomicBool,
) -> Result<Value, LintFailure> {
    let file = File::open(path).map_err(|error| LintFailure::Io {
        message: format!("opening {} failed: {error}", path.display()),
    })?;
    let reader = BufReader::with_capacity(64 * 1024, Cancellable { file, cancel });
    let mut deserializer = serde_json::Deserializer::from_reader(reader);
    let remaining = Cell::new(PROJECTION_BUDGET);
    let document = Project {
        shape: Shape::Document,
        root,
        remaining: &remaining,
    }
    .deserialize(&mut deserializer)
    .and_then(|value| {
        deserializer.end()?;
        Ok(value.unwrap_or(Value::Null))
    });
    if cancel.load(Ordering::Relaxed) {
        return Err(LintFailure::Cancelled);
    }
    document.map_err(|error| {
        let message = error.to_string();
        if message.starts_with(PROJECTION_LIMIT) {
            LintFailure::InvalidAstJson {
                message: PROJECTION_LIMIT.to_string(),
            }
        } else if error.is_io() {
            LintFailure::Io { message }
        } else if message.contains("recursion limit exceeded") {
            LintFailure::AstJsonNestedTooDeep { measured: None }
        } else {
            LintFailure::InvalidAstJson { message }
        }
    })
}

struct Cancellable<'a> {
    file: File,
    cancel: &'a AtomicBool,
}

impl Read for Cancellable<'_> {
    fn read(&mut self, buffer: &mut [u8]) -> io::Result<usize> {
        if self.cancel.load(Ordering::Relaxed) {
            return Err(io::Error::other("signal trace cancelled"));
        }
        self.file.read(buffer)
    }
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Shape {
    Document,
    Member,
    Body,
    Expression,
}

#[derive(Clone, Copy)]
struct Project<'a> {
    shape: Shape,
    root: &'a Path,
    remaining: &'a Cell<usize>,
}

impl Project<'_> {
    fn charge<E: serde::de::Error>(&self, bytes: usize) -> Result<(), E> {
        let remaining = self
            .remaining
            .get()
            .checked_sub(bytes)
            .ok_or_else(|| E::custom(PROJECTION_LIMIT))?;
        self.remaining.set(remaining);
        Ok(())
    }

    fn as_shape(self, shape: Shape) -> Self {
        Self { shape, ..self }
    }
}

fn is_member(kind: &str) -> bool {
    matches!(
        kind,
        "Root"
            | "Instance"
            | "InstanceBody"
            | "InstanceArray"
            | "GenerateBlock"
            | "GenerateBlockArray"
            | "Net"
            | "Variable"
            | "Port"
            | "ContinuousAssign"
            | "ProceduralBlock"
    )
}

fn has_position(kind: &str) -> bool {
    matches!(
        kind,
        "Instance" | "Net" | "Variable" | "Port" | "NamedValue"
    )
}

impl<'de> DeserializeSeed<'de> for Project<'_> {
    type Value = Option<Value>;

    fn deserialize<D: Deserializer<'de>>(self, deserializer: D) -> Result<Self::Value, D::Error> {
        deserializer.deserialize_any(self)
    }
}

impl<'de> Visitor<'de> for Project<'_> {
    type Value = Option<Value>;

    fn expecting(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str("a slang AST JSON value")
    }

    fn visit_unit<E: serde::de::Error>(self) -> Result<Self::Value, E> {
        Ok(None)
    }

    fn visit_bool<E: serde::de::Error>(self, _: bool) -> Result<Self::Value, E> {
        Ok(None)
    }

    fn visit_i64<E: serde::de::Error>(self, _: i64) -> Result<Self::Value, E> {
        Ok(None)
    }

    fn visit_u64<E: serde::de::Error>(self, _: u64) -> Result<Self::Value, E> {
        Ok(None)
    }

    fn visit_f64<E: serde::de::Error>(self, _: f64) -> Result<Self::Value, E> {
        Ok(None)
    }

    fn visit_str<E: serde::de::Error>(self, value: &str) -> Result<Self::Value, E> {
        // Slang may describe a repeated instance body by address instead.
        if self.shape == Shape::Body {
            self.charge(value.len())?;
            return Ok(Some(Value::String(value.to_owned())));
        }
        Ok(None)
    }

    fn visit_seq<A: SeqAccess<'de>>(self, mut sequence: A) -> Result<Self::Value, A::Error> {
        let mut items = Vec::new();
        while let Some(item) = sequence.next_element_seed(self)? {
            if let Some(item) = item {
                self.charge(64)?;
                items.push(item);
            }
        }
        Ok((!items.is_empty()).then_some(Value::Array(items)))
    }

    fn visit_map<A: MapAccess<'de>>(self, mut input: A) -> Result<Self::Value, A::Error> {
        let mut fields = Map::new();
        while let Some(key) = input.next_key::<String>()? {
            let kind = fields
                .get("kind")
                .and_then(Value::as_str)
                .unwrap_or_default();
            let value = match key.as_str() {
                "design" if self.shape == Shape::Document => {
                    input.next_value_seed(self.as_shape(Shape::Member))?
                }
                _ if self.shape == Shape::Document
                    || (self.shape == Shape::Member && !kind.is_empty() && !is_member(kind)) =>
                {
                    input.next_value::<IgnoredAny>()?;
                    None
                }
                "kind" | "name" | "direction" | "procedureKind" | "symbol" => {
                    input.next_value::<Option<String>>()?.map(Value::String)
                }
                "addr" | "constructIndex" => input.next_value::<Option<u64>>()?.map(Value::from),
                "source_file" | "source_file_start" | "source_file_end"
                    if kind.is_empty() || has_position(kind) =>
                {
                    input.next_value::<Option<String>>()?.map(Value::String)
                }
                "source_line"
                | "source_line_start"
                | "source_line_end"
                | "source_column"
                | "source_column_start"
                | "source_column_end"
                    if kind.is_empty() || has_position(kind) =>
                {
                    input.next_value::<Option<u64>>()?.map(Value::from)
                }
                "body" => input.next_value_seed(self.as_shape(Shape::Body))?,
                "members" if self.shape != Shape::Expression => {
                    input.next_value_seed(self.as_shape(Shape::Member))?
                }
                "connections" | "assignment" => {
                    input.next_value_seed(self.as_shape(Shape::Expression))?
                }
                _ if self.shape == Shape::Member || kind == "InstanceBody" => {
                    input.next_value::<IgnoredAny>()?;
                    None
                }
                "type"
                | "constant"
                | "definition"
                | "internalSymbol"
                | "source_file"
                | "source_file_start"
                | "source_file_end"
                | "source_line"
                | "source_line_start"
                | "source_line_end"
                | "source_column"
                | "source_column_start"
                | "source_column_end" => {
                    input.next_value::<IgnoredAny>()?;
                    None
                }
                // Operators and statement wrappers vary. Preserve their structure
                // whenever a retained child carries an assignment or named reference.
                _ => input.next_value_seed(self.as_shape(Shape::Expression))?,
            };
            if let Some(value) = value {
                fields.insert(key, value);
            }
        }
        let kind = fields
            .get("kind")
            .and_then(Value::as_str)
            .unwrap_or_default();
        let keep = is_member(kind)
            || matches!(kind, "NamedValue" | "Assignment")
            || fields
                .values()
                .any(|value| value.is_object() || value.is_array());
        if !keep || (self.shape == Shape::Member && !is_member(kind)) {
            return Ok(None);
        }
        let positioned = has_position(kind);
        fields.retain(|key, _| positioned || !key.starts_with("source_"));
        for key in ["source_file", "source_file_start", "source_file_end"] {
            if let Some(Value::String(file)) = fields.get_mut(key) {
                if !Path::new(file).is_absolute() {
                    *file = crate::tool_paths::absolute_path_str(file, self.root);
                }
            }
        }
        for (key, value) in &fields {
            self.charge(128 + key.len() + value.as_str().map_or(0, str::len))?;
        }
        self.charge(256)?;
        Ok(Some(Value::Object(fields)))
    }
}
