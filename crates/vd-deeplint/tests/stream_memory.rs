//! The streaming catalog must not allocate the `--ast-json` DOCUMENT.
//!
//! A `serde_json::Value` parse of a real dump costs several times the file size
//! (the reason the old 256 MiB "cap" existed, and the reason it failed the
//! catalog rather than OOMing the user's machine). The streaming walk keeps only
//! the catalog nodes, so this test catalogues a document many times larger than
//! any plausible working set and asserts the PROCESS did not grow by anything
//! like the document size.
//!
//! The measurement uses the kernel's own high-water mark (`VmHWM` in
//! `/proc/self/status`): the counting-allocator trick is not available here
//! because the workspace forbids unsafe code. The document is written
//! incrementally, so the test process never holds it either.

use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};

use vd_deeplint::{build_catalog, CatalogModule, CatalogOptions};

/// One expression-shaped node. It uses the REAL layout of a slang expression —
/// the source span first, `kind` LAST — so a walker cannot cheat by peeking at
/// the first key, and it carries enough text to dominate a document.
const FILLER: &str = "{\"source_file\":\"top.sv\",\"source_file_end\":\"top.sv\",\"source_line\":1,\"source_line_end\":1,\"source_column\":1,\"source_column_end\":2,\"kind\":\"Assignment\",\"symbol\":\"assign w = a ^ b; this filler is what makes a real --ast-json dump large\"}";

const FILLERS: usize = 220_000;
/// Below this the assertion would not distinguish a streaming walk from a full
/// `Value` parse (which costs several times the document).
const MIN_DOCUMENT_BYTES: u64 = 40 * 1024 * 1024;
/// The walk may grow the process by its own structures, not by the document.
const MAX_RSS_GROWTH_KB: u64 = 16 * 1024;

fn write_large_document(path: &Path, fillers: usize) -> u64 {
    let file = fs::File::create(path).unwrap_or_else(|error| panic!("creating the AST: {error}"));
    let mut writer = std::io::BufWriter::new(file);
    let prefix = r#"{"design":{"name":"$root","kind":"Root","addr":1,"members":[{"name":"top","kind":"Instance","source_file":"top.sv","source_line":1,"source_column":1,"addr":2,"body":{"name":"top","kind":"InstanceBody","source_file":"top.sv","source_line":1,"source_column":1,"addr":3,"members":[{"name":"clk","kind":"Port","source_file":"top.sv","source_line":1,"source_column":10,"addr":4,"type":"logic","direction":"In"},{"name":"q","kind":"Port","source_file":"top.sv","source_line":1,"source_column":20,"addr":5,"type":"logic","direction":"Out"}"#;
    let suffix = "]}}]},\"definitions\":[{\"name\":\"top\",\"kind\":\"Definition\",\"addr\":6,\"definitionKind\":\"Module\"}]}";
    writer
        .write_all(prefix.as_bytes())
        .unwrap_or_else(|error| panic!("writing the AST prefix: {error}"));
    let mut written = prefix.len() as u64;
    for _ in 0..fillers {
        writer
            .write_all(b",")
            .and_then(|()| writer.write_all(FILLER.as_bytes()))
            .unwrap_or_else(|error| panic!("writing a filler: {error}"));
        written += 1 + FILLER.len() as u64;
    }
    writer
        .write_all(suffix.as_bytes())
        .unwrap_or_else(|error| panic!("writing the AST suffix: {error}"));
    written += suffix.len() as u64;
    writer
        .flush()
        .unwrap_or_else(|error| panic!("flushing the AST: {error}"));
    written
}

/// The process's peak resident set in KiB (`VmHWM`), if the kernel exposes it.
fn peak_rss_kb() -> Option<u64> {
    let status = fs::read_to_string("/proc/self/status").ok()?;
    for line in status.lines() {
        if let Some(rest) = line.strip_prefix("VmHWM:") {
            return rest.split_whitespace().next()?.parse::<u64>().ok();
        }
    }
    None
}

fn fake_slang_copying(dir: &Path, source: &Path) -> PathBuf {
    let fake = dir.join("fake-slang");
    let script = format!(
        "#!/bin/sh\nprev=\"\"\nfor arg in \"$@\"; do\n  if [ \"$prev\" = \"--ast-json\" ]; then out=\"$arg\"; fi\n  prev=\"$arg\"\ndone\ncp '{}' \"$out\"\nexit 0\n",
        source.display()
    );
    fs::write(&fake, script).unwrap_or_else(|error| panic!("writing the fake tool: {error}"));
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let mut permissions = fs::metadata(&fake)
            .unwrap_or_else(|error| panic!("stat the fake tool: {error}"))
            .permissions();
        permissions.set_mode(0o755);
        fs::set_permissions(&fake, permissions)
            .unwrap_or_else(|error| panic!("chmod the fake tool: {error}"));
    }
    fake
}

fn module<'a>(modules: &'a [CatalogModule], name: &str) -> &'a CatalogModule {
    modules
        .iter()
        .find(|module| module.name == name)
        .unwrap_or_else(|| panic!("module {name} missing"))
}

#[test]
fn the_streaming_catalog_does_not_allocate_the_document() {
    let Some(baseline_kb) = peak_rss_kb() else {
        eprintln!("stream-memory: /proc/self/status has no VmHWM on this platform; skipped");
        return;
    };
    let dir = tempfile::tempdir().unwrap_or_else(|error| panic!("tempdir: {error}"));
    let source = dir.path().join("ast.json");
    let write_started = std::time::Instant::now();
    let document_bytes = write_large_document(&source, FILLERS);
    let write_ms = write_started.elapsed().as_millis();
    assert!(
        document_bytes > MIN_DOCUMENT_BYTES,
        "the fixture must dwarf the catalog's own structures: {document_bytes} bytes"
    );

    let fake = fake_slang_copying(dir.path(), &source);
    let project = dir.path().join("project");
    fs::create_dir_all(&project).unwrap_or_else(|error| panic!("mkdir: {error}"));
    let top = project.join("top.sv");
    fs::write(&top, "module top; endmodule\n").unwrap_or_else(|error| panic!("top.sv: {error}"));
    let files = vec![top];

    let catalog_started = std::time::Instant::now();
    let catalog = build_catalog(
        &files,
        &CatalogOptions {
            slang_binary: Some(fake),
            // The size note is disabled: the point of this test is memory.
            max_json_bytes: 0,
            ..CatalogOptions::default()
        },
    );
    let catalog_ms = catalog_started.elapsed().as_millis();
    let peak_kb = peak_rss_kb().unwrap_or(baseline_kb);
    let growth_kb = peak_kb.saturating_sub(baseline_kb);

    assert!(
        catalog.failure.is_none(),
        "the catalog failed on the large document: {:?}",
        catalog.failure
    );
    assert_eq!(catalog.modules.len(), 1, "the module must be catalogued");
    let top = module(&catalog.modules, "top");
    assert_eq!(top.ports.len(), 2, "both ports survive the prune");

    println!(
        "stream-memory: document={document_bytes} bytes, write_ms={write_ms}, catalog_ms={catalog_ms}, \
         peak_rss_baseline={baseline_kb} KiB, peak_rss_after={peak_kb} KiB, growth={growth_kb} KiB \
         (bound {MAX_RSS_GROWTH_KB} KiB)"
    );
    assert!(
        growth_kb < MAX_RSS_GROWTH_KB,
        "cataloguing a {document_bytes}-byte document grew the process by {growth_kb} KiB: \
         the walker must keep the catalog nodes, not the document"
    );
    assert!(
        (document_bytes / 1024) > 2 * growth_kb,
        "the measurement must clearly separate the document from the retained catalog"
    );
}
