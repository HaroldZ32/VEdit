use std::io::{BufRead, Write};
use std::path::Path;
use std::time::Duration;

use anyhow::{bail, ensure, Context, Result};
use serde_json::{json, Value};

pub const MODE: &str = "VEDIT_TEST_SESSION_HEALTH_MODE";
pub const EVENTS: &str = "wire.log";
pub const IDLE_STDERR: &str = "fixture idle exit";
pub const STARTUP_STDERR: &str = "fixture rejected initialization";
pub const UNSAVED: &str = "module unsaved; endmodule\n";
pub const DEFINE: &str = "WIDTH=8 + 4";

pub fn run(mode: &str) -> Result<()> {
    let pid = std::process::id();
    let mut events = std::fs::OpenOptions::new()
        .create(true)
        .append(true)
        .open(EVENTS)?;
    writeln!(events, "spawn {pid}")?;
    let mut output = std::fs::OpenOptions::new().write(true).open("/dev/fd/3")?;
    let mut input = std::io::stdin().lock();
    std::thread::spawn(move || {
        let marker = format!("exit-{pid}");
        while !Path::new(&marker).is_file() {
            std::thread::sleep(Duration::from_millis(10));
        }
        eprintln!("{IDLE_STDERR}");
        std::process::exit(23);
    });
    while let Some(message) = read_frame(&mut input)? {
        let result = match message["method"].as_str() {
            Some("initialize") => {
                if mode == "startup-failure" {
                    eprintln!("{STARTUP_STDERR}");
                    std::process::exit(17);
                }
                json!({"capabilities": {"documentSymbolProvider": true}})
            }
            Some("textDocument/didOpen") => {
                let doc = &message["params"]["textDocument"];
                if doc["text"] == UNSAVED {
                    writeln!(events, "open {pid} unsaved")?;
                }
                send(
                    &mut output,
                    &json!({
                        "jsonrpc": "2.0", "method": "textDocument/publishDiagnostics",
                        "params": {"uri": doc["uri"], "diagnostics": []}
                    }),
                )?;
                continue;
            }
            Some("textDocument/didChange") => {
                if message["params"]["contentChanges"][0]["text"] == UNSAVED {
                    writeln!(events, "change {pid} unsaved")?;
                }
                continue;
            }
            Some("workspace/executeCommand") => {
                let params = &message["params"];
                ensure!(
                    params["command"] == "slang.setBuildFile",
                    "unexpected command: {params}"
                );
                let path = params["arguments"][0]
                    .as_str()
                    .context("missing build file")?;
                let content = std::fs::read_to_string(path)?;
                ensure!(
                    content.contains("+incdir+")
                        && content.contains(DEFINE)
                        && content.contains("top.sv"),
                    "missing build context inputs"
                );
                writeln!(events, "context {pid}")?;
                Value::Null
            }
            Some("textDocument/documentSymbol") => json!([]),
            Some("shutdown") => Value::Null,
            Some("exit") => return Ok(()),
            Some("initialized") => continue,
            other => bail!("unexpected fixture method: {other:?}"),
        };
        send(
            &mut output,
            &json!({"jsonrpc": "2.0", "id": message["id"], "result": result}),
        )?;
    }
    Ok(())
}

fn read_frame(input: &mut impl BufRead) -> Result<Option<Value>> {
    let mut header = String::new();
    if input.read_line(&mut header)? == 0 {
        return Ok(None);
    }
    let len = header
        .strip_prefix("Content-Length: ")
        .context("missing Content-Length")?
        .trim()
        .parse()?;
    header.clear();
    input.read_line(&mut header)?;
    ensure!(header == "\r\n", "expected blank LSP header line");
    let mut body = vec![0; len];
    input.read_exact(&mut body)?;
    Ok(Some(serde_json::from_slice(&body)?))
}

fn send(output: &mut impl Write, message: &Value) -> Result<()> {
    let body = serde_json::to_vec(message)?;
    write!(output, "Content-Length: {}\r\n\r\n", body.len())?;
    output.write_all(&body)?;
    output.flush()?;
    Ok(())
}
