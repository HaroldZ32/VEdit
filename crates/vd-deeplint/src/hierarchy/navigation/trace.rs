use super::*;
use crate::tool_paths::check_cancelled;
use std::convert::Infallible;

const NODE_LIMIT: usize = 512;
const DEPTH_LIMIT: usize = 32;

type Sites = HashMap<PathBuf, HashMap<(u32, u32), Vec<usize>>>;

/// Read-only index of one filelist elaboration. Cached by the hierarchy job.
pub struct TraceIndex {
    nets: Vec<SignalInfo>,
    sites: Sites,
    paths: HashMap<String, usize>,
    elapsed_ms: u64,
    peak_rss_kb: u64,
    warning: Option<String>,
}

impl TraceIndex {
    /// Build the cursor and instance indices on the elaboration worker.
    #[must_use]
    pub fn from_report(report: HierarchyReport) -> Self {
        let mut columns = Columns::new();
        match Self::build(
            report,
            || Ok::<_, Infallible>(()),
            |file, line, byte| Ok(columns.char_column(file, line, byte)),
        ) {
            Ok(index) => index,
            Err(never) => match never {},
        }
    }

    /// Build a trace index, checking for an invalidated snapshot between nets,
    /// positions and source-file reads.
    ///
    /// # Errors
    /// Returns `LintFailure::Cancelled` if the caller releases the job.
    pub fn from_report_cancellable(
        report: HierarchyReport,
        cancel: &AtomicBool,
    ) -> Result<Self, LintFailure> {
        let mut columns = Columns::new();
        Self::build(
            report,
            || check_cancelled(cancel),
            |file, line, byte| columns.char_column_cancellable(file, line, byte, cancel),
        )
    }

    fn build<E>(
        mut report: HierarchyReport,
        check: impl Fn() -> Result<(), E>,
        mut column: impl FnMut(&Path, u32, u32) -> Result<u32, E>,
    ) -> Result<Self, E> {
        let mut sites: Sites = HashMap::new();
        let mut paths = HashMap::new();
        for (id, net) in report.signals.iter_mut().enumerate() {
            check()?;
            paths.insert(net.path.clone(), id);
            for (file, line, byte) in net.declaration.iter().chain(&net.occurrences) {
                let column = column(file, *line, *byte)?;
                let entry = sites
                    .entry(file.clone())
                    .or_default()
                    .entry((*line, column))
                    .or_default();
                if entry.last() != Some(&id) {
                    entry.push(id);
                }
            }
            net.occurrences.clear();
            if let Some((file, line, byte)) = &mut net.declaration {
                *byte = column(file, *line, *byte)?;
            }
            for edge in net.drivers.iter_mut().chain(&mut net.loads) {
                edge.column = column(&edge.file, edge.line, edge.column)?;
            }
        }
        check()?;
        Ok(Self {
            nets: report.signals,
            sites,
            paths,
            elapsed_ms: report.elapsed.as_millis().try_into().unwrap_or(u64::MAX),
            peak_rss_kb: report.peak_rss_kb,
            warning: report.warning,
        })
    }

    /// Build a bounded directional tree for a UTF-16 token start.
    #[must_use]
    pub fn query(&self, file: &Path, line: u32, column: u32, drivers: bool, full: bool) -> Value {
        let mut walk = Walk {
            index: self,
            drivers,
            full,
            remaining: NODE_LIMIT,
            truncated: false,
            ancestors: HashSet::new(),
        };
        let mut roots = Vec::new();
        let matches = self
            .sites
            .get(file)
            .and_then(|sites| sites.get(&(line, column)));
        for &id in matches.into_iter().flatten() {
            if !walk.take() {
                break;
            }
            let net = &self.nets[id];
            let site = net
                .declaration
                .as_ref()
                .map(|(file, line, column)| json!({"file":file,"line":line,"column":column}));
            let children = walk.children(id, 0);
            roots.push(json!({"path":net.path, "location":site, "children":children}));
        }
        json!({"roots": roots, "matched": matches.map_or(0, Vec::len), "truncated": walk.truncated,
               "elapsedMs": self.elapsed_ms, "peakRssKb": self.peak_rss_kb, "warning": self.warning})
    }
}

struct Walk<'a> {
    index: &'a TraceIndex,
    drivers: bool,
    full: bool,
    remaining: usize,
    truncated: bool,
    ancestors: HashSet<usize>,
}

impl Walk<'_> {
    fn take(&mut self) -> bool {
        if self.remaining == 0 {
            self.truncated = true;
            return false;
        }
        self.remaining -= 1;
        true
    }

    fn children(&mut self, id: usize, depth: usize) -> Vec<Value> {
        let net = &self.index.nets[id];
        let edges = if self.drivers {
            &net.drivers
        } else {
            &net.loads
        };
        if depth == DEPTH_LIMIT {
            self.truncated |= !edges.is_empty();
            return Vec::new();
        }
        self.ancestors.insert(id);
        let mut rows = Vec::new();
        let mut emitted = HashSet::new();
        for edge in edges {
            let port = matches!(
                edge.kind,
                EndpointKind::InstanceInputPort | EndpointKind::InstanceOutputPort
            );
            let path = if self.full || port {
                &edge.path
            } else {
                &net.path
            };
            if !emitted.insert((path, &edge.file, edge.line, edge.column, &edge.detail)) {
                continue;
            }
            if !self.take() {
                break;
            }
            let peer = edge
                .source
                .as_ref()
                .and_then(|path| self.index.paths.get(path));
            let cycle = peer.is_some_and(|next| self.ancestors.contains(next));
            let children = match peer {
                Some(&next) if self.full && !cycle => self.children(next, depth + 1),
                _ => Vec::new(),
            };
            // `source` is the peer in the traversal direction. Keep the result's
            // endpoints in signal-flow order even when walking upstream.
            let peer_path = edge.source.as_deref();
            let net_path = Some(net.path.as_str());
            let (from, to) = if self.drivers {
                (peer_path, net_path)
            } else {
                (net_path, peer_path)
            };
            rows.push(json!({
                "path": path, "from": from, "to": to, "detail": edge.detail,
                "cycle": cycle, "children": children,
                "location": {"file": edge.file, "line": edge.line, "column": edge.column}
            }));
        }
        self.ancestors.remove(&id);
        rows
    }
}
