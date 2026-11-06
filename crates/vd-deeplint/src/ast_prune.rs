//! Streaming prune for slang's `--ast-json` document.
//!
//! The module catalog only needs module definitions, their ports, the
//! elaborated instance tree and the `definitions` declaration kinds. Parsing the
//! whole document into a [`serde_json::Value`] costs several times the file size
//! in RAM — on a real 1176-file project the dump reaches hundreds of MB — and
//! that cost is what the old 256 MiB "cap" was protecting against, at the price
//! of failing the catalog outright.
//!
//! This module reads the document from a [`std::io::Read`] and keeps ONLY the
//! nodes the catalog walks: `Root`, `CompilationUnit`, `Instance`,
//! `InstanceBody`, `InstanceArray`, `GenerateBlockArray`, `GenerateBlock`,
//! `Port`, `InterfacePort` and `Definition`. Every other node (variables, nets,
//! expressions, assignments, connections, …) is skipped with serde's
//! [`IgnoredAny`], so retained memory is bounded by the catalog's own structures
//! rather than by the raw JSON.
//!
//! # Retention budget
//!
//! The walker's memory is bounded by the number of catalog nodes it KEEPS, not
//! by the size of the document it reads: a body's expressions, statements and
//! connections are dropped, so a 1176-file project retains only its module
//! bodies, ports and instantiation sites however many gigabytes `slang`
//! serialises. Past [`DEFAULT_MAX_RETAINED_NODES`] such nodes the walker stops
//! retaining `Instance` / `InstanceBody` / generate nodes (it keeps consuming
//! the stream, so the trailing `definitions` array is still complete) and
//! reports [`Pruned::retained_fully`] `= false`. The caller turns that into a
//! warning, never into a failure: a pathological design yields a partial,
//! clearly-warned catalog instead of no catalog.
//!
//! # What is preserved
//!
//! The pruned document keeps exactly the keys the catalog reads (`name`, `kind`,
//! `addr`, `source_file`/`_line`/`_column`, `definitionKind`, `definition`,
//! `type`, `direction`, `interfaceDef`, `constructIndex`, `body`, `members`), so
//! [`crate::catalog::parse_catalog_value`] runs unchanged on it.

use std::cell::Cell;
use std::fmt;
use std::io::{self, Read};
use std::rc::Rc;

use serde::de::{DeserializeSeed, IgnoredAny, MapAccess, SeqAccess, Visitor};
use serde::{Deserialize, Deserializer};
use serde_json::{Map, Value};

/// The object keys the pruning walk cares about.
///
/// The key is matched through a borrowed `&str` visitor, so an expression node
/// allocates NOTHING: allocating a `String` per key (the first cut did) made the
/// walk several times slower than the disk it reads.
enum Key {
    Kind,
    Name,
    Members,
    Body,
    Design,
    Definitions,
    /// A key the catalog reads verbatim once the node is retained.
    Keep(&'static str),
    Other,
}

impl Key {
    const fn name(&self) -> &'static str {
        match self {
            Self::Kind => "kind",
            Self::Name => "name",
            Self::Members => "members",
            Self::Body => "body",
            Self::Design => "design",
            Self::Definitions => "definitions",
            Self::Keep(name) => name,
            Self::Other => "",
        }
    }
}

impl<'de> Deserialize<'de> for Key {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        struct KeyVisitor;
        impl<'de> Visitor<'de> for KeyVisitor {
            type Value = Key;

            fn expecting(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
                formatter.write_str("a slang AST JSON object key")
            }

            fn visit_str<E>(self, value: &str) -> Result<Key, E>
            where
                E: serde::de::Error,
            {
                Ok(match value {
                    "kind" => Key::Kind,
                    "name" => Key::Name,
                    "members" => Key::Members,
                    "body" => Key::Body,
                    "design" => Key::Design,
                    "definitions" => Key::Definitions,
                    "addr" | "source_file" | "source_line" | "source_column" | "definitionKind"
                    | "definition" | "type" | "direction" | "interfaceDef" | "constructIndex" => {
                        Key::Keep(match value {
                            "addr" => "addr",
                            "source_file" => "source_file",
                            "source_line" => "source_line",
                            "source_column" => "source_column",
                            "definitionKind" => "definitionKind",
                            "definition" => "definition",
                            "type" => "type",
                            "direction" => "direction",
                            "interfaceDef" => "interfaceDef",
                            _ => "constructIndex",
                        })
                    }
                    _ => Key::Other,
                })
            }
        }
        deserializer.deserialize_str(KeyVisitor)
    }
}

/// What the walk does with one AST node, decided from its `kind`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum NodeClass {
    /// Keep the node and the keys the catalog reads.
    Retained(&'static str),
    /// Keep only the node's `members` (a scope that can contain instances).
    Container,
    /// Drop the node and its subtree.
    Other,
}

/// Kinds whose count is the catalog's real memory bound (they scale with the
/// elaborated design). `Port`/`Definition` are cheap: they scale with the module
/// count, which the body nodes already bound.
fn is_budgeted(kind: &'static str) -> bool {
    matches!(
        kind,
        "Instance" | "InstanceBody" | "InstanceArray" | "GenerateBlockArray" | "GenerateBlock"
    )
}

/// The node class of one `kind` string, matched without allocating.
fn classify(kind: &str) -> NodeClass {
    match kind {
        "Instance" => NodeClass::Retained("Instance"),
        "InstanceBody" => NodeClass::Retained("InstanceBody"),
        "InstanceArray" => NodeClass::Retained("InstanceArray"),
        "GenerateBlockArray" => NodeClass::Retained("GenerateBlockArray"),
        "GenerateBlock" => NodeClass::Retained("GenerateBlock"),
        "Port" => NodeClass::Retained("Port"),
        "InterfacePort" => NodeClass::Retained("InterfacePort"),
        "Definition" => NodeClass::Retained("Definition"),
        // Scopes that only forward their members (the design root and the
        // per-compilation-unit stubs).
        "Root" | "CompilationUnit" => NodeClass::Container,
        _ => NodeClass::Other,
    }
}

/// Deserializes a `kind` string into its [`NodeClass`] without allocating.
struct KindProbe {
    class: NodeClass,
}

impl<'de> Deserialize<'de> for KindProbe {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        struct ProbeVisitor;
        impl<'de> Visitor<'de> for ProbeVisitor {
            type Value = KindProbe;

            fn expecting(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
                formatter.write_str("a slang AST node kind")
            }

            fn visit_str<E>(self, value: &str) -> Result<KindProbe, E>
            where
                E: serde::de::Error,
            {
                Ok(KindProbe {
                    class: classify(value),
                })
            }
        }
        deserializer.deserialize_str(ProbeVisitor)
    }
}

/// Stack size of the thread that runs the deep walk (U9d).
///
/// The document is measured first and refused above
/// [`crate::AST_JSON_MAX_NESTING`], so the recursive walk can nest at most that
/// many JSON levels; 64 MiB holds 10,000 serde frames with a large margin
/// (measured: a document just below the cap completes on this stack). The
/// reservation is virtual — only the pages the recursion actually touches
/// become resident.
pub(crate) const WALK_STACK_BYTES: usize = 64 * 1024 * 1024;

/// Default cap on the number of structural nodes the walker KEEPS.
///
/// This is the catalog's real memory bound. It counts only the nodes the
/// catalog walks (`InstanceBody`, `Instance`, arrays and generate blocks); a
/// module body's ports, expressions, statements and connections never enter
/// the count of a large dump's bulk. The cap therefore trips only on a
/// pathological instance/fan-out count, where the catalog degrades to a
/// warned partial tree instead of growing without bound.
pub(crate) const DEFAULT_MAX_RETAINED_NODES: u64 = 1_000_000;

/// Shared counters for the reader and the pruning visitor.
#[derive(Clone)]
struct Budget {
    /// Bytes consumed from the reader. Reported so a size warning can name the
    /// document; deliberately NOT a retention limit, because the document size
    /// does not bound the walker's memory.
    bytes: Rc<Cell<u64>>,
    /// Structural nodes KEPT so far.
    nodes: Rc<Cell<u64>>,
    /// Maximum structural nodes that may be kept.
    node_limit: u64,
    /// `false` once the node limit dropped part of the design.
    retained_fully: Rc<Cell<bool>>,
}

impl Budget {
    fn new(node_limit: u64) -> Self {
        Self {
            bytes: Rc::new(Cell::new(0)),
            nodes: Rc::new(Cell::new(0)),
            node_limit,
            retained_fully: Rc::new(Cell::new(true)),
        }
    }

    fn add_bytes(&self, read: u64) {
        self.bytes.set(self.bytes.get().saturating_add(read));
    }

    /// Charge one structural node the caller is about to keep.
    ///
    /// `false` means the node budget is spent: the node (and its subtree) must
    /// be dropped, and the walk is no longer complete.
    fn keep_node(&self) -> bool {
        let kept = self.nodes.get().saturating_add(1);
        if kept > self.node_limit {
            self.retained_fully.set(false);
            return false;
        }
        self.nodes.set(kept);
        true
    }

    fn bytes_read(&self) -> u64 {
        self.bytes.get()
    }
}

/// Measure the deepest JSON container nesting in a stream, iteratively.
///
/// The catalog runs this BEFORE parsing so it can decide whether disabling
/// serde_json's 128-value guard is safe for THIS document. It reads 1 MiB at a
/// time, keeps O(1) state and never recurses, so it can scan a multi-GB dump
/// without memory growth. Brackets inside strings are ignored (string/escape
/// state machine). It stops early and returns `None` as soon as the measured
/// depth exceeds `limit`: the caller only needs "at least limit + 1" and a
/// pathological document must not cost a full extra scan.
pub(crate) fn measure_nesting<R: Read>(reader: R, limit: u64) -> std::io::Result<Option<u64>> {
    let mut reader = reader;
    let mut buffer = vec![0u8; 1024 * 1024];
    let mut depth: u64 = 0;
    let mut max_depth: u64 = 0;
    let mut in_string = false;
    let mut escaped = false;
    loop {
        let read = reader.read(&mut buffer)?;
        if read == 0 {
            return Ok(Some(max_depth));
        }
        for &byte in &buffer[..read] {
            if in_string {
                if escaped {
                    escaped = false;
                } else if byte == b'\\' {
                    escaped = true;
                } else if byte == b'"' {
                    in_string = false;
                }
                continue;
            }
            match byte {
                b'"' => in_string = true,
                b'{' | b'[' => {
                    depth += 1;
                    max_depth = max_depth.max(depth);
                    if max_depth > limit {
                        return Ok(None);
                    }
                }
                b'}' | b']' => depth = depth.saturating_sub(1),
                _ => {}
            }
        }
    }
}

/// The pruned document plus what the pruning walk observed.
#[derive(Debug)]
pub(crate) struct Pruned {
    /// The compact document (only the nodes the catalog reads).
    pub(crate) document: Value,
    /// Bytes consumed from the reader (the whole document, even past the budget).
    pub(crate) bytes_read: u64,
    /// `false` when the retention budget dropped part of the design.
    pub(crate) retained_fully: bool,
}

/// Increments the shared counter for every byte serde_json pulls from `inner`.
struct CountingReader<R> {
    inner: R,
    budget: Budget,
}

impl<R: Read> Read for CountingReader<R> {
    fn read(&mut self, buffer: &mut [u8]) -> io::Result<usize> {
        let read = self.inner.read(buffer)?;
        self.budget
            .add_bytes(u64::try_from(read).unwrap_or(u64::MAX));
        Ok(read)
    }
}

/// Read and prune a whole `--ast-json` document.
///
/// `max_retained_nodes` bounds the structural nodes KEPT (see
/// [`DEFAULT_MAX_RETAINED_NODES`]); the document itself may be arbitrarily
/// large because it is never held in memory.
///
/// # Errors
/// Returns a message when the document is not valid JSON for this walk (the
/// caller maps it to the existing malformed-document failure).
pub(crate) fn prune_json<R: Read>(reader: R, max_retained_nodes: u64) -> Result<Pruned, String> {
    let budget = Budget::new(max_retained_nodes);
    let counting = CountingReader {
        inner: reader,
        budget: budget.clone(),
    };
    let mut deserializer = serde_json::Deserializer::from_reader(counting);
    // U9d: the caller has already MEASURED this document's nesting with the
    // iterative scan above and refused anything past `AST_JSON_MAX_NESTING`, so
    // the recursion depth here is provably bounded. Without this, serde_json's
    // fixed 128-value guard trips at ~42 nested instances — a normal design.
    deserializer.disable_recursion_limit();
    let document = PruneSeed { budget: &budget }
        .deserialize(&mut deserializer)
        .map_err(|error| error.to_string())?
        .ok_or_else(|| "the AST JSON root is not an object".to_string())?;
    if !document.is_object() {
        return Err("the AST JSON root is not an object".to_string());
    }
    Ok(Pruned {
        document,
        bytes_read: budget.bytes_read(),
        retained_fully: budget.retained_fully.get(),
    })
}

struct PruneSeed<'a> {
    budget: &'a Budget,
}

impl<'de, 'a> DeserializeSeed<'de> for PruneSeed<'a> {
    type Value = Option<Value>;

    fn deserialize<D>(self, deserializer: D) -> Result<Self::Value, D::Error>
    where
        D: Deserializer<'de>,
    {
        deserializer.deserialize_any(PruneVisitor {
            budget: self.budget,
        })
    }
}

struct PruneVisitor<'a> {
    budget: &'a Budget,
}

impl<'de, 'a> Visitor<'de> for PruneVisitor<'a> {
    type Value = Option<Value>;

    fn expecting(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str("a slang AST JSON value")
    }

    fn visit_map<A>(self, mut map: A) -> Result<Self::Value, A::Error>
    where
        A: MapAccess<'de>,
    {
        let mut out = Map::new();
        let mut class: Option<NodeClass> = None;
        let mut discarded = false;
        // A position/key of a RETAINED node that arrived before its `kind`. The
        // tool writes `kind` second on every catalog node and puts the source
        // span before it only on expressions, so this never fires today; if it
        // ever does, erroring beats silently reporting empty positions.
        let mut skipped_before_kind = false;
        while let Some(key) = map.next_key::<Key>()? {
            if discarded {
                map.next_value::<IgnoredAny>()?;
                continue;
            }
            match key {
                Key::Kind => {
                    let probe: KindProbe = map.next_value()?;
                    class = Some(probe.class);
                    match probe.class {
                        NodeClass::Retained(kind) => {
                            if is_budgeted(kind) && !self.budget.keep_node() {
                                class = Some(NodeClass::Other);
                                discarded = true;
                            } else {
                                out.insert("kind".to_string(), Value::String(kind.to_string()));
                            }
                        }
                        NodeClass::Container => {}
                        NodeClass::Other => discarded = true,
                    }
                }
                Key::Members => {
                    // A scope (Root / InstanceBody / generate / instance array):
                    // the only place a catalog node may appear.
                    let members = map.next_value_seed(MembersSeed {
                        budget: self.budget,
                    })?;
                    out.insert("members".to_string(), Value::Array(members));
                }
                Key::Body | Key::Design | Key::Definitions => {
                    // `body` is an inline InstanceBody or a "<addr> name" ref;
                    // `design` / `definitions` are the document's two containers.
                    let value = map.next_value_seed(PruneSeed {
                        budget: self.budget,
                    })?;
                    if let Some(value) = value {
                        out.insert(key.name().to_string(), value);
                    }
                }
                Key::Name => {
                    // `name` precedes `kind` on every catalog node, so it is
                    // read even before the node is classified.
                    let value: String = map.next_value()?;
                    out.insert("name".to_string(), Value::String(value));
                }
                Key::Keep(name) => {
                    if class.is_some() {
                        let value: Value = map.next_value()?;
                        out.insert(name.to_string(), value);
                    } else {
                        // An unclassified node's position/definition fields:
                        // allocating them for every expression would dominate
                        // the walk, so they are skipped (the layout guard above
                        // turns a retained node of this shape into an error).
                        skipped_before_kind = true;
                        map.next_value::<IgnoredAny>()?;
                    }
                }
                Key::Other => {
                    map.next_value::<IgnoredAny>()?;
                }
            }
        }
        let Some(class) = class else {
            // The document root carries no `kind`: any object that contributed a
            // retained container (`design`, `definitions`, `members`, `body`) is
            // kept as a plain container, everything else is dropped.
            return Ok(if out.is_empty() {
                None
            } else {
                Some(Value::Object(out))
            });
        };
        if class == NodeClass::Other {
            return Ok(None);
        }
        if matches!(class, NodeClass::Retained(_)) && skipped_before_kind {
            return Err(serde::de::Error::custom(
                "unsupported AST JSON layout: a position field precedes `kind`",
            ));
        }
        if class == NodeClass::Container {
            // A scope keeps only its members; nothing else is read from it.
            let mut container = Map::new();
            if let Some(members) = out.remove("members") {
                container.insert("members".to_string(), members);
            }
            return Ok(Some(Value::Object(container)));
        }
        Ok(Some(Value::Object(out)))
    }

    fn visit_seq<A>(self, mut seq: A) -> Result<Self::Value, A::Error>
    where
        A: SeqAccess<'de>,
    {
        let mut out = Vec::new();
        while let Some(item) = seq.next_element_seed(PruneSeed {
            budget: self.budget,
        })? {
            if let Some(item) = item {
                out.push(item);
            }
        }
        Ok(Some(Value::Array(out)))
    }

    fn visit_bool<E>(self, value: bool) -> Result<Self::Value, E> {
        Ok(Some(Value::Bool(value)))
    }

    fn visit_i64<E>(self, value: i64) -> Result<Self::Value, E> {
        Ok(Some(Value::Number(value.into())))
    }

    fn visit_u64<E>(self, value: u64) -> Result<Self::Value, E> {
        Ok(Some(Value::Number(value.into())))
    }

    fn visit_f64<E>(self, value: f64) -> Result<Self::Value, E> {
        serde_json::Number::from_f64(value)
            .map_or(Ok(None), |number| Ok(Some(Value::Number(number))))
    }

    fn visit_str<E>(self, value: &str) -> Result<Self::Value, E> {
        Ok(Some(Value::String(value.to_string())))
    }

    fn visit_string<E>(self, value: String) -> Result<Self::Value, E> {
        Ok(Some(Value::String(value)))
    }

    fn visit_unit<E>(self) -> Result<Self::Value, E> {
        Ok(Some(Value::Null))
    }
}

struct MembersSeed<'a> {
    budget: &'a Budget,
}

impl<'de, 'a> DeserializeSeed<'de> for MembersSeed<'a> {
    type Value = Vec<Value>;

    fn deserialize<D>(self, deserializer: D) -> Result<Self::Value, D::Error>
    where
        D: Deserializer<'de>,
    {
        deserializer.deserialize_seq(MembersVisitor {
            budget: self.budget,
        })
    }
}

struct MembersVisitor<'a> {
    budget: &'a Budget,
}

impl<'de, 'a> Visitor<'de> for MembersVisitor<'a> {
    type Value = Vec<Value>;

    fn expecting(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str("an array of AST members")
    }

    fn visit_seq<A>(self, mut seq: A) -> Result<Self::Value, A::Error>
    where
        A: SeqAccess<'de>,
    {
        let mut out = Vec::new();
        while let Some(item) = seq.next_element_seed(PruneSeed {
            budget: self.budget,
        })? {
            if let Some(item) = item {
                out.push(item);
            }
        }
        Ok(out)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn prune(text: &str) -> Pruned {
        prune_json(text.as_bytes(), u64::MAX).unwrap_or_else(|error| panic!("prune: {error}"))
    }

    #[test]
    fn irrelevant_subtrees_are_dropped_but_catalog_nodes_survive() {
        let text = r#"{
            "design": {
                "name": "$root", "kind": "Root", "addr": 1,
                "members": [
                    {"name": "top", "kind": "Instance", "addr": 7,
                     "source_file": "top.sv", "source_line": 1, "source_column": 1,
                     "body": {"name": "top", "kind": "InstanceBody", "addr": 8,
                              "members": [
                                {"name": "clk", "kind": "Port", "type": "logic", "direction": "In"},
                                {"name": "unused", "kind": "Variable",
                                 "members": [{"kind": "NamedValue", "name": "x"}]}
                              ]}},
                    {"kind": "CompilationUnit", "addr": 9}
                ]
            },
            "definitions": [
                {"name": "top", "kind": "Definition", "addr": 10, "definitionKind": "Module"}
            ]
        }"#;
        let pruned = prune(text);
        let design = pruned
            .document
            .get("design")
            .unwrap_or_else(|| panic!("design missing"));
        let members = design
            .get("members")
            .and_then(Value::as_array)
            .unwrap_or_else(|| panic!("members missing"));
        assert_eq!(
            members.len(),
            2,
            "only the Instance and the CompilationUnit survive"
        );
        let instance = &members[0];
        assert_eq!(
            instance.get("kind").and_then(Value::as_str),
            Some("Instance")
        );
        let body_members = instance
            .get("body")
            .and_then(|body| body.get("members"))
            .and_then(Value::as_array)
            .unwrap_or_else(|| panic!("body members missing"));
        assert_eq!(body_members.len(), 1, "the Variable member is dropped");
        assert_eq!(
            body_members[0].get("kind").and_then(Value::as_str),
            Some("Port")
        );
        let definitions = pruned
            .document
            .get("definitions")
            .and_then(Value::as_array)
            .unwrap_or_else(|| panic!("definitions missing"));
        assert_eq!(definitions.len(), 1);
        assert_eq!(
            definitions[0].get("definitionKind").and_then(Value::as_str),
            Some("Module")
        );
        assert!(pruned.retained_fully);
    }

    #[test]
    fn connections_and_expressions_are_not_retained() {
        let text = r#"{"design": {"kind": "Root", "members": [
            {"name": "u", "kind": "Instance", "addr": 3,
             "connections": [{"port": "a", "expr": {"kind": "BinaryOp", "value": 1}}],
             "body": {"name": "child", "kind": "InstanceBody", "addr": 4,
                      "members": [{"kind": "Assignment", "name": "", "members": [{"kind": "IntegerLiteral"}]}]}}
        ]}, "definitions": []}"#;
        let pruned = prune(text);
        let rendered = serde_json::to_string(&pruned.document)
            .unwrap_or_else(|error| panic!("serialize: {error}"));
        assert!(
            !rendered.contains("connections"),
            "connections must be dropped: {rendered}"
        );
        assert!(
            !rendered.contains("Assignment"),
            "members of an InstanceBody are filtered: {rendered}"
        );
    }

    #[test]
    fn the_retention_budget_keeps_definitions_and_reports_partial_retention() {
        let text = r#"{"design": {"kind": "Root", "members": [
            {"name": "top", "kind": "Instance", "addr": 3,
             "body": {"name": "top", "kind": "InstanceBody", "addr": 4,
                      "members": [{"name": "a", "kind": "Port", "type": "logic", "direction": "In"}]}}
        ]}, "definitions": [
            {"name": "top", "kind": "Definition", "addr": 5, "definitionKind": "Module"}
        ]}"#;
        // A zero node budget drops every budgeted structural node.
        let pruned =
            prune_json(text.as_bytes(), 0).unwrap_or_else(|error| panic!("prune: {error}"));
        assert!(
            !pruned.retained_fully,
            "the budget must report a partial walk"
        );
        let definitions = pruned
            .document
            .get("definitions")
            .and_then(Value::as_array)
            .unwrap_or_else(|| panic!("definitions must survive the budget"));
        assert_eq!(definitions.len(), 1);
        let design_members = pruned
            .document
            .get("design")
            .and_then(|design| design.get("members"))
            .and_then(Value::as_array)
            .cloned()
            .unwrap_or_default();
        assert!(
            design_members.is_empty(),
            "budgeted instances are not retained: {design_members:?}"
        );
    }

    #[test]
    fn malformed_documents_are_reported() {
        assert!(prune_json(b"{ not json".as_slice(), u64::MAX).is_err());
        assert!(prune_json(b"[1, 2, 3]".as_slice(), u64::MAX).is_err());
    }

    #[test]
    fn measure_nesting_counts_containers_and_ignores_strings() {
        let doc = b"{\"a\": [1, {\"b\": \"}]]\"}], \"c\": {}}";
        let measured = measure_nesting(doc.as_slice(), u64::MAX)
            .unwrap_or_else(|error| panic!("scan: {error}"));
        assert_eq!(measured, Some(3), "object -> array -> object");
        let strings_only = br#"{"a":"{{{[[["}"#;
        let measured = measure_nesting(strings_only.as_slice(), u64::MAX)
            .unwrap_or_else(|error| panic!("scan: {error}"));
        assert_eq!(measured, Some(1), "braces inside strings do not count");
    }

    #[test]
    fn measure_nesting_stops_early_above_the_cap() {
        let deep = format!("{}{}", "[".repeat(300), "]".repeat(300));
        let measured =
            measure_nesting(deep.as_bytes(), 128).unwrap_or_else(|error| panic!("scan: {error}"));
        assert_eq!(
            measured, None,
            "a document past the cap is reported as such"
        );
        let measured =
            measure_nesting(deep.as_bytes(), 300).unwrap_or_else(|error| panic!("scan: {error}"));
        assert_eq!(
            measured,
            Some(300),
            "at the cap the measured depth is returned"
        );
    }

    #[test]
    fn the_walk_parses_a_document_beyond_serde_json_s_default_guard() {
        // 200 nested arrays are past serde_json's 128-value guard; the catalog
        // disables that guard after measuring the document, so valid deep
        // designs are read instead of refused.
        let deep = format!(
            "{{\"design\": {{\"members\": []}}, \"definitions\": [], \"pad\": {}{}}}",
            "[".repeat(200),
            "]".repeat(200)
        );
        let pruned =
            prune_json(deep.as_bytes(), u64::MAX).unwrap_or_else(|error| panic!("prune: {error}"));
        assert!(pruned.document.is_object());
    }
}
