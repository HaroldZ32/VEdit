//! Private slang-server configuration for an explicitly imported project.

use std::collections::BTreeMap;
use std::io;
use std::path::Path;

use serde_json::{Map, Value};

use crate::session::create_private_file;
use crate::slang_flags::CompilerFlags;

/// Preserve the server's three configuration layers and their merge order.
pub(crate) fn write_layers(
    private_root: &Path,
    workspace: &Path,
    environment: &BTreeMap<String, String>,
    patterns: Vec<String>,
    override_flags: &str,
) -> io::Result<()> {
    let workspace_settings = workspace.join(".slang");
    let home = environment
        .get("HOME")
        .map(std::ffi::OsString::from)
        .or_else(|| std::env::var_os("HOME"));
    let private_settings = private_root.join(".slang");
    let private_home = private_root.join("home/.slang");
    std::fs::create_dir_all(private_settings.join("local"))?;
    std::fs::create_dir_all(&private_home)?;
    let mut compiler = CompilerFlags::new(workspace, private_root, environment);
    let mut shared = read_layer(&workspace_settings.join("server.json"), workspace)?;
    let mut user = match home {
        Some(home) => read_layer(&Path::new(&home).join(".slang/server.json"), workspace)?,
        None => Map::new(),
    };
    let mut local = read_layer(&workspace_settings.join("local/server.json"), workspace)?;

    // The upstream loader selects nonempty shared flags in preference to the
    // user's flags, then appends the local flags. Keep ignored files untouched.
    let has_flags = |layer: &Map<String, Value>| {
        layer
            .get("flags")
            .and_then(Value::as_str)
            .is_some_and(|flags| !flags.is_empty())
    };
    let shared_has_flags = has_flags(&shared);
    if shared_has_flags {
        user.remove("flags");
    }
    let use_user_flags = !shared_has_flags && has_flags(&user);
    for layer in [&mut shared, &mut user, &mut local] {
        if let Some(Value::String(flags)) = layer.get_mut("flags") {
            *flags = compiler.rebase(flags)?;
        }
    }
    if !override_flags.is_empty() {
        // Slang takes the first -D for a name, including before nested -f/-F.
        let layer = if use_user_flags {
            &mut user
        } else {
            &mut shared
        };
        let mut flags = override_flags.to_owned();
        if let Some(Value::String(native)) = layer.get("flags") {
            flags.push_str(native);
        }
        layer.insert("flags".to_string(), Value::String(flags));
    }
    // An empty project index still explicitly disables the workspace crawl.
    shared.insert("index".to_string(), serde_json::json!([{"dirs": []}]));
    shared.insert("indexGlobs".to_string(), serde_json::json!(patterns));
    write_layer(&private_settings.join("server.json"), shared)?;
    write_layer(&private_home.join("server.json"), user)?;
    write_layer(&private_settings.join("local/server.json"), local)
}

fn write_layer(path: &Path, layer: Map<String, Value>) -> io::Result<()> {
    create_private_file(path, &Value::Object(layer).to_string())
}

fn read_layer(path: &Path, workspace: &Path) -> io::Result<Map<String, Value>> {
    let text = match std::fs::read_to_string(path) {
        Ok(text) => text,
        Err(error) if error.kind() == io::ErrorKind::NotFound => return Ok(Map::new()),
        Err(error) => {
            return Err(io::Error::new(
                error.kind(),
                format!("{}: {error}", path.display()),
            ))
        }
    };
    let mut layer: Map<String, Value> =
        serde_json::from_slice(&strict_json(&text)).map_err(|error| {
            io::Error::new(
                io::ErrorKind::InvalidData,
                format!("{}: {error}", path.display()),
            )
        })?;
    // Index and startup build selection belong to the imported filelist. The
    // compiler options and the remaining server settings keep their native merge.
    for key in [
        "index",
        "indexGlobs",
        "excludeDirs",
        "build",
        "builds",
        "buildPattern",
    ] {
        layer.remove(key);
    }
    if let Some(Value::Array(directories)) = layer.get_mut("incdirs") {
        for directory in directories {
            if let Value::String(path) = directory {
                *path = workspace.join(&*path).to_string_lossy().into_owned();
            }
        }
    }
    Ok(layer)
}

// The pinned server accepts comments and trailing commas. Keep strings (and
// their escapes) verbatim while removing those two JSONC extensions.
fn strict_json(text: &str) -> Vec<u8> {
    let mut bytes = text.as_bytes().iter().copied().peekable();
    let mut result = Vec::with_capacity(text.len());
    let mut in_string = false;
    let mut pending_comma = false;
    while let Some(byte) = bytes.next() {
        if in_string {
            result.push(byte);
            if byte == b'\\' {
                result.extend(bytes.next());
            } else if byte == b'"' {
                in_string = false;
            }
            continue;
        }
        match (byte, bytes.peek()) {
            (b'/', Some(b'/')) => {
                for next in bytes.by_ref() {
                    if next == b'\n' {
                        break;
                    }
                }
                result.push(b'\n');
                continue;
            }
            (b'/', Some(b'*')) => {
                bytes.next();
                while let Some(next) = bytes.next() {
                    if next == b'*' && bytes.next_if_eq(&b'/').is_some() {
                        break;
                    }
                    if bytes.peek().is_none() {
                        result.push(b'/');
                    }
                }
                result.push(b' ');
                continue;
            }
            _ => {}
        }
        if pending_comma && !byte.is_ascii_whitespace() {
            if !matches!(byte, b'}' | b']') {
                result.push(b',');
            }
            pending_comma = false;
        }
        if byte == b',' && !pending_comma {
            pending_comma = true;
        } else {
            result.push(byte);
            in_string = byte == b'"';
        }
    }
    if pending_comma {
        result.push(b',');
    }
    result
}
