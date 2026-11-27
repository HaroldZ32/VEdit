use super::{
    collect_assignments, endpoint, expr_position, node_kind, EndpointKind, HierarchyReport,
    SignalInfo,
};
use crate::{tool_paths::Columns, LintFailure};
use serde_json::{json, Value};
use std::cell::Cell;
use std::collections::{HashMap, HashSet};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, Ordering};

mod ast;
mod trace;
pub(super) use ast::read_ast;
pub use trace::TraceIndex;

const GRAPH_BUDGET: usize = 256 * 1024 * 1024;

pub(super) struct GraphBudget<'a> {
    remaining: Cell<usize>,
    cancel: &'a AtomicBool,
}

impl<'a> GraphBudget<'a> {
    pub(super) fn new(cancel: &'a AtomicBool) -> Self {
        Self {
            remaining: Cell::new(GRAPH_BUDGET),
            cancel,
        }
    }

    // An AST may reference one body from many instances. Bound the expanded
    // graph separately, including the storage for its path and cursor indices.
    pub(super) fn charge(&self, size: usize) -> Result<(), LintFailure> {
        if self.cancel.load(Ordering::Relaxed) {
            return Err(LintFailure::Cancelled);
        }
        let remaining =
            self.remaining
                .get()
                .checked_sub(size)
                .ok_or_else(|| LintFailure::InvalidAstJson {
                    message: "the design exceeds the signal trace graph budget".to_string(),
                })?;
        self.remaining.set(remaining);
        Ok(())
    }
}

fn collect_reads<'a>(value: &'a Value, reads: &mut Vec<&'a Value>, skip_assignments: bool) {
    match value {
        Value::Object(fields) => match node_kind(value) {
            "NamedValue" => reads.push(value),
            "Assignment" if skip_assignments => {}
            "Instance" | "InstanceBody" | "Subroutine" | "GenerateBlockArray" | "GenerateBlock" => {
            }
            _ => fields
                .values()
                .for_each(|child| collect_reads(child, reads, skip_assignments)),
        },
        Value::Array(values) => values
            .iter()
            .for_each(|child| collect_reads(child, reads, skip_assignments)),
        _ => {}
    }
}

fn writes<'a>(expr: &'a Value, out: &mut Vec<&'a Value>) {
    match node_kind(expr) {
        "NamedValue" => out.push(expr),
        "Concatenation" => {
            if let Some(values) = expr.get("operands").and_then(Value::as_array) {
                values.iter().for_each(|value| writes(value, out));
            }
        }
        "Assignment" => writes(&expr["left"], out),
        "ElementSelect" | "RangeSelect" | "MemberAccess" | "Conversion" | "Parenthesized" => {
            writes(&expr["value"], out);
        }
        _ => {}
    }
}

struct Scope<'a> {
    ids: HashMap<u64, usize>,
    nets: &'a mut [SignalInfo],
    budget: &'a GraphBudget<'a>,
}

impl Scope<'_> {
    fn index(&self, expr: &Value) -> Option<usize> {
        let (addr, _) = expr.get("symbol")?.as_str()?.split_once(' ')?;
        self.ids.get(&addr.parse().ok()?).copied()
    }

    fn edge(
        &mut self,
        net: usize,
        peer: Option<String>,
        at: &Value,
        kind: EndpointKind,
        label: &str,
        driver: bool,
    ) -> Result<(), LintFailure> {
        let signal = &mut self.nets[net];
        let Some(site) = expr_position(at) else {
            // Still bound the cross-product work when no location can be emitted.
            return self.budget.charge(1);
        };
        let peer_len = peer.as_ref().map_or(signal.path.len(), String::len);
        self.budget
            .charge(384 + 2 * (peer_len + site.0.as_os_str().len()) + label.len())?;
        signal.occurrences.push(site.clone());
        let mut edge = endpoint(
            peer.clone().unwrap_or_else(|| signal.path.clone()),
            kind,
            label.to_owned(),
            Some(site),
        );
        edge.source = peer;
        if driver {
            &mut signal.drivers
        } else {
            &mut signal.loads
        }
        .push(edge);
        Ok(())
    }

    fn assignment(
        &mut self,
        left: &Value,
        right: &Value,
        kind: EndpointKind,
        label: &str,
    ) -> Result<(), LintFailure> {
        let mut targets = Vec::new();
        writes(left, &mut targets);
        let destinations: Vec<_> = targets
            .iter()
            .filter_map(|&value| Some((self.index(value)?, value)))
            .collect();
        let mut reads = Vec::new();
        collect_reads(right, &mut reads, false);
        // A select's index is read, even when its base is written. Compare
        // node identity so another occurrence with an identical span survives.
        let mut left_reads = Vec::new();
        collect_reads(left, &mut left_reads, false);
        let target_nodes: HashSet<_> = targets
            .iter()
            .map(|&node| std::ptr::from_ref(node))
            .collect();
        reads.extend(
            left_reads
                .into_iter()
                .filter(|&node| !target_nodes.contains(&std::ptr::from_ref(node))),
        );
        let sources: Vec<_> = reads
            .into_iter()
            .filter_map(|value| Some((self.index(value)?, value)))
            .collect();
        for &(destination, site) in &destinations {
            if sources.is_empty() {
                self.edge(destination, None, site, kind, label, true)?;
            }
            for &(source, _) in &sources {
                self.edge(
                    destination,
                    Some(self.nets[source].path.clone()),
                    site,
                    kind,
                    label,
                    true,
                )?;
            }
        }
        for (source, site) in sources {
            if destinations.is_empty() {
                self.edge(source, None, site, kind, label, false)?;
            }
            for &(destination, _) in &destinations {
                self.edge(
                    source,
                    Some(self.nets[destination].path.clone()),
                    site,
                    kind,
                    label,
                    false,
                )?;
            }
        }
        Ok(())
    }

    fn port(&mut self, connection: &Value, child: &str) -> Result<(), LintFailure> {
        let port = &connection["port"];
        let Some(name) = port["name"].as_str() else {
            return Ok(());
        };
        let direction = port["direction"].as_str().unwrap_or_default();
        let expr = &connection["expr"];
        let peer = format!("{child}.{name}");
        let label = format!("Port {name} ({direction})");
        if matches!(direction, "Out" | "InOut") {
            let mut targets = Vec::new();
            writes(expr, &mut targets);
            for site in targets {
                if let Some(net) = self.index(site) {
                    self.edge(
                        net,
                        Some(peer.clone()),
                        site,
                        EndpointKind::InstanceOutputPort,
                        &label,
                        true,
                    )?;
                }
            }
        }
        if matches!(direction, "In" | "InOut") {
            let mut reads = Vec::new();
            collect_reads(expr, &mut reads, false);
            for site in reads {
                if let Some(net) = self.index(site) {
                    self.edge(
                        net,
                        Some(peer.clone()),
                        site,
                        EndpointKind::InstanceInputPort,
                        &label,
                        false,
                    )?;
                }
            }
        }
        Ok(())
    }
}

pub(super) fn record_scope(
    members: &[&Value],
    children: &[(&Value, String)],
    nets: &mut [SignalInfo],
    budget: &GraphBudget<'_>,
) -> Result<(), LintFailure> {
    let by_name: HashMap<_, _> = nets
        .iter()
        .enumerate()
        .filter_map(|(i, signal)| Some((signal.path.rsplit_once('.')?.1, i)))
        .collect();
    let ids = members
        .iter()
        .filter(|value| matches!(node_kind(value), "Net" | "Variable" | "Port"))
        .filter_map(|value| {
            Some((
                value["addr"].as_u64()?,
                *by_name.get(value["name"].as_str()?)?,
            ))
        })
        .collect();
    let mut scope = Scope { ids, nets, budget };
    for (child, path) in children {
        budget.charge(0)?;
        if let Some(connections) = child["connections"].as_array() {
            for connection in connections {
                scope.port(connection, path)?;
            }
        }
    }
    for member in members {
        budget.charge(0)?;
        match node_kind(member) {
            "ContinuousAssign" => {
                let assignment = &member["assignment"];
                scope.assignment(
                    &assignment["left"],
                    &assignment["right"],
                    EndpointKind::ContinuousAssign,
                    "assign",
                )?;
            }
            "ProceduralBlock" => {
                let label = member["procedureKind"].as_str().unwrap_or("procedural");
                let mut found = Vec::new();
                collect_assignments(&member["body"], &mut found);
                for assignment in found {
                    scope.assignment(
                        &assignment["left"],
                        &assignment["right"],
                        EndpointKind::Procedural,
                        label,
                    )?;
                }
                let mut reads = Vec::new();
                collect_reads(&member["body"], &mut reads, true);
                for site in reads {
                    if let Some(net) = scope.index(site) {
                        scope.edge(net, None, site, EndpointKind::Procedural, label, false)?;
                    }
                }
            }
            _ => {}
        }
    }
    Ok(())
}

pub(super) fn connect_parent_ports(
    nets: &mut [SignalInfo],
    budget: &GraphBudget<'_>,
) -> Result<(), LintFailure> {
    let indices: HashMap<_, _> = nets
        .iter()
        .enumerate()
        .map(|(i, net)| (net.path.clone(), i))
        .collect();
    let mut opposite = Vec::new();
    let declarations: Vec<_> = nets.iter().map(|net| net.declaration.clone()).collect();
    for net in &mut *nets {
        budget.charge(0)?;
        for (edges, driver) in [(&mut net.drivers, true), (&mut net.loads, false)] {
            for edge in edges {
                if !matches!(
                    edge.kind,
                    EndpointKind::InstanceInputPort | EndpointKind::InstanceOutputPort
                ) {
                    continue;
                }
                if let Some(&child) = indices.get(&edge.path) {
                    let destination_len = declarations[child]
                        .as_ref()
                        .map_or(0, |site| site.0.as_os_str().len());
                    budget.charge(
                        384 + 2 * (net.path.len() + edge.file.as_os_str().len())
                            + edge.detail.len()
                            + destination_len,
                    )?;
                    let mut reverse = edge.clone();
                    reverse.path.clone_from(&net.path);
                    reverse.source = Some(net.path.clone());
                    opposite.push((child, reverse, !driver));
                    if let Some((file, line, column)) = &declarations[child] {
                        edge.file.clone_from(file);
                        edge.line = *line;
                        edge.column = *column;
                    }
                }
            }
        }
    }
    for (child, edge, driver) in opposite {
        let net = &mut nets[child];
        if driver {
            &mut net.drivers
        } else {
            &mut net.loads
        }
        .push(edge);
    }
    Ok(())
}
